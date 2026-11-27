// Phase-2: paired A/B package-power measurement across N cores.
//
// Why: one core's AMX/AVX state is ~1 W or less, while the shared socket
// draws ~100 W and drifts by +-1 W as other users come and go (slot 1-2
// hold_state could not tell any state apart). This probe fixes both:
//   * N worker threads (one per physical core in `cpus`) run the SAME state
//     at once, so the signal is N x the per-core difference;
//   * states alternate in short windows in an ABBA pattern, and the result is
//     the mean of paired (A - B) differences, so slow drift cancels.
// Workers follow a shared TSC schedule (TSC is synchronous across cores).
// The main thread reads RAPL package/DRAM energy at every window boundary.
// The first `skip` windows (settling) are excluded from the summary.
//
// A state is op:delta_ns:burst, e.g. dpbssd:0:64 (saturated AMX),
// dpbssd:20000:1 (AMX held in the ~5.7k gated state), none:0:1 (scalar spin).
//
// Usage: build/power_ab [key=val ...] >> out.csv
//   cpus=2-9,11-17  a=dpbssd:0:64  b=none:0:1  win_ms=250  windows=200
//   skip=4  main=10 (CPU for the RAPL reader)  pkg=auto (from first cpu)
//   label=   free text copied into the CSV     header=0|1
// CSV (one row per window):
//   label,n_cores,a,b,win,state,dt_s,pkg_w,dram_w
// stderr: mean A, mean B, paired diff A-B with 95% CI, per-core diff.
#define _GNU_SOURCE
#include "timer.h"
#include "amx.h"
#include "args.h"
#include <math.h>
#include <pthread.h>

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
typedef struct { int op; uint64_t n; int burst; char name[64]; } state_t;

static int parse_op(const char *s) {
    return !strcmp(s, "dpbssd") ? OP_DP : !strcmp(s, "fma512") ? OP_F512
         : !strcmp(s, "fma256") ? OP_F256 : !strcmp(s, "none") ? OP_NONE : -1;
}
static void parse_state(const char *s, state_t *st, double mhz) {
    char op[32] = {0}; double d = 0; int b = 1;
    if (sscanf(s, "%31[^:]:%lf:%d", op, &d, &b) < 1 || (st->op = parse_op(op)) < 0 || b < 1) {
        fprintf(stderr, "bad state '%s' (want op:delta_ns:burst)\n", s); exit(2);
    }
    st->n = (uint64_t)(d * mhz / 1e3); st->burst = b;
    snprintf(st->name, sizeof st->name, "%s", s);
}
static inline __attribute__((always_inline)) void do_op(int op) {
    if (op == OP_DP) _tile_dpbssd(0, 1, 2);
    else if (op == OP_F512) __asm__ volatile(ZERO10 ".rept 8\n" FMA8("zmm") ".endr\n" ::: CLOB);
    else if (op == OP_F256) __asm__ volatile(ZERO10 ".rept 8\n" FMA8("ymm") ".endr\n" ::: CLOB);
}

// ABBA ABBA ...: window w runs A when (w % 4 == 0 || w % 4 == 3)
static inline int is_a(uint64_t w) { uint64_t m = w & 3; return m == 0 || m == 3; }

static state_t SA, SB;
static uint64_t T0, WT, NW;              // schedule start, window ticks, windows
static volatile uint64_t g_ops[2];      // ops completed in A / B (all workers)
static volatile int g_ready;

typedef struct { int cpu; } warg_t;
static void *worker(void *p) {
    warg_t *w = p;
    pin_cpu(w->cpu);
    if (SA.op == OP_DP || SB.op == OP_DP) amx_init();
    __atomic_add_fetch(&g_ready, 1, __ATOMIC_SEQ_CST);
    uint64_t t0;
    while (__rdtsc() < (t0 = __atomic_load_n(&T0, __ATOMIC_SEQ_CST))) ;
    uint64_t end = t0 + WT * NW, ops[2] = {0, 0};
    for (;;) {
        uint64_t t = __rdtsc();
        if (t >= end) break;
        int a = is_a((t - t0) / WT);
        const state_t *s = a ? &SA : &SB;
        while (__rdtsc() - t < s->n) ;
        for (int i = 0; i < s->burst; i++) do_op(s->op);
        ops[!a] += s->burst;
    }
    __atomic_add_fetch(&g_ops[0], ops[0], __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&g_ops[1], ops[1], __ATOMIC_SEQ_CST);
    return NULL;
}

static int rd_str(const char *p, char *buf, size_t n) {
    FILE *f = fopen(p, "r"); if (!f) return -1;
    if (!fgets(buf, n, f)) { fclose(f); return -1; }
    fclose(f); buf[strcspn(buf, "\n")] = 0; return 0;
}
static uint64_t rd_u64(const char *p) {
    char b[64]; if (rd_str(p, b, sizeof b)) { fprintf(stderr, "read %s failed\n", p); exit(1); }
    return strtoull(b, NULL, 10);
}
static uint64_t e_diff(uint64_t a, uint64_t b, uint64_t max) { return b >= a ? b - a : b + max - a; }

static int parse_cpus(const char *s, int *out, int max) {
    int n = 0; const char *p = s;
    while (*p && n < max) {
        char *e; long a = strtol(p, &e, 10), b = a;
        if (e == p) break;
        if (*e == '-') { p = e + 1; b = strtol(p, &e, 10); }
        for (long i = a; i <= b && n < max; i++) out[n++] = (int)i;
        p = (*e == ',') ? e + 1 : e;
        if (*e != ',' ) break;
    }
    return n;
}

int main(int argc, char **argv) {
    int cpus[256], nc = parse_cpus(kv(argc, argv, "cpus", "2-9,11-17"), cpus, 256);
    double win_ms = atof(kv(argc, argv, "win_ms", "250"));
    int windows = atoi(kv(argc, argv, "windows", "200"));
    int skip = atoi(kv(argc, argv, "skip", "4"));
    int mainc = atoi(kv(argc, argv, "main", "10"));
    int header = atoi(kv(argc, argv, "header", "0"));
    const char *label = kv(argc, argv, "label", "");
    if (nc < 1 || windows < 8 || skip >= windows) { fprintf(stderr, "bad cpus/windows/skip\n"); return 2; }

    // RAPL zone for the first worker's package
    char p[512], name[64], pz[256] = "", dz[256] = "";
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpus[0]);
    int pkg = atoi(kv(argc, argv, "pkg", "-1")); if (pkg < 0) pkg = (int)rd_u64(p);
    char want[32]; snprintf(want, sizeof want, "package-%d", pkg);
    for (int i = 0; i < 8 && !pz[0]; i++) {
        snprintf(p, sizeof p, "/sys/class/powercap/intel-rapl:%d/name", i);
        if (rd_str(p, name, sizeof name) || strcmp(name, want)) continue;
        snprintf(pz, sizeof pz, "/sys/class/powercap/intel-rapl:%d", i);
        for (int j = 0; j < 4; j++) {
            snprintf(p, sizeof p, "%s/intel-rapl:%d:%d/name", pz, i, j);
            if (!rd_str(p, name, sizeof name) && !strcmp(name, "dram")) { snprintf(dz, sizeof dz, "%s/intel-rapl:%d:%d", pz, i, j); break; }
        }
    }
    if (!pz[0]) { fprintf(stderr, "no RAPL zone %s\n", want); return 1; }
    char pe[300], de[300];
    snprintf(pe, sizeof pe, "%s/energy_uj", pz); snprintf(de, sizeof de, "%s/energy_uj", dz);
    snprintf(p, sizeof p, "%s/max_energy_range_uj", pz); uint64_t pmax = rd_u64(p);
    uint64_t dmax = 0; if (dz[0]) { snprintf(p, sizeof p, "%s/max_energy_range_uj", dz); dmax = rd_u64(p); }

    pin_cpu(mainc);
    double mhz = tsc_mhz();
    parse_state(kv(argc, argv, "a", "dpbssd:0:64"), &SA, mhz);
    parse_state(kv(argc, argv, "b", "none:0:1"), &SB, mhz);
    WT = (uint64_t)(win_ms * mhz * 1e3); NW = windows;
    T0 = UINT64_MAX;

    pthread_t th[256]; warg_t wa[256];
    for (int i = 0; i < nc; i++) { wa[i].cpu = cpus[i]; pthread_create(&th[i], NULL, worker, &wa[i]); }
    while (__atomic_load_n(&g_ready, __ATOMIC_SEQ_CST) < nc) usleep(1000);
    uint64_t t0 = __rdtsc() + (uint64_t)(0.05 * mhz * 1e6);   // start 50 ms from now
    __atomic_store_n(&T0, t0, __ATOMIC_SEQ_CST);

    uint64_t *pe_v = malloc(8 * (NW + 1)), *de_v = malloc(8 * (NW + 1)), *tt = malloc(8 * (NW + 1));
    for (uint64_t w = 0; w <= NW; w++) {
        uint64_t target = t0 + w * WT;
        while (__rdtsc() + (uint64_t)(2 * mhz * 1e3) < target) usleep(1000);   // coarse sleep, then spin
        while (__rdtsc() < target) ;
        tt[w] = __rdtsc();
        pe_v[w] = rd_u64(pe); de_v[w] = dz[0] ? rd_u64(de) : 0;
    }
    for (int i = 0; i < nc; i++) pthread_join(th[i], NULL);

    if (header) printf("label,n_cores,a,b,win,state,dt_s,pkg_w,dram_w\n");
    double sa = 0, sb = 0; int na = 0, nb = 0;
    double *pw = malloc(sizeof(double) * NW);
    for (uint64_t w = 0; w < NW; w++) {
        double dt = (tt[w + 1] - tt[w]) / (mhz * 1e6);
        pw[w] = e_diff(pe_v[w], pe_v[w + 1], pmax) / 1e6 / dt;
        double dw = dz[0] ? e_diff(de_v[w], de_v[w + 1], dmax) / 1e6 / dt : 0;
        int a = is_a(w);
        printf("%s,%d,%s,%s,%lu,%s,%.4f,%.3f,%.3f\n", label, nc, SA.name, SB.name, w, a ? "A" : "B", dt, pw[w], dw);
        if ((int)w < skip) continue;
        if (a) { sa += pw[w]; na++; } else { sb += pw[w]; nb++; }
    }
    // paired differences over ABBA blocks: (A0 + A3 - B1 - B2) / 2 per block
    double s = 0, s2 = 0; int nbk = 0;
    for (uint64_t k = (skip + 3) / 4 * 4; k + 3 < NW; k += 4) {
        double d = (pw[k] + pw[k + 3] - pw[k + 1] - pw[k + 2]) / 2;
        s += d; s2 += d * d; nbk++;
    }
    double m = nbk ? s / nbk : 0, sd = nbk > 1 ? sqrt((s2 - nbk * m * m) / (nbk - 1)) : 0;
    double ci = nbk > 1 ? 1.96 * sd / sqrt(nbk) : 0;
    double secs_a = (double)WT * (NW / 2) / (mhz * 1e6), secs_b = secs_a;   // ABBA: half the windows each
    fprintf(stderr, "%s n=%d A=%s B=%s | A %.2f W  B %.2f W | A-B %.3f +- %.3f W (95%%, %d blocks) = %.3f W/core | ops/s/core A %.3g B %.3g\n",
            label, nc, SA.name, SB.name, na ? sa / na : 0, nb ? sb / nb : 0, m, ci, nbk, m / nc,
            g_ops[0] / secs_a / nc, g_ops[1] / secs_b / nc);
    fflush(stdout);
    return 0;
}
