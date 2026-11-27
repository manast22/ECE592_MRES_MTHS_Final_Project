# bench/

Timing probes and power samplers. Each `*.c` here is built to `build/<name>` by `make`.

| File (planned) | Phase | Purpose |
|---|---|---|
| `common/timer.h` | 1 | ✅ `LFENCE;RDTSC;RDPMC;LFENCE` stamps (TSC + core cycles), pinning, spin-wait, percentiles |
| `timer_test.c` | 1 | ✅ Timer validation: overhead + 1000-IMUL chain (expect 3000 core cycles) |
| `avx512_probe.c` | 1 | ✅ 64-FMA block (`op=fma512/fma256/none`) after idle Δ; reports idle-window core MHz |
| `license_timeline.c` | 1 | ✅ Diagnostic: detects core clock halts after a vector burst (`op=fma512/fma256/amx/none`) |
| `common/amx.h` | 1 | ✅ AMX permission + tile config + self-test |
| `common/args.h` | 1 | ✅ `key=val` argument parsing |
| `amx_probe.c` | 1 | ✅ One AMX op (`op=dpbssd/dpbssd_st/tileload/none`) after idle Δ = 0, 100 ns … 100 ms (4/decade) |
| `../scripts/run_slot.sh` | 1-2 | ✅ Runs every probe for one turbo-off slot → `data/raw/slot_<host>_<stamp>/` |
| `hold_state.c` | 2 | ✅ Holds a unit at fixed Δ for N s; reports ops/s + RAPL package/DRAM W |
| `power_sampler.sh` | 2 | ✅ RAPL energy time series (sysfs `energy_uj`) |
| `dsa_probe.c` | 3 | DSA submit→complete latency vs. idle Δ (`/dev/dsa/wqX.Y`) |
| `qat_probe.c` | 3 | QAT first-after-idle vs. steady state (qatlib) |
| `qat_pm_logger.sh` | 3 | Polls `pm_status` (needs root/debugfs) |

Latency-sweep CSV schema: `unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu`
(`tsc` = TSC ticks, `cyc` = core cycles via RDPMC; raw, timer overhead not subtracted).
