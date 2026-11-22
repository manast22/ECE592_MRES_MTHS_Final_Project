// Sustained load generator used to check the *effective* core frequency per
// instruction class (frequency licenses differ for scalar / AVX-512 / AMX).
// Usage: perf stat -e msr/aperf/,msr/mperf/ taskset -c <cpu> ./freq_probe {scalar|avx512|amx} [secs]
// Effective GHz = TSC_GHz * aperf / mperf.
#define _GNU_SOURCE
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

typedef struct { uint8_t palette, start_row, rsvd[14]; uint16_t colsb[16]; uint8_t rows[16]; }
    __attribute__((packed)) tilecfg_t;

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "scalar";
    time_t end = time(NULL) + (argc > 2 ? atoi(argv[2]) : 2);
    if (!strcmp(mode, "amx")) {
        if (syscall(SYS_arch_prctl, 0x1023 /*ARCH_REQ_XCOMP_PERM*/, 18 /*XTILEDATA*/)) { perror("arch_prctl"); return 1; }
        tilecfg_t c; memset(&c, 0, sizeof c); c.palette = 1;
        for (int i = 0; i < 3; i++) { c.rows[i] = 16; c.colsb[i] = 64; }
        _tile_loadconfig(&c);
        static int8_t a[1024]; _tile_loadd(1, a, 64); _tile_loadd(2, a, 64); _tile_zero(0);
        while (time(NULL) < end) for (int i = 0; i < 100000; i++) _tile_dpbssd(0, 1, 2);
        _tile_release();
    } else if (!strcmp(mode, "avx512")) {
        __m512 x = _mm512_set1_ps(1.f), y = x, z = x, w = x;
        while (time(NULL) < end) for (int i = 0; i < 100000; i++) { x = _mm512_fmadd_ps(x, y, z); w = _mm512_fmadd_ps(w, y, z); }
        volatile float sink = x[0] + w[0]; (void)sink;
    } else {
        volatile uint64_t v = 0; while (time(NULL) < end) for (int i = 0; i < 100000; i++) v += i;
    }
    return 0;
}
