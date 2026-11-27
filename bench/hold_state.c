// Phase-2: hold a unit at a fixed reuse distance and measure power.
//
// Loop for `secs`: scalar spin delta_ns -> one op. Package and DRAM energy are
// read from RAPL sysfs (world-readable on artemisia) before and after; a 0.5 s
// unmeasured warm-up lets the unit settle into the state first. Run op=none at
// the same delta as the baseline: other users share the package, so compare
// interleaved runs rather than absolute watts.
//
// Usage: taskset -c <cpu> build/hold_state [key=val ...] >> out.csv
//   cpu=10  op=dpbssd|fma512|fma256|none  delta_ns=1000  secs=5  header=0|1
//   burst=1   ops issued back-to-back per iteration (e.g. burst=64 with
//             delta_ns=0 saturates the unit: "fully active" reference power)
// CSV: unit,delta_ns,burst,secs,ops,ops_per_s,pkg_w,dram_w,core_mhz,cpu
#include "timer.h"
#include "amx.h"
#include "args.h"

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

enum { OP_NONE, OP_DP, OP_F512, OP_F256 };

static int rd_str(const char *p, char *buf, size_t n) {
    FILE *f = fopen(p, "r"); if (!f) return -1;
    if (!fgets(buf, n, f)) { fclose(f); return -1; }
    fclose(f); buf[strcspn(buf, "\n")] = 0; return 0;
}
static uint64_t rd_u64(const char *p) {
    char b[64]; if (rd_str(p, b, sizeof b)) { fprintf(stderr, "read %s failed\n", p); exit(1); }
    return strtoull(b, NULL, 10);
}
// Find RAPL package zone for physical package `pkg` and its dram subzone.
static void rapl_find(int pkg, char *pz, char *dz) {
    char p[256], name[64], want[32];
    snprintf(want, sizeof want, "package-%d", pkg);
    pz[0] = dz[0] = 0;
    for (int i = 0; i < 8; i++) {
        snprintf(p, sizeof p, "/sys/class/powercap/intel-rapl:%d/name", i);
        if (rd_str(p, name, sizeof name) || strcmp(name, want)) continue;
        snprintf(pz, 256, "/sys/class/powercap/intel-rapl:%d", i);
        for (int j = 0; j < 4; j++) {
            snprintf(p, sizeof p, "%s/intel-rapl:%d:%d/name", pz, i, j);
            if (!rd_str(p, name, sizeof name) && !strcmp(name, "dram")) { snprintf(dz, 256, "%s/intel-rapl:%d:%d", pz, i, j); break; }
        }
        return;
    }
    fprintf(stderr, "no RAPL zone %s\n", want); exit(1);
}
static uint64_t e_diff(uint64_t a, uint64_t b, uint64_t max) { return b >= a ? b - a : b + max - a; }

static inline __attribute__((always_inline)) void do_op(int op) {
    if (op == OP_DP) _tile_dpbssd(0, 1, 2);
    else if (op == OP_F512) __asm__ volatile(ZERO10 ".rept 8\n" FMA8("zmm") ".endr\n" ::: CLOB);
    else if (op == OP_F256) __asm__ volatile(ZERO10 ".rept 8\n" FMA8("ymm") ".endr\n" ::: CLOB);
}

int main(int argc, char **argv) {
    int cpu = atoi(kv(argc, argv, "cpu", "10"));
    const char *ops = kv(argc, argv, "op", "dpbssd");
    double delta = atof(kv(argc, argv, "delta_ns", "1000"));
    double secs = atof(kv(argc, argv, "secs", "5"));
    int header = atoi(kv(argc, argv, "header", "0"));
    int burst = atoi(kv(argc, argv, "burst", "1"));
    if (burst < 1) burst = 1;
    int op = !strcmp(ops, "dpbssd") ? OP_DP : !strcmp(ops, "fma512") ? OP_F512
           : !strcmp(ops, "fma256") ? OP_F256 : !strcmp(ops, "none") ? OP_NONE : -1;
    if (op < 0) { fprintf(stderr, "bad op\n"); return 2; }

    char p[256], pz[256], dz[256];
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
    rapl_find((int)rd_u64(p), pz, dz);
    char pe[300], pm[300], de[300], dm[300];
    snprintf(pe, sizeof pe, "%s/energy_uj", pz); snprintf(pm, sizeof pm, "%s/max_energy_range_uj", pz);
    snprintf(de, sizeof de, "%s/energy_uj", dz); snprintf(dm, sizeof dm, "%s/max_energy_range_uj", dz);
    uint64_t pmax = rd_u64(pm), dmax = dz[0] ? rd_u64(dm) : 0;

    pin_cpu(cpu);
    cyc_t c; cyc_open(&c);
    if (op == OP_DP) amx_init();
    double mhz = tsc_mhz();
    uint64_t n = (uint64_t)(delta * mhz / 1e3);

    // warm-up: settle into the steady state for this delta
    uint64_t w0 = __rdtsc(), wn = (uint64_t)(0.5 * mhz * 1e6);
    while (__rdtsc() - w0 < wn) { uint64_t t = __rdtsc(); while (__rdtsc() - t < n) ; for (int i = 0; i < burst; i++) do_op(op); }

    uint64_t e0 = rd_u64(pe), d0 = dz[0] ? rd_u64(de) : 0;
    stamp_t a = stamp(&c);
    uint64_t len = (uint64_t)(secs * mhz * 1e6), ops_n = 0;
    while (__rdtsc() - a.tsc < len) {
        uint64_t t = __rdtsc(); while (__rdtsc() - t < n) ;
        for (int i = 0; i < burst; i++) do_op(op);
        ops_n += burst;
    }
    stamp_t b = stamp(&c);
    uint64_t e1 = rd_u64(pe), d1 = dz[0] ? rd_u64(de) : 0;

    double dt = d_tsc(a, b) / (mhz * 1e6);
    double pw = e_diff(e0, e1, pmax) / 1e6 / dt, dw = dz[0] ? e_diff(d0, d1, dmax) / 1e6 / dt : 0;
    double core = mhz * d_cyc(&c, a, b) / (double)d_tsc(a, b);
    if (header) printf("unit,delta_ns,burst,secs,ops,ops_per_s,pkg_w,dram_w,core_mhz,cpu\n");
    printf("%s,%.0f,%d,%.3f,%lu,%.0f,%.3f,%.3f,%.0f,%d\n", ops, delta, burst, dt, ops_n, ops_n / dt, pw, dw, core, cpu);
    fflush(stdout);
    return 0;
}
