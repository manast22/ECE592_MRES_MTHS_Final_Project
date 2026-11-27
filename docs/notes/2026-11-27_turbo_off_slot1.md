# 2026-11-27: first turbo-off slot (socket 0, CPU 10)

Data: `data/raw/slot_artemisia_20261127-1106/` (`scripts/run_slot.sh 10`, 11:06–11:12 ET).
Socket 0 turbo disabled (IA32_MISC_ENABLE[38]) + pinned 2.0 GHz; machine ~98% idle.

## Environment: OK
- `verify_env` PASS at start and end: scalar / AVX-512 / AMX all 2000 MHz (0.0% dev).
- `timer_test`: overhead 54 TSC = 54 cyc; 1000 IMUL = 2997 net cycles; core 2000 MHz.
- TSC and core cycles now agree 1:1, so the two columns can be cross-checked.

## AMX: clear state ladder (main result)
Median core cycles of the timed op, timer overhead (~54) included.

| idle Δ | `dpbssd_st` (TDPBSSD+TILESTORED) | `dpbssd` | state |
|---|---|---|---|
| 0 – 316 ns | 133 | 88 | Warm |
| 562 ns – 1.78 µs | ~670 | ~630 | Cold 1 (~600) |
| 3.16 µs | ~7,660 (tsc 10.5k: core halts during wake) | ~7,650 | transition |
| 5.6 – 100 µs | ~5,690 | ~5,646 | Cold 2 (~6k) |
| ≥ 178 µs – 100 ms | **~9,370** | ~5,646 median, p90/p99 ≈ 9.3–9.5k | Cold 3 (~9k) |

- Matches GateBleed's 50 / 600 / 6k / 9k levels. The ~20k (Cold 4) state did not appear up to 100 ms → next run should sweep to 1–10 s.
- **Idle thresholds (new, unpublished in GateBleed):** Warm→C1 between 316–562 ns; C1→C2 between 1.78–3.16 µs; C2→C3 between 100–178 µs.
- `dpbssd` alone under-reports C3 (median stays ~5.6k): LFENCE likely doesn't wait for the full TMUL wake. Use `dpbssd_st` as the primary probe.
- `tileload` shows no wake penalty (~85 cyc flat): the load path isn't gated, the TMUL compute is.
- `amx_none` control: +42 cyc for Δ ≥ 100 µs (58 → 100), at the same threshold as C2→C3. Subtract the control per Δ.
- Consistency: `hold_state dpbssd` at Δ=1 µs runs 754k ops/s vs 968k for `none` → ~0.27 µs (~530 cyc) extra per op, which matches the Cold-1 penalty.

## AVX-512: no clean gating step; throttle/transition dominates
- 64-FMA block: ~314 cyc (512) / ~295 (256) for most Δ, fast (~95) only at Δ ≈ 1.78 µs. Same shape for 256-bit, so it's not 512-bit upper-lane gating.
- `f_idle` dips (≈1.36 GHz effective at Δ=3.16 µs) after fma512/fma256/AMX ops but not for the `none` control: the core still halts briefly after vector/AMX work **even with turbo off and frequency fixed** → voltage/power-license transition, not a frequency change.
- `license_timeline` (true halts = gap with < 50% of expected cycles): fma512 49/50, fma256 49/50, amx 49/50 reps, ~350–440 ns per halt, starting ~3–5 µs after the burst. **But the `none` control also shows 32/50**, unlike the turbo-on runs (2–5/50). Unexplained; needs a repeat before it's a claim.
- Next probe change: sustained FMA warm-up (~50–100 µs) before each idle, so the license is granted before the gap (cf. Downs' AVX-512 transition measurements).

## Power (hold_state, RAPL package-0): no signal yet
- Package ~102 W ± ~1 W across all ops and Δ (other users share the package); DRAM ~12 W.
- One TDPBSSD per loop iteration is too light a load to move package power; differences are within noise.
- Next: heavier sustained AMX load per state, longer runs (≥30 s), more interleaved repeats with `none`.
