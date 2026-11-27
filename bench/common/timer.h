// Shared timing helpers for all probes.
//
// Every sample records two clocks:
//   tsc : fixed-rate timestamp counter (2.0 GHz on artemisia) = wall time
//   cyc : unhalted core cycles via RDPMC (perf_event mmap)    = real cycles,
//         correct even when turbo / AMX frequency licenses change the clock.
//
// Fencing (one sequence everywhere, start and end identical):
//   LFENCE; RDTSC; RDPMC; LFENCE
// The leading LFENCE waits for all earlier instructions to complete; the
// trailing one stops later instructions from starting before the reads.
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <x86intrin.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

// ---------- pinning ----------
static inline void pin_cpu(int cpu) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    if (sched_setaffinity(0, sizeof s, &s)) { perror("sched_setaffinity"); exit(1); }
}

// ---------- core-cycle counter (RDPMC) ----------
typedef struct {
    int fd;
    struct perf_event_mmap_page *pc;
    int idx;          // RDPMC index (pc->index - 1)
    uint64_t mask;    // counter width mask
} cyc_t;

static inline void cyc_open(cyc_t *c) {
    struct perf_event_attr a; memset(&a, 0, sizeof a);
    a.size = sizeof a;
    a.type = PERF_TYPE_HARDWARE;
    a.config = PERF_COUNT_HW_CPU_CYCLES;
    a.pinned = 1;
    a.exclude_hv = 1;
    c->fd = syscall(SYS_perf_event_open, &a, 0 /*this thread*/, -1, -1, 0);
    if (c->fd < 0) { perror("perf_event_open(cycles)"); exit(1); }
    c->pc = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, c->fd, 0);
    if (c->pc == MAP_FAILED) { perror("mmap perf page"); exit(1); }
    if (!c->pc->cap_user_rdpmc || c->pc->index == 0) {
        fprintf(stderr, "RDPMC not available (cap_user_rdpmc=%u index=%u)\n",
                (unsigned)c->pc->cap_user_rdpmc, c->pc->index);
        exit(1);
    }
    c->idx = c->pc->index - 1;
    c->mask = (c->pc->pmc_width >= 64) ? ~0ULL : ((1ULL << c->pc->pmc_width) - 1);
}

// ---------- stamps ----------
typedef struct { uint64_t tsc, cyc; } stamp_t;

static inline stamp_t stamp(const cyc_t *c) {
    stamp_t s;
    _mm_lfence();
    s.tsc = __rdtsc();
    s.cyc = __rdpmc(c->idx);
    _mm_lfence();
    return s;
}
static inline uint64_t d_tsc(stamp_t a, stamp_t b) { return b.tsc - a.tsc; }
static inline uint64_t d_cyc(const cyc_t *c, stamp_t a, stamp_t b) { return (b.cyc - a.cyc) & c->mask; }

// ---------- TSC frequency ----------
static inline double now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}
static inline double tsc_mhz(void) {
    double t0 = now_ns(); uint64_t r0 = __rdtsc();
    while (now_ns() - t0 < 1e8) ;
    double t1 = now_ns(); uint64_t r1 = __rdtsc();
    return (r1 - r0) / (t1 - t0) * 1e3;
}

// Busy-wait for n TSC ticks (idle interval for reuse-distance sweeps).
static inline void spin_tsc(uint64_t n) {
    uint64_t t0 = __rdtsc();
    while (__rdtsc() - t0 < n) _mm_pause();
}

// ---------- stats ----------
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
// Sorts v in place; returns the p-th percentile (0..100).
static inline uint64_t pct(uint64_t *v, size_t n, double p) {
    qsort(v, n, sizeof *v, cmp_u64);
    size_t i = (size_t)(p / 100.0 * (n - 1) + 0.5);
    return v[i];
}
