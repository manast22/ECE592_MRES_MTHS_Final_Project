// Phase-0 sanity check: request AMX XSTATE permission, run one TDPBSSD,
// and estimate TSC frequency. Build: gcc -O2 -mamx-tile -mamx-int8 check_amx.c
#define _GNU_SOURCE
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define ARCH_REQ_XCOMP_PERM 0x1023
#define XFEATURE_XTILEDATA 18

typedef struct {
    uint8_t palette_id, start_row, reserved[14];
    uint16_t colsb[16];
    uint8_t rows[16];
} __attribute__((packed)) tilecfg_t;

static double now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

int main(void) {
    if (syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA)) {
        perror("ARCH_REQ_XCOMP_PERM"); return 1;
    }
    tilecfg_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.palette_id = 1;
    for (int i = 0; i < 3; i++) { cfg.rows[i] = 16; cfg.colsb[i] = 64; }
    _tile_loadconfig(&cfg);

    static int8_t a[16 * 64], b[16 * 64]; static int32_t c[16 * 16];
    for (int i = 0; i < 16 * 64; i++) { a[i] = 1; b[i] = 1; }
    _tile_loadd(1, a, 64); _tile_loadd(2, b, 64); _tile_zero(0);
    _tile_dpbssd(0, 1, 2);
    _tile_stored(0, c, 64);
    _tile_release();
    printf("AMX OK: c[0]=%d (expect 64)\n", c[0]);

    double t0 = now_ns(); uint64_t r0 = __rdtsc();
    while (now_ns() - t0 < 2e8) ;
    double t1 = now_ns(); uint64_t r1 = __rdtsc();
    printf("TSC freq ~ %.2f MHz\n", (r1 - r0) / (t1 - t0) * 1e3);
    return 0;
}
