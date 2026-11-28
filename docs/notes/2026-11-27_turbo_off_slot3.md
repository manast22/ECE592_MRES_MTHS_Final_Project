# Turbo-off slot 3 — 2026-11-27 17:04 EST (artemisia, CPU 10 + 12, 2.0 GHz)

Data: `data/raw/phase12_artemisia_20261127-1704/` (`scripts/run_phase12.sh`).
verify_env PASS at start and end. Figures: `figures/amx_ladder_slot3.png`,
`figures/avx_widths_slot3.png`. Thresholds: `data/processed/phase1/thresholds.csv`
(from `analysis/processing/find_thresholds.py`).

## Phase 1 results

### AVX: width control settles it (P1a)
| idle | fma512 | fma256 | **fma128** |
|---|---:|---:|---:|
| 0 – 1.78 µs | 120 | 95 | 94 |
| 3.16 µs – 1.78 ms | 239 | 214–223 | 94 |
| ≥ 3.16 ms | 312 | 301 | 94 |

128-bit ops never pay a penalty; 256- and 512-bit pay the same one at the same
idle times (1.78–3.16 µs, then 1.78–3.16 ms). So the gated part is the vector
datapath **above 128 bits** — consistent with NetSpectre's AVX2 upper-half
gating — and there is no separate 512-bit-only gate. CPU 12 reproduces
(123 / 239 / 344).

### AMX: reproducible ladder, three new facts
Spin idle, CPU 10 (CPU 12 within 2 %): **135 → 682** at 316–562 ns
(fine grid: **316–365 ns**), **→ 7.7k** at 1.78–3.16 µs (transient),
**→ 5.7k** from 3.16–5.6 µs to 10 s. Penalties ≈ 550 / 7.5k / 5.6k.

1. **Deep 9.4k state absent all day** after slot 1 (11:06). Not caused by load
   on the SMT sibling, 4 neighbour cores spinning, or 4 neighbour cores running
   AMX (P1b: all 8 runs 0 % deep samples), not by the previous sample's idle,
   not by time since process start. Package C-state residency is 0 on this
   machine. Cause still unknown; something machine-wide differed at 11:06.
2. **No ~20k state** in any variant up to 10 s idle (spin, sleep, TILERELEASE).
3. **The policy depends on how the core idles and on usage density:**
   * *Sleep* (core halts in C1, actual idle ≥ 4.8 µs): **~730 up to 0.57 ms**,
     **3.3k from 1 ms**, 5.7k at ≥ 0.18 s. Halting the core keeps AMX in a
     shallower state far longer than spinning (5.7k already at 5.6 µs).
   * *Dense sweeps* (all idles ≤ 10 µs, or 30 µs–1 ms): **~720 up to 10 µs**
     (no 7.7k / 5.7k), and **3.3k** for 30 µs–1 ms instead of 5.7k. Not the
     previous sample's idle (checked by t_ns order) — so the controller
     adapts to usage over a longer window. A new level, 3.3k, appears only
     in these modes.
   * *TILERELEASE before idle* (op=full, timed LDTILECFG+loads+op+store):
     warm 362, same steps shifted (178–316 ns → 875; then 7.6k; 5.9k) and
     more 9.5k tail samples (p90). Releasing tiles is not what unlocks 20k.

## Phase 2: power_ab — null test FAILED, method fixed

`null` (same state in A and B): **−2.32 ± 0.69 W** (should be 0). Cause found:
artemisia's package power has a **2.7 W pulse at exactly 1.00 Hz**, locked to
the wall-clock second (another tenant; 30 s RAPL trace, FFT + fold). The
fixed ABBA pattern with 250 ms windows also repeats every 1 s, so the pulse
projected entirely onto A−B; every slot-3 difference carries an unknown
±2.5 W offset (strong lag-4 autocorrelation in every run). **Slot-3 power
numbers are not usable.**

Fix (`bench/power_ab.c`): 1 s windows (integrate one whole pulse) + random
ABBA/BAAB block order + wall-clock t_ns per window. Post-fix null on 2 cores,
turbo on: **0.16 ± 0.61 W** (OK). Rerun: `scripts/run_phase12.sh 10 p2`
(~16 min, Phase 2 only).

## Phase 2 rerun — 17:34 (`data/raw/phase12_artemisia_20261127-1734_p2/`)

Fixed power_ab (1 s windows, random ABBA/BAAB, 16 blocks, 15 cores 2-9,11-17).
**The stale 17:04 auto-restore timer fired at 17:44:09 and re-enabled turbo
mid-run** (verify_end FAIL). Valid runs = those that finished before 17:44:09
(first 8). `avx256_sat` (17:43:21) straddles the switch; it and everything
after it (`avx512_20us`, `amx_sat_n1/n4/n8`) is excluded.
Summary: `data/processed/phase2/power_by_state.csv`
(`analysis/processing/power_by_state.py`).

| run (A vs B, 15 cores) | A−B (W, 95 % CI) | per core (W) |
|---|---:|---:|
| null (spin vs spin) | +0.46 ± 0.81 | **OK, consistent with 0** |
| AMX "saturated" 64-burst vs spin | +1.15 ± 0.91 | 0.08 ± 0.06 |
| AMX every 300 ns (warm) vs spin | **+2.96 ± 0.63** | **0.20 ± 0.04** |
| AMX every 1 µs (~680 state) vs spin | +1.56 ± 1.32 | 0.10 ± 0.09 |
| AMX every 20 µs (5.7k gated) vs spin | **+1.65 ± 0.62** | **0.11 ± 0.04** |
| AMX every 1 ms vs spin | +0.85 ± 1.53 | n.s. |
| AMX 20 µs vs AMX 300 ns (gated − warm) | −1.28 ± 1.10 | −0.09 ± 0.07 |
| AVX-512 saturated vs spin | **+4.66 ± 1.27** | **0.31 ± 0.08** |

Reading:
- The method now works (null passes) and resolves ~0.05 W/core.
- Keeping AMX warm (op every 300 ns) costs ~0.2 W/core over a scalar spin;
  letting it gate between ops (20 µs) roughly halves that; gated−warm
  ≈ −0.09 W/core (CI just touches 0). At 2.0 GHz fixed, this is the
  per-core power-gating saving — small, as GateBleed found (3.6 W was
  package-level at full turbo).
- The "saturated" AMX run **is** saturated: 1.2e8 ops/s at 2.0 GHz = 1
  TDPBSSD per ~16 cycles, the unit's throughput limit (16x16x64 MACs at
  1024 MACs/cycle). Checked after the slot: 4 independent accumulators
  (tiles 0,3,4,5) give the same 1.44e8 ops/s/core as one chained tile (turbo
  on). So AMX compute alone really draws only ~0.08 W/core more than a spin
  at 2.0 GHz; operand loads from memory are not included.
- Still to do: rerun the excluded runs (avx256_sat, avx512_20us, core-count
  scaling n=1/4/8): `scripts/run_phase12.sh 10 p2b` (~7 min).

## Phase 2 leftovers — slot 4, 19:51 (`data/raw/phase12_artemisia_20261127-1951_p2b/`)

`scripts/run_phase12.sh 10 p2b`. verify_env PASS at start **and end**; the
new auto_restore token check means no stale timer could fire. Summary:
`data/processed/phase2/power_by_state_p2b.csv`.

| run (A vs B) | n | A−B (W, 95 % CI) | per core (W) |
|---|---:|---:|---:|
| null | 15 | −0.17 ± 0.69 | **OK** |
| AVX-256 saturated vs spin | 15 | **+3.09 ± 0.66** | **0.21 ± 0.04** |
| AVX-512 one op every 20 µs vs spin | 15 | **+3.04 ± 0.55** | **0.20 ± 0.04** |
| AMX saturated vs spin | 1 | +0.61 ± 0.93 | n.s. |
| AMX saturated vs spin | 4 | +1.86 ± 0.65 | 0.47 ± 0.16 |
| AMX saturated vs spin | 8 | +2.76 ± 1.19 | 0.35 ± 0.15 |

Reading:
- AVX-256 saturated (0.21 W/core) < AVX-512 saturated (0.31, 17:34 run): wider
  datapath costs more, as expected.
- **Sparse AVX-512 (1 op / 20 µs, ~5e4 ops/s) costs as much as saturated
  AVX-256.** The upper datapath is powered between ops (the 20 µs gap is past
  the ~3 µs timing step, so the op pays the wake-up but the power is not
  saved). Contrast AMX: sparse (20 µs) costs ~0.11 W/core, about half its
  warm cost. Candidate explanation: the AVX timing step is a voltage/clock
  re-ramp rather than a true power gate — needs a check.
- AMX core scaling, n = 1/4/8 in this run: **0.31 ± 0.22 W/core** (linear
  fit). This is higher than the 17:34 15-core run (0.08 W/core); adding that
  point gives a slope of ~0. The two runs were 2 h apart with different
  background power (B ≈ 110 W now vs ~117 W), so **only compare within one
  run**. Within-run AMX saturated power is therefore ~0.1–0.35 W/core;
  report it as a range.

## Analysis scripts + in-run latency (20:00-20:15)

- `analysis/processing/power_vs_latency.py` -> `power_vs_latency.csv`,
  `gating_summary.csv`, `figures/power_vs_latency.png`.
- `analysis/processing/compute_fom.py` -> `fom.csv`. AMX (current data):
  2 bits (4 levels), capacity 1.88 bits/probe, saving 0.086 +- 0.073 W/core
  -> FoM ~22 bits/W (>= 12). AVX: no matched warm/gated power pair yet.
- `bench/power_ab.c lat_out=` now times every op of burst-1 states inside
  the power windows. **Smoke test (turbo ON, 20:11, preliminary):** AMX
  every 20 us runs at **~636 ticks (shallow ~680 state), not 5.7k**; AVX-512
  /256 every 20 us show **no penalty** (70 / 66 ticks, same as every 300 ns).
  Periodic use keeps the units out of deep gating, so the "gated" power so far
  is really the shallow state. Confirm turbo-off: `scripts/run_phase12.sh 10 p2c`.

## Checking candidate explanations for 9.4k / no 20k (20:40)

Read as user on artemisia (`rdmsr` has cap_sys_rawio):
EPB (0x1B0) = 6 on all cores; MSR_PKG_CST_CONFIG_CONTROL (0xE2) = 0x5;
POWER_CTL (0x1FC) = 0x1600ac0059 (C1E off); uncore ratio limit (0x620) =
0x1616 -> **uncore pinned 2.2 GHz** (current 0x621 = 2.2 GHz); idle driver
`acpi_idle`, states POLL + C1 only (intel_idle not in use); microcode
0x2b000685; BIOS Lenovo ESE136G-3.70 (2025-09-04); kernel 6.17.8 (not 5.14);
**no reboot since 2026-10-26**; tuned inactive, profile file unchanged since boot.

| guess | verdict |
|---|---|
| BIOS/EPB power profile | Plausible for no-20k (C1-only + pinned uncore look like a performance profile). Not the 11:06 change: no reboot, no tuned, EPB 6 everywhere. Testable: EPB 15 on CPU 10 (root) |
| Microcode/BIOS version | Not the 11:06 change (no reboot). Vs GateBleed: need their version |
| Other tenants | Weak: package power 102.6 W (11:06) vs 102.5 W (15:18) |
| Sleep can't reach C6 | Confirmed: acpi_idle offers only C1. Best explanation for no 20k |
| Usage history | Not for 20k: long sweeps already had 0.17-10 s between ops |
| Timer tick | Not the 9.4k trigger: slot-1 9.4k share 2 % -> 45 % at 100 us, plateau 80-85 %; a 1 kHz tick would give 10 % at 100 us and 100 % at >= 1 ms |

run_phase12.sh now logs these MSRs, idle states, microcode and boot time in meta.txt.

## EPB test (20:50, turbo ON — setup_env was not run)

`data/raw/epb15/` (EPB 15 on CPU 10 + 66, set by hand, then back to 6) vs
`data/raw/epb6_turbo/` (EPB 6 control, same commands right after).
Both: one deep level only, **~6160 TSC ticks (3.08 us) = ~7920 core cycles at
~2.69 GHz**, for every idle from 10 us to 10 s; the same median within 0.5 %;
the same idle frequency. No 9.4k-equivalent (~4.7 us) or 20k-equivalent
(~10 us) level. **EPB does not change AMX gating -> ruled out** (strictly,
with turbo on). Side note: in wall time the wake-up is nearly the same as at
2.0 GHz fixed (5.7k cycles = 2.85 us), so it scales with time, not cycles.
