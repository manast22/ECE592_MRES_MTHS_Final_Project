// Diagnostic: does a vector burst trigger a core clock halt (frequency /
// voltage license transition)? After a long scalar idle, run one 64-FMA
// block, then sample (TSC, core cycles) back-to-back for WINDOW_US. A gap
// where core cycles stop advancing while TSC does = core halted.
//
// Usage: taskset -c <cpu> build/license_timeline [cpu=45] [reps=50] [op=fma512|fma256|none] [idle_us=2000]
// stdout: per rep, halts as  rep,start_ns,dur_ns  (relative to end of burst)
// stderr: summary (count, median start, median duration, total halted per rep)
#include "timer.h"

#define WINDOW_US 40
#define MAXS 400000

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

static uint64_t st[MAXS], sc[MAXS];

int main(int argc, char **argv) {
    int cpu = argc > 1 ? atoi(argv[1]) : 45;
    int reps = argc > 2 ? atoi(argv[2]) : 50;
    const char *op = argc > 3 ? argv[3] : "fma512";
    double idle_us = argc > 4 ? atof(argv[4]) : 2000;
    pin_cpu(cpu);
    cyc_t c; cyc_open(&c);
    double mhz = tsc_mhz();
    uint64_t win = (uint64_t)(WINDOW_US * mhz), idle = (uint64_t)(idle_us * mhz);
    double ns_per_tick = 1e3 / mhz;

    // A halt = consecutive samples whose TSC gap is > GAP_NS while core cycles
    // advanced far less than TSC-time * nominal freq.
    const double GAP_NS = 100;
    uint64_t *nh = calloc(reps, 8);
    double *hs = malloc(8 * reps * 64), *hd = malloc(8 * reps * 64), *tot = calloc(reps, 8);
    int nall = 0;

    printf("rep,start_ns,dur_ns,cyc_in_gap\n");
    for (int r = 0; r < reps; r++) {
        spin_tsc(idle);
        if (!strcmp(op, "fma512"))      __asm__ volatile(ZERO10 ".rept 8\n" FMA8("zmm") ".endr\n" ::: CLOB);
        else if (!strcmp(op, "fma256")) __asm__ volatile(ZERO10 ".rept 8\n" FMA8("ymm") ".endr\n" ::: CLOB);
        stamp_t s = stamp(&c);
        size_t n = 0;
        st[n] = s.tsc; sc[n] = s.cyc; n++;
        while (n < MAXS && st[n - 1] - st[0] < win) {
            s = stamp(&c); st[n] = s.tsc; sc[n] = s.cyc; n++;
        }
        for (size_t i = 1; i < n; i++) {
            double gap = (st[i] - st[i - 1]) * ns_per_tick;
            if (gap > GAP_NS) {
                uint64_t dc = (sc[i] - sc[i - 1]) & c.mask;
                double start = (st[i - 1] - st[0]) * ns_per_tick;
                printf("%d,%.0f,%.0f,%lu\n", r, start, gap, dc);
                if (nall < reps * 64) { hs[nall] = start; hd[nall] = gap; nall++; }
                nh[r]++; tot[r] += gap;
            }
        }
    }
    // summary
    uint64_t *tmp = malloc(8 * (nall + 1));
    for (int i = 0; i < nall; i++) tmp[i] = (uint64_t)hs[i];
    uint64_t ms = nall ? pct(tmp, nall, 50) : 0;
    for (int i = 0; i < nall; i++) tmp[i] = (uint64_t)hd[i];
    uint64_t md = nall ? pct(tmp, nall, 50) : 0, mx = nall ? tmp[nall - 1] : 0;
    int with = 0; double tsum = 0;
    for (int r = 0; r < reps; r++) { with += nh[r] > 0; tsum += tot[r]; }
    fprintf(stderr, "op=%s cpu=%d reps=%d idle_us=%.0f window_us=%d tsc_mhz=%.0f\n", op, cpu, reps, idle_us, WINDOW_US, mhz);
    fprintf(stderr, "reps_with_halt=%d/%d  halts=%d  median_start=%lu ns  median_dur=%lu ns  max_dur=%lu ns  mean_halted_per_rep=%.0f ns\n",
            with, reps, nall, ms, md, mx, tsum / reps);
    return 0;
}
