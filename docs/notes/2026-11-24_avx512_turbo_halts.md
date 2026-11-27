# 2026-11-24 — AVX-512 baseline: turbo license halts dominate

Host artemisia, CPU 45 (sibling 101 idle), turbo ON (no root changes yet).
jkalyan running 13× `get_pmcs_ipex.py` on CPUs 28-40 at the time.

## Timer (bench/timer_test) — OK
- Stamp overhead: 40 TSC / 54 core cycles median, p99 +2.
- 1000 dependent IMUL → 2998 net core cycles (expect 3000). RDPMC fixed counter works.
- 1000 `add $1,reg` → only ~223 cycles: rename-time immediate folding on Golden Cove. Don't use ADD-imm chains for calibration.

## Single-FMA probe — not usable
- One FMA (~4 cyc) sits under ~55 cyc of timer overhead; results flipped direction
  depending on stamp composition (TSC-only vs cycles-only vs both, extra stamp before s0).
  Classified as harness artifacts. Probe now times a 64-FMA throughput block.

## Finding: every vector burst triggers a ~1.3 µs core clock halt (turbo ON)
`bench/license_timeline` (sample TSC+cycles for 40 µs after one 64-FMA block;
halt = TSC gap where core cycles < 50% of expected at 2 GHz):

| op | reps w/ halt | first halt after burst | total halted |
|---|---|---|---|
| fma512 | 49/49 | ~2.3 µs | ~1.29 µs (≈3 chunks of 200-500 ns) |
| fma256 | 49/49 | ~4.1 µs | ~1.31 µs |
| none   | 2/49  | random | — |

- Occurs after every idle length tested (effective ≥40 µs up to 10 ms).
- Onset depends on idle: ≤100 µs idle → 4-8 µs; ≥300 µs idle → ~2.3-2.5 µs.
- Consistent with a frequency/voltage **license transition** (DVFS), not unit
  power gating: 256-bit triggers it too, and the 64-FMA blocks run ~4× below
  peak throughput (512 and 256 alike) except in a narrow window (Δ≈1.78 µs).
- Also explains the earlier `f_idle` dip (≈1.18 µs of missing cycles in every idle window ≥3 µs).

## Implication
With turbo on, AVX-512 (and very likely AMX, which has its own 2.5 GHz license)
latency-vs-idle data is confounded by license transitions: the Hertzbleed
risk from BACKGROUND.md §7, now observed directly. **Phase-1 data must be
collected with turbo off + frequency pinned (`sudo env/setup_env.sh`)**, then
re-run `license_timeline` to confirm halts are gone.

Side note: halt onset differs between 512-bit and 256-bit and with idle time,
so the license transition is itself a timing signal (possible extra channel to
compare against gating).
