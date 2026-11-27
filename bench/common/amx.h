// Shared AMX setup: request XTILEDATA permission, configure 3 tiles
// (tile0 = int32 accumulator 16x16, tile1/tile2 = int8 16x64), load operands,
// and warm up once so the kernel's lazy (XFD) tile-buffer allocation happens
// before any timing.
#pragma once
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define ARCH_REQ_XCOMP_PERM 0x1023
#define XFEATURE_XTILEDATA 18

typedef struct {
    uint8_t palette_id, start_row, reserved[14];
    uint16_t colsb[16];
    uint8_t rows[16];
} __attribute__((packed)) tilecfg_t;

static int8_t amx_a[16 * 64] __attribute__((aligned(64)));
static int8_t amx_b[16 * 64] __attribute__((aligned(64)));
static int32_t amx_c[16 * 16] __attribute__((aligned(64)));
static tilecfg_t amx_cfg __attribute__((aligned(64)));

static inline void amx_init(void) {
    if (syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA)) {
        perror("ARCH_REQ_XCOMP_PERM"); exit(1);
    }
    memset(&amx_cfg, 0, sizeof amx_cfg);
    amx_cfg.palette_id = 1;
    for (int i = 0; i < 3; i++) { amx_cfg.rows[i] = 16; amx_cfg.colsb[i] = 64; }
    _tile_loadconfig(&amx_cfg);
    for (int i = 0; i < 16 * 64; i++) { amx_a[i] = 1; amx_b[i] = 1; }
    _tile_loadd(1, amx_a, 64);
    _tile_loadd(2, amx_b, 64);
    _tile_zero(0);
    for (int i = 0; i < 1000; i++) _tile_dpbssd(0, 1, 2);
    _tile_stored(0, amx_c, 64);
    if (amx_c[0] != 64 * 1000) { fprintf(stderr, "AMX self-test failed: c[0]=%d\n", amx_c[0]); exit(1); }
    _tile_zero(0);
}
