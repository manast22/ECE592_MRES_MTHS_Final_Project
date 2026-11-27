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
//   op=dpbssd     one TDPBSSD
//      dpbssd_st  TDPBSSD + TILESTORED (forces the result to memory)
//      tileload   one TILELOADD (load path only)
//      none       empty timed region (harness control)
// CSV  : unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu   (raw; timer overhead included)
// stderr: config, overhead, and per delta: median tsc, median/p90/p99 cyc,
//         f_idle = median core MHz across the idle spin.
#include "timer.h"
#include "amx.h"
#include "args.h"

#define MAXD 64
enum { OP_NONE, OP_DP, OP_DPST, OP_LOAD };

static inline __attribute__((always_inline)) void spin_np(uint64_t n) {
    uint64_t t0 = __rdtsc();
    while (__rdtsc() - t0 < n) ;
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
           : !strcmp(ops, "tileload") ? OP_LOAD : !strcmp(ops, "none") ? OP_NONE : -1;
    if (op < 0 || trials < 1) { fprintf(stderr, "bad op/trials\n"); return 2; }

    static const double MULT[4] = {1.0, 1.778279, 3.162278, 5.623413};
    double D[MAXD]; size_t nd = 0;
    if (dmin <= 0) D[nd++] = 0;
    for (double dec = 100; nd < MAXD; dec *= 10) {
        int stop = 0;
        for (int i = 0; i < 4 && nd < MAXD; i++) {
            double d = dec * MULT[i];
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
    fprintf(stderr, "unit=amx_%s cpu=%d trials=%d deltas=%zu dmin_ns=%.0f dmax_ns=%.0f tsc_mhz=%.1f overhead_med tsc=%lu cyc=%lu\n",
            ops, cpu, trials, nd, dmin, dmax, mhz, pct(ot, NO, 50), pct(oc, NO, 50));

    size_t N = nd * (size_t)trials;
    uint64_t *rt = malloc(8 * N), *rc = malloc(8 * N), *fi = malloc(8 * N);
    size_t order[MAXD];
    for (size_t i = 0; i < nd; i++) order[i] = i;

    for (int t = 0; t < trials; t++) {
        for (size_t i = nd - 1; i > 0; i--) { size_t j = xs() % (i + 1), s = order[i]; order[i] = order[j]; order[j] = s; }
        for (size_t i = 0; i < nd; i++) {
            size_t d = order[i];
            uint64_t n = (uint64_t)(D[d] * mhz / 1e3);
            _tile_zero(0);                       // last AMX activity before the idle window
            stamp_t p = stamp(&c);
            spin_np(n);
            stamp_t q = stamp(&c);
            stamp_t s0 = stamp(&c);
            if (op == OP_DP) _tile_dpbssd(0, 1, 2);
            else if (op == OP_DPST) { _tile_dpbssd(0, 1, 2); _tile_stored(0, amx_c, 64); }
            else if (op == OP_LOAD) _tile_loadd(1, amx_a, 64);
            stamp_t s1 = stamp(&c);
            size_t k = d * trials + t;
            rt[k] = d_tsc(s0, s1);
            rc[k] = d_cyc(&c, s0, s1);
            uint64_t it = d_tsc(p, q);
            fi[k] = it ? (uint64_t)(mhz * d_cyc(&c, p, q) / (double)it) : 0;
        }
    }

    printf("unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu\n");
    for (size_t d = 0; d < nd; d++)
        for (int t = 0; t < trials; t++)
            printf("amx_%s,%.0f,%d,%lu,%lu,%.1f,%d\n", ops, D[d], t,
                   rt[d * trials + t], rc[d * trials + t], mhz, cpu);

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
