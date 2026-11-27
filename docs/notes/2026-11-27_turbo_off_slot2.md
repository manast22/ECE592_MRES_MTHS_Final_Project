# Turbo-off slot 2 — 2026-11-27 15:18 EST (artemisia, CPU 10, 2.0 GHz)

Data: `data/raw/slot_artemisia_20261127-1518/`. verify_env PASS at start and end.
Probes changed since slot 1: AVX-512 `warm_us=100`, AMX long sweep (0.18–10 s),
heavier `hold_state` (10 s x 4 shuffled reps, saturated references).

## AMX wake ladder (dpbssd_st, median core cycles incl. ~54-cycle timer)

| idle            | slot 1 | slot 2 |
|-----------------|-------:|-------:|
| 0 – 316 ns      | 133    | 132    |
| 562 ns – 1.78 µs| ~670   | ~678   |
| 3.16 µs         | 7,660  | 7,660  |
| 5.6 µs – 100 µs | 5,690  | 5,690  |
| 178 µs – 100 ms | 9,370  | **5,690** (9.4k in only 1–9 % of samples) |
| 0.18 – 10 s     | —      | 9,370 in 50–100 % of samples, rest 5.7k |

- First three steps (316–562 ns, 1.78–3.16 µs, 3.16–5.6 µs) reproduce exactly.
- The 5.7k -> 9.4k step is **not set by this core's idle time alone**. Slot 1:
  ~80 % of samples were 9.4k from 178 µs on. Slot 2: ~95 % stayed 5.7k up to
  100 ms, and even at 0.18–10 s only half-to-most samples reached 9.4k.
  Likely a package-level decision (other cores' activity / package C-states),
  which differed between 11:06 and 15:18. Next: log other-core load and
  package C-state residency alongside the sweep.
- No ~20k state up to 10 s idle (a few single samples 12–14k, not a plateau).
- `dpbssd` alone tracks `dpbssd_st` this time (~45 cycles lower throughout).
- `tileload`: no wake penalty (82–182 cycles); small drift up past 10 ms.
- `none` control: flat 57–58 short, ~100 cycles past 100 ms (harness, also in AVX none).
- f_idle drops to ~1450 MHz at 5.6 µs after AMX/tile use: the core halts
  briefly during the idle window, the same halts license_timeline sees.

## AVX-512 with 100 µs warm-up (median cycles, 64-FMA block)

| idle           | fma512 | fma256 | none |
|----------------|-------:|-------:|-----:|
| 0 – 1.78 µs    | 124    | 93     | 62   |
| 3.16 µs – ~2 ms| 232    | 213    | 61 -> 94 at ≥100 µs |
| ≥3.16 ms       | 311    | 295 (≥5.6 ms) | 100 |

- Warm-up gives clean steps now, but **fma256 steps at the same idle times**
  with similar size. So this is not 512-bit upper-lane gating specifically;
  it looks like a common vector-unit (or core) idle mechanism.
- The first step (1.78 -> 3.16 µs) lines up with AMX's 1.78 -> 3.16 µs step.

## Halts (license_timeline, 50 reps)

fma512/fma256/amx: halts in 50/50 reps; none control: 39/50 and 34/50.
Halts in the control repeat in both slots, so most are not caused by the
vector/AMX op. Still unexplained.

## Power (hold_state, mean of 4 reps, pkg W)

All runs 102.5–103.0 W, sd 0.3–1.0 W — including saturated AMX (117M ops/s),
saturated AVX-512, and idle. One core's unit state is invisible in package
RAPL with the rest of the shared socket running. No power result possible
at this granularity; would need per-core energy or an otherwise idle socket.
