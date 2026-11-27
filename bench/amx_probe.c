// Phase-1: AMX power-gating probe (GateBleed replication + idle thresholds).
//
// Each sample: TILEZERO (last AMX use, untimed) -> scalar-only RDTSC spin for
// delta -> time ONE AMX op. If the AMX unit gated during the idle, the op pays
// the wake penalty. GateBleed reports ~50/600/6k/9k/20k core cycles (warm +
// 4 cold states), far above the ~55-cycle timer overhead.
// Deltas: 0, then 100 ns .. dmax_ns at 4 points/decade, shuffled per trial.
//
// Usage: taskset -c <cpu> build/amx_probe [key=val ...] > out.csv
//   cpu=10  trials=100  dmax_ns=1e8 (100 ms; one trial costs ~2.3*dmax)
//   dmin_ns=0   >0 drops delta=0 and grid points below it (long-idle sweeps,
//               e.g. dmin_ns=1.7e8 dmax_ns=1e10 trials=6 ~ 2.3 min)
//   ppd=4       grid points per decade, 100 ns * 10^(i/ppd) (ppd=4 gives
//               1, 1.78, 3.16, 5.62 per decade; ppd=16 for fine threshold
//               sweeps, e.g. dmin_ns=200 dmax_ns=1e4 ppd=16)
//   op=dpbssd     one TDPBSSD
//      dpbssd_st  TDPBSSD + TILESTORED (forces the result to memory)
//      tileload   one TILELOADD (load path only)
//      none       empty timed region (harness control)
//      full       TILERELEASE before the idle (AMX state back to INIT), then
//                 time LDTILECFG + 2x TILELOADD + TDPBSSD + TILESTORED
//   idle=spin     scalar RDTSC spin (core stays in C0)
//        sleep    clock_nanosleep (core may halt / enter C1); timer slack 1 ns.
//                 Actual idle is recorded in idle_ns: use that, not delta_ns.
// CSV  : unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu,idle_ns,t_ns
//        (raw; timer overhead included; idle_ns = measured idle window;
//         t_ns = wall clock (CLOCK_REALTIME ns) of the sample, for joining
//         with bench/sys_logger.sh)
// stderr: config, overhead, and per delta: median tsc, median/p90/p99 cyc,
//         f_idle = median core MHz across the idle spin.
#include "timer.h"
#include "amx.h"
#include "args.h"
#include <math.h>
#include <sys/prctl.h>

#define MAXD 160
enum { OP_NONE, OP_DP, OP_DPST, OP_LOAD, OP_FULL };

static inline __attribute__((always_inline)) void spin_np(uint64_t n) {
    uint64_t t0 = __rdtsc();
    while (__rdtsc() - t0 < n) ;
}
static inline void sleep_ns(double ns) {
    if (ns <= 0) return;
    struct timespec t = { (time_t)(ns / 1e9), (long)((ns - (time_t)(ns / 1e9) * 1e9)) };
    clock_nanosleep(CLOCK_MONOTONIC, 0, &t, NULL);
}
static uint64_t rng = 88172645463325252ULL;
static inline uint64_t xs(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(int argc, char **argv) {
    int cpu = atoi(kv(argc, argv, "cpu", "10"));
    int trials = atoi(kv(argc, argv, "trials", "100"));
    double dmax = atof(kv(argc, argv, "dmax_ns", "1e8"));
    double dmin = atof(kv(argc, argv, "dmin_ns", "0"));
    const char *ops = kv(argc, argv, "op", "dpbssd");
    int op = !strcmp(ops, "dpbssd") ? OP_DP : !strcmp(ops, "dpbssd_st") ? OP_DPST
           : !strcmp(ops, "tileload") ? OP_LOAD : !strcmp(ops, "none") ? OP_NONE
           : !strcmp(ops, "full") ? OP_FULL : -1;
    const char *idles = kv(argc, argv, "idle", "spin");
    int sleepy = !strcmp(idles, "sleep");
    if (op < 0 || trials < 1 || (!sleepy && strcmp(idles, "spin"))) { fprintf(stderr, "bad op/trials/idle\n"); return 2; }
    if (sleepy) prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0);

    // ppd=4 keeps the exact 7-digit multipliers of slots 1-2 so delta values match across runs
    static const double MULT[4] = {1.0, 1.778279, 3.162278, 5.623413};
    int ppd = atoi(kv(argc, argv, "ppd", "4"));
    if (ppd < 1 || ppd > 32) { fprintf(stderr, "bad ppd\n"); return 2; }
    double D[MAXD]; size_t nd = 0;
    if (dmin <= 0) D[nd++] = 0;
    for (double dec = 100; nd < MAXD; dec *= 10) {
        int stop = 0;
        for (int i = 0; i < ppd && nd < MAXD; i++) {
            double d = dec * (ppd == 4 ? MULT[i] : round(pow(10.0, (double)i / ppd) * 1e6) / 1e6);
            if (d > dmax * 1.0001) { stop = 1; break; }
            if (d >= dmin * 0.9999) D[nd++] = d;
        }
        if (stop) break;
    }
    if (nd == 0) { fprintf(stderr, "empty delta grid\n"); return 2; }

    pin_cpu(cpu);
    cyc_t c; cyc_open(&c);
    amx_init();
    double mhz = tsc_mhz();

    enum { NO = 20000 };
    static uint64_t ot[NO], oc[NO];
    for (int i = 0; i < NO; i++) { stamp_t a = stamp(&c), b = stamp(&c); ot[i] = d_tsc(a, b); oc[i] = d_cyc(&c, a, b); }
    fprintf(stderr, "unit=amx_%s idle=%s cpu=%d trials=%d deltas=%zu dmin_ns=%.0f dmax_ns=%.0f tsc_mhz=%.1f overhead_med tsc=%lu cyc=%lu\n",
            ops, idles, cpu, trials, nd, dmin, dmax, mhz, pct(ot, NO, 50), pct(oc, NO, 50));
    struct timespec rt0; clock_gettime(CLOCK_REALTIME, &rt0);
    uint64_t tsc0 = __rdtsc();
    double real0 = rt0.tv_sec * 1e9 + rt0.tv_nsec;

    size_t N = nd * (size_t)trials;
    uint64_t *rt = malloc(8 * N), *rc = malloc(8 * N), *fi = malloc(8 * N), *il = malloc(8 * N), *ts = malloc(8 * N);
    size_t order[MAXD];
    for (size_t i = 0; i < nd; i++) order[i] = i;

    for (int t = 0; t < trials; t++) {
        for (size_t i = nd - 1; i > 0; i--) { size_t j = xs() % (i + 1), s = order[i]; order[i] = order[j]; order[j] = s; }
        for (size_t i = 0; i < nd; i++) {
            size_t d = order[i];
            uint64_t n = (uint64_t)(D[d] * mhz / 1e3);
            if (op == OP_FULL) _tile_release();  // AMX state -> INIT before the idle
            else _tile_zero(0);                  // last AMX activity before the idle window
            stamp_t p = stamp(&c);
            if (sleepy) sleep_ns(D[d]); else spin_np(n);
            stamp_t q = stamp(&c);
            stamp_t s0 = stamp(&c);
            if (op == OP_DP) _tile_dpbssd(0, 1, 2);
            else if (op == OP_DPST) { _tile_dpbssd(0, 1, 2); _tile_stored(0, amx_c, 64); }
            else if (op == OP_LOAD) _tile_loadd(1, amx_a, 64);
            else if (op == OP_FULL) {
                _tile_loadconfig(&amx_cfg); _tile_loadd(1, amx_a, 64); _tile_loadd(2, amx_b, 64);
                _tile_dpbssd(0, 1, 2); _tile_stored(0, amx_c, 64);
            }
            stamp_t s1 = stamp(&c);
            size_t k = d * trials + t;
            rt[k] = d_tsc(s0, s1);
            rc[k] = d_cyc(&c, s0, s1);
            uint64_t it = d_tsc(p, q);
            fi[k] = it ? (uint64_t)(mhz * d_cyc(&c, p, q) / (double)it) : 0;
            il[k] = (uint64_t)(it * 1e3 / mhz);
            ts[k] = (uint64_t)(real0 + (s0.tsc - tsc0) * 1e3 / mhz);
        }
    }

    printf("unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu,idle_ns,t_ns\n");
    for (size_t d = 0; d < nd; d++)
        for (int t = 0; t < trials; t++) {
            size_t k = d * trials + t;
            printf("amx_%s%s,%.0f,%d,%lu,%lu,%.1f,%d,%lu,%lu\n", ops, sleepy ? "_sleep" : "", D[d], t,
                   rt[k], rc[k], mhz, cpu, il[k], ts[k]);
        }

    fprintf(stderr, "%12s %8s %8s %8s %8s %8s\n", "delta_ns", "tsc_med", "cyc_med", "cyc_p90", "cyc_p99", "f_idle");
    for (size_t d = 0; d < nd; d++) {
        uint64_t *T = rt + d * trials, *C = rc + d * trials, *F = fi + d * trials;
        uint64_t tm = pct(T, trials, 50);
        uint64_t cm = pct(C, trials, 50), c90 = pct(C, trials, 90), c99 = pct(C, trials, 99);
        uint64_t fm = pct(F, trials, 50);
        fprintf(stderr, "%12.0f %8lu %8lu %8lu %8lu %8lu\n", D[d], tm, cm, c90, c99, fm);
    }
    return 0;
}
