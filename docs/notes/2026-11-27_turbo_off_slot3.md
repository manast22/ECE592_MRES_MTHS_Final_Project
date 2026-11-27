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
