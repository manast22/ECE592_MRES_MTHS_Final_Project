# bench/

Timing probes and power samplers. Each `*.c` here is built to `build/<name>` by `make`.

| File (planned) | Phase | Purpose |
|---|---|---|
| `common/timer.h` | 1 | Serialized TSC timer (`LFENCE;RDTSC … RDTSCP;LFENCE`), pinning helpers |
| `avx512_probe.c` | 1 | Baseline: one FMA after controlled idle Δ |
| `amx_probe.c` | 1 | One `TDPBSSD` after idle Δ (reuse-distance sweep) |
| `run_reuse_sweep.sh` | 1 | Drives Δ sweep → `data/raw/phase1/*.csv` |
| `hold_state.c` | 2 | Holds a unit at fixed reuse distance while power is sampled |
| `power_sampler.sh` | 2 | RAPL sampler (sysfs `energy_uj` / `perf power/energy-pkg/`) |
| `dsa_probe.c` | 3 | DSA submit→complete latency vs. idle Δ (`/dev/dsa/wqX.Y`) |
| `qat_probe.c` | 3 | QAT first-after-idle vs. steady state (qatlib) |
| `qat_pm_logger.sh` | 3 | Polls `pm_status` (needs root/debugfs) |

Latency-sweep CSV schema: `unit,delta_ns,trial,cycles,tsc_mhz,cpu`.
