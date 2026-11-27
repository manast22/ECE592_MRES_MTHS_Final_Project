# bench/

Timing probes and power samplers. Each `*.c` here is built to `build/<name>` by `make`.

| File (planned) | Phase | Purpose |
|---|---|---|
| `common/timer.h` | 1 | ✅ `LFENCE;RDTSC;RDPMC;LFENCE` stamps (TSC + core cycles), pinning, spin-wait, percentiles |
| `timer_test.c` | 1 | ✅ Timer validation: overhead + 1000-IMUL chain (expect 3000 core cycles) |
| `avx512_probe.c` | 1 | ✅ 64-FMA block (`op=fma512/fma256/none`) after idle Δ; reports idle-window core MHz |
| `license_timeline.c` | 1 | ✅ Diagnostic: detects core clock halts after a vector burst (turbo license transitions) |
| `amx_probe.c` | 1 | One `TDPBSSD` after idle Δ (reuse-distance sweep) |
| `run_reuse_sweep.sh` | 1 | Drives Δ sweep → `data/raw/phase1/*.csv` |
| `hold_state.c` | 2 | Holds a unit at fixed reuse distance while power is sampled |
| `power_sampler.sh` | 2 | RAPL sampler (sysfs `energy_uj` / `perf power/energy-pkg/`) |
| `dsa_probe.c` | 3 | DSA submit→complete latency vs. idle Δ (`/dev/dsa/wqX.Y`) |
| `qat_probe.c` | 3 | QAT first-after-idle vs. steady state (qatlib) |
| `qat_pm_logger.sh` | 3 | Polls `pm_status` (needs root/debugfs) |

Latency-sweep CSV schema: `unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu`
(`tsc` = TSC ticks, `cyc` = core cycles via RDPMC; raw, timer overhead not subtracted).
