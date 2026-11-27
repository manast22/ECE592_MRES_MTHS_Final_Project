// Validates bench/common/timer.h.
//   1. empty   : back-to-back stamps -> timer overhead (subtract from probes)
//   2. chain   : 1000 dependent IMULs (3 cycles each) -> expect ~3000 core cycles
//                net of overhead. (ADD reg,imm is folded at rename on Golden
//                Cove: 1000 ADDs measured ~223 cycles, so it is not used.)
// Usage: taskset -c <cpu> build/timer_test [cpu=45] [n=100000]
#include "timer.h"

#define CHAIN 3000

static void report(const char *name, uint64_t *t, uint64_t *c, size_t n, double mhz) {
    uint64_t tmed = pct(t, n, 50), tmin = t[0], t99 = pct(t, n, 99), tmax = t[n - 1];
    uint64_t cmed = pct(c, n, 50), cmin = c[0], c99 = pct(c, n, 99), cmax = c[n - 1];
    printf("%-6s tsc  min=%5lu med=%5lu p99=%6lu max=%8lu\n", name, tmin, tmed, t99, tmax);
    printf("%-6s cyc  min=%5lu med=%5lu p99=%6lu max=%8lu   core~%.0f MHz\n",
           name, cmin, cmed, c99, cmax, tmed ? mhz * (double)cmed / tmed : 0);
}

int main(int argc, char **argv) {
    int cpu = argc > 1 ? atoi(argv[1]) : 45;
    size_t n = argc > 2 ? strtoul(argv[2], 0, 10) : 100000;
    pin_cpu(cpu);
    cyc_t c; cyc_open(&c);
    double mhz = tsc_mhz();
    printf("cpu=%d n=%zu tsc_mhz=%.1f rdpmc_idx=%d pmc_width=%u\n",
           cpu, n, mhz, c.idx, (unsigned)c.pc->pmc_width);

    uint64_t *et = malloc(n * 8), *ec = malloc(n * 8), *ht = malloc(n * 8), *hc = malloc(n * 8);

    for (size_t i = 0; i < n / 10; i++) { stamp_t a = stamp(&c), b = stamp(&c); (void)a; (void)b; } // warm-up

    for (size_t i = 0; i < n; i++) {
        stamp_t a = stamp(&c), b = stamp(&c);
        et[i] = d_tsc(a, b); ec[i] = d_cyc(&c, a, b);
    }
    uint64_t x = 3;
    for (size_t i = 0; i < n; i++) {
        stamp_t a = stamp(&c);
        __asm__ volatile(".rept 1000\n\timul %0, %0\n\t.endr" : "+r"(x));
        stamp_t b = stamp(&c);
        ht[i] = d_tsc(a, b); hc[i] = d_cyc(&c, a, b);
    }
    report("empty", et, ec, n, mhz);
    report("chain", ht, hc, n, mhz);

    uint64_t net = pct(hc, n, 50) - pct(ec, n, 50);
    printf("chain net core cycles = %lu (expect ~%d)  -> %s\n", net, CHAIN,
           (net > CHAIN * 0.95 && net < CHAIN * 1.05) ? "PASS" : "CHECK");
    return x == 42; // keep x live
}
