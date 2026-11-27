// Phase-1 baseline: AVX-512 power-gating probe.
//
// After an idle interval delta (scalar-only RDTSC spin), time a BLOCK of 64
// independent vector FMAs (8 accumulators x 8 rounds). Warm, that is
// throughput-bound (~32 core cycles at 2/cycle). If the 512-bit upper lanes
// were gated, the block runs at reduced throughput while they wake, so the
// penalty is far larger than timer noise (~55 cycles), unlike a single FMA.
// A single op (4-cycle latency) proved too small to separate from harness
// artifacts.
//
// Usage: taskset -c <cpu> build/avx512_probe [key=val ...] > out.csv
//   cpu=45  trials=200
//   op=fma512|fma256|none   timed block (fma256 = control without upper lanes,
//                           none = empty region, harness control)
//   spin=nopause|pause      idle loop style
//   warm_us=0               before each idle, run FMA blocks back-to-back for
//                           this long so the voltage/power license transition
//                           finishes before the gap (fma256 warms with 256-bit,
//                           fma512 and none with 512-bit)
// CSV  : unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu   (raw; timer overhead included)
// stderr: config, timer overhead, and per delta: median tsc, median/p90 cyc,
//         f_idle = median core MHz measured across the idle spin.
#include "timer.h"

static const double DELTAS_NS[] = {
    0, 100, 178, 316, 562, 1e3, 1.78e3, 3.16e3, 5.62e3, 1e4, 1.78e4, 3.16e4, 5.62e4,
    1e5, 1.78e5, 3.16e5, 5.62e5, 1e6, 1.78e6, 3.16e6, 5.62e6, 1e7,
};
#define ND (sizeof DELTAS_NS / sizeof DELTAS_NS[0])

enum { OP_NONE, OP_FMA512, OP_FMA256 };

// Zero idioms (handled at rename, no execution) then 64 independent FMAs on
// zeros (no denormal assists).
#define ZERO10 \
    "vxorps %%xmm0,%%xmm0,%%xmm0\n vxorps %%xmm1,%%xmm1,%%xmm1\n vxorps %%xmm2,%%xmm2,%%xmm2\n" \
    "vxorps %%xmm3,%%xmm3,%%xmm3\n vxorps %%xmm4,%%xmm4,%%xmm4\n vxorps %%xmm5,%%xmm5,%%xmm5\n" \
    "vxorps %%xmm6,%%xmm6,%%xmm6\n vxorps %%xmm7,%%xmm7,%%xmm7\n vxorps %%xmm8,%%xmm8,%%xmm8\n" \
    "vxorps %%xmm9,%%xmm9,%%xmm9\n"
#define FMA8(R) \
    "vfmadd231ps %%" R "9,%%" R "8,%%" R "0\n vfmadd231ps %%" R "9,%%" R "8,%%" R "1\n" \
    "vfmadd231ps %%" R "9,%%" R "8,%%" R "2\n vfmadd231ps %%" R "9,%%" R "8,%%" R "3\n" \
    "vfmadd231ps %%" R "9,%%" R "8,%%" R "4\n vfmadd231ps %%" R "9,%%" R "8,%%" R "5\n" \
    "vfmadd231ps %%" R "9,%%" R "8,%%" R "6\n vfmadd231ps %%" R "9,%%" R "8,%%" R "7\n"
#define CLOB "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9"

static inline __attribute__((always_inline)) void block512(void) {
    __asm__ volatile(ZERO10 ".rept 8\n" FMA8("zmm") ".endr\n" ::: CLOB);
}
static inline __attribute__((always_inline)) void block256(void) {
    __asm__ volatile(ZERO10 ".rept 8\n" FMA8("ymm") ".endr\n" ::: CLOB);
}
static inline __attribute__((always_inline)) void spin_nopause(uint64_t n) {
    uint64_t t0 = __rdtsc();
    while (__rdtsc() - t0 < n) ;
}

static uint64_t rng = 88172645463325252ULL;
static inline uint64_t xs(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static const char *arg(int argc, char **argv, const char *k, const char *def) {
    size_t n = strlen(k);
    for (int i = 1; i < argc; i++) if (!strncmp(argv[i], k, n) && argv[i][n] == '=') return argv[i] + n + 1;
    return def;
}

int main(int argc, char **argv) {
    int cpu = atoi(arg(argc, argv, "cpu", "45"));
    int trials = atoi(arg(argc, argv, "trials", "200"));
    const char *ops = arg(argc, argv, "op", "fma512");
    const char *spins = arg(argc, argv, "spin", "nopause");
    int op = !strcmp(ops, "none") ? OP_NONE : !strcmp(ops, "fma256") ? OP_FMA256 : OP_FMA512;
    int pause = !strcmp(spins, "pause");
    double warm_us = atof(arg(argc, argv, "warm_us", "0"));

    pin_cpu(cpu);
    cyc_t c; cyc_open(&c);
    double mhz = tsc_mhz();
    uint64_t wn = (uint64_t)(warm_us * mhz);

    enum { NO = 20000 };
    static uint64_t ot[NO], oc[NO];
    for (int i = 0; i < NO; i++) { stamp_t a = stamp(&c), b = stamp(&c); ot[i] = d_tsc(a, b); oc[i] = d_cyc(&c, a, b); }
    fprintf(stderr, "op=%s spin=%s warm_us=%.0f cpu=%d trials=%d tsc_mhz=%.1f overhead_med tsc=%lu cyc=%lu\n",
            ops, spins, warm_us, cpu, trials, mhz, pct(ot, NO, 50), pct(oc, NO, 50));

    size_t N = ND * (size_t)trials;
    uint64_t *rt = malloc(8 * N), *rc = malloc(8 * N), *fi = malloc(8 * N);
    int order[ND];
    for (size_t i = 0; i < ND; i++) order[i] = i;

    for (int t = 0; t < trials; t++) {
        for (size_t i = ND - 1; i > 0; i--) { size_t j = xs() % (i + 1); int s = order[i]; order[i] = order[j]; order[j] = s; }
        for (size_t i = 0; i < ND; i++) {
            int d = order[i];
            uint64_t n = (uint64_t)(DELTAS_NS[d] * mhz / 1e3);
            if (wn) {
                uint64_t w0 = __rdtsc();
                if (op == OP_FMA256) while (__rdtsc() - w0 < wn) block256();
                else                 while (__rdtsc() - w0 < wn) block512();
            }
            stamp_t p = stamp(&c);
            if (pause) spin_tsc(n); else spin_nopause(n);
            stamp_t q = stamp(&c);                    // end of idle window
            stamp_t s0 = stamp(&c);
            if (op == OP_FMA512) block512(); else if (op == OP_FMA256) block256();
            stamp_t s1 = stamp(&c);
            size_t k = (size_t)d * trials + t;
            rt[k] = d_tsc(s0, s1);
            rc[k] = d_cyc(&c, s0, s1);
            fi[k] = (uint64_t)(mhz * d_cyc(&c, p, q) / (double)d_tsc(p, q));
        }
    }

    printf("unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu\n");
    for (size_t d = 0; d < ND; d++)
        for (int t = 0; t < trials; t++)
            printf("%s,%.0f,%d,%lu,%lu,%.1f,%d\n", ops, DELTAS_NS[d], t,
                   rt[d * trials + t], rc[d * trials + t], mhz, cpu);

    fprintf(stderr, "%12s %8s %8s %8s %8s\n", "delta_ns", "tsc_med", "cyc_med", "cyc_p90", "f_idle");
    for (size_t d = 0; d < ND; d++) {
        uint64_t tm = pct(rt + d * trials, trials, 50);
        uint64_t cm = pct(rc + d * trials, trials, 50), c90 = pct(rc + d * trials, trials, 90);
        uint64_t fm = pct(fi + d * trials, trials, 50);
        fprintf(stderr, "%12.0f %8lu %8lu %8lu %8lu\n", DELTAS_NS[d], tm, cm, c90, fm);
    }
    return 0;
}
