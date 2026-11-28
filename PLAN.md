# Plan of Action: Measuring Power-Gating Channels in AMX vs. Off-Core Accelerators

> Execution plan for the project described in `BACKGROUND.md`. Ordered by
> dependency and risk: we front-load the de-risking steps (hardware access) and
> the universally-available baseline, then extend to the harder off-core work.

## Test platform: `artemisia.ece.ncsu.edu` (surveyed 2026-11-22)

Full snapshot: `env/hostinfo_artemisia_2026-11-22.txt` (regenerate with `make hostinfo`).

### Hardware

| Item | Value |
|---|---|
| CPU | 2× **Intel Xeon Gold 5420+** (Sapphire Rapids, family 6 model 143 stepping 8), microcode `0x2b000685` (kernel flags "Old microcode") |
| Cores | 2 sockets × 28 cores × 2 SMT = 112 logical CPUs; CPUs **60, 68 offline** |
| NUMA | node0 = CPUs 0-27,56-83 (256 GB); node1 = CPUs 28-55,84-111 (256 GB). SMT sibling of CPU *n* is *n*+56 |
| Caches | L1d 48 KB/core, L2 2 MB/core, L3 52.5 MB/socket |
| Memory | 501 GB total; no static hugepages (THP active) |
| Frequency | Base/TSC **2.0 GHz** (`PLATFORM_INFO` max non-turbo ratio 20); min ratio 8 (800 MHz); turbo ratio limits 27-41 (`MSR 0x1AD = 0x1b1b1b1c1d232629`) |
| ISA (relevant) | `amx_tile amx_int8 amx_bf16`, `avx512f/bw/dq/vl/vnni/bf16/fp16/ifma/vbmi/vbmi2`, `enqcmd movdir64b movdiri`, `serialize waitpkg`, `constant_tsc nonstop_tsc tsc_known_freq` |

### On-die accelerators (all present and driver-bound)

| Unit | PCI (node0 / node1) | Driver | Config / state |
|---|---|---|---|
| **DSA** | `75:01.0` → `dsa0` / `f2:01.0` → `dsa2` | `idxd` | enabled; user WQs: `wq0.0` shared(64), `wq0.1` dedicated(64), `wq2.0` dedicated(64), `wq2.1` shared(64). PASID/SVM on (`intel_iommu=on,sm_on`) |
| **IAA** | `75:02.0` → `iax1` / `f2:02.0` → `iax3` | `idxd` | enabled; `wq1.1`, `wq3.0` shared(128) |
| **QAT** (401xx / 4xxx gen) | `76:00.0` / `f3:00.0` (PF) + 16 VFs each | PF `4xxx`, VFs `vfio-pci` | `state=up`, `cfg_services=sym;dc`, **`pm_idle_enabled=1`**; `qatmgr --policy=16` running; qatlib 25.08 (in-tree driver, no `/etc/4xxx_devX.conf`) |
| **DLB 2.0** | `78:00.0` / `f5:00.0` | `dlb2` (node0 only) | `/dev/dlb0` |
| idxd PMUs | `perf` PMUs `dsa0 dsa2 iax1 iax3 dlb0` | | can count device events via `perf stat -e dsa0/...` |

### Software / tools available
- Compilers: gcc 11.5 (`-mamx-tile -mamx-int8` OK), clang 21.1. cmake, make, git.
- Kernel 6.17.8 (elrepo), RHEL 9.8. AMX requires `arch_prctl(ARCH_REQ_XCOMP_PERM, XTILEDATA)` (verified working in `env/check_amx.c`).
- Perf/power: `perf` (5.14; `perf_event_paranoid=0`, so CPU-wide counting and `msr/aperf,mperf,tsc/` work), `turbostat` (no-MSR mode as non-root, still reports `Bzy_MHz`/`PkgWatt`), `cpupower`, `pqos` (RDT/CAT: `cat_l3 cat_l2 mba` present), `rdmsr` (**has `cap_sys_rawio`, so reads work as user**), `stress-ng`, `numactl`, `taskset`.
- RAPL: sysfs `intel-rapl:{0,1}` (package-0/1) + `:0:0` (dram), **world-readable**; perf `power/energy-pkg/`, `energy-ram/`, `energy-psys/`. **No `energy-cores` (PP0) domain on SPR**, so core-only power is not available and we report package deltas.
- Accelerator libs: `accel-config` 4.1.8 + `libaccel-config` headers, `linux/idxd.h`; qatlib 25.08 + `qatlib-devel` + samples (`cpa_sample_code`, `dc_stateless_sample`, `cipher_sample`, …); QATzip 1.3.1 (`qzip`, `qatzip-test`).
- Python 3.9.25: numpy 1.26.3, pandas 2.2.0, matplotlib 3.9.4, scipy 1.9.3 (numpy/scipy version warning). `ruptures` not installed, so use a venv (`requirements.txt`).

### Current system settings (as found, not changed by us)

| Setting | Value | Implication |
|---|---|---|
| `isolcpus` | `28-55,84-111` (all of socket 1) | Use node-1 cores for probes, **paired with node-1 devices `dsa2`/`iax3`/QAT `f3:00.0`**. Keep SMT sibling (+56) idle |
| `nohz_full` | not set | Timer tick still fires on test cores; filter outliers |
| cpufreq | `intel_cpufreq` (intel_pstate **passive**), HWP off, governor `performance`, range 0.8-2.0 GHz | |
| `no_turbo` | `1` in sysfs, **but turbo is effectively ON**: `PERF_CTL` requests ratio 20, `PERF_STATUS` reports ratio 27, `IA32_MISC_ENABLE[38]=0` | See measured frequencies below |
| cpuidle | `acpi_idle`, only `POLL` + `C1` exposed (no C6) | Core C6 can't confound AMX gating, which is good; `PKG_CST_CONFIG=0x5` |
| Other users | shared machine; other users have active sessions; CrashPlan backup (~75% of one node-0 CPU) | Run on isolated node 1; log `verify_env.sh` output with every dataset |

**Measured effective core frequency under sustained load on CPU 30** (`env/verify_env.sh`, APERF/MPERF × TSC):

| Instruction class | Effective freq | vs. TSC (2.0 GHz) |
|---|---|---|
| scalar | 2.70 GHz | +35% |
| AVX-512 FMA | 2.70 GHz | +35% |
| AMX `TDPBSSD` | **2.50 GHz** | +25% |

The **frequency license differs for AMX**. So until turbo is really disabled,
TSC-measured latencies mix gating with DVFS (the Hertzbleed confounder). Fix:
root runs `env/setup_env.sh` (sets `IA32_MISC_ENABLE[38]` and min=max=2.0 GHz).
Fallback without root: record `aperf/mperf` alongside every sample and report
latency in *core* cycles as well as TSC ticks.

### Access status for our account (non-root, no sudo)

| Resource | Status | Needed for | Ask admin for |
|---|---|---|---|
| AMX / AVX-512 | ✅ works | Phases 1-2 | none |
| RAPL (sysfs + perf) | ✅ readable | Phase 2 | none |
| `rdmsr` | ✅ (file caps) | freq/turbo checks | none |
| `wrmsr`, cpufreq writes, `cpupower` set | ❌ | real turbo-off + P-state pin | run `env/setup_env.sh` once per boot, or sudoers rule |
| `/dev/dsa/*`, `/dev/iax/*` | ❌ (`dsausers` group + per-user ACL) | Phase 3 DSA/IAA | add us to **`dsausers`** |
| QAT VFs `/dev/vfio/281-312` | ❌ (`qat` group) | Phase 3 QAT | add us to **`qat`** |
| QAT `pm_status` (`/sys/kernel/debug`, mode 0700) | ❌ | QAT ground truth | sudoers rule for a read-only `pm_status` logger, or debugfs read access |
| `/dev/dlb0` | ❌ (per-user ACL) | optional | ACL entry (low priority) |

## Phase 0 — Environment & de-risking (do first, week 1)

Goal: confirm what hardware we can actually measure before writing analysis code.

- [x] Identify test machine(s): CPU generation, Xeon SKU, which accelerators are
      enabled. Recorded in `env/hostinfo_artemisia_2026-11-22.txt` (see "Test platform" above).
- [x] Confirm AMX present **and executable** (`build/check_amx` → `TDPBSSD` result correct).
- [x] Check off-core availability: DSA ✅, IAA ✅, QAT ✅ (`pm_idle_enabled=1`),
      DLB ✅, all present and bound. **Blocked only on permissions** (table above).
- [ ] Get access: request `dsausers` + `qat` group membership and a `pm_status`
      read path from the admin.
- [ ] Decide off-core target. *Provisional:* **QAT** (hardware + PM present;
      `pm_status` gives ground truth) once `qat` group + `pm_status` access is
      granted; **DSA** as fallback (only needs `dsausers`; simpler descriptor
      path, perf PMU available). Record the final decision here.
- [ ] Lock down measurement environment (`env/setup_env.sh`, **needs root**):
  - [x] Core isolation: `isolcpus=28-55,84-111` already set. `nohz_full` not set (kernel param, admin).
  - [x] Really disable turbo (`IA32_MISC_ENABLE[38]`) and pin min=max=2.0 GHz
        (`env/setup_env.sh`, socket 0; `env/auto_restore.sh` timer restores).
  - [x] C-states: only POLL/C1 exposed by `acpi_idle`, so nothing deeper to restrict.
  - [x] Confirm with `env/verify_env.sh <cpu>` → all classes within 2% of TSC
        (PASS at 2000 MHz in turbo-off slots 1-2, 2026-11-27).

**Deliverable:** `env/` dir with host info, a reproducible setup script, and a
pre-flight verification script. If no
off-core hardware is available, the project still proceeds (AMX + AVX-512 only).

## Phase 1 — Baseline + AMX replication (on-core)

### Scripts / benchmarks
- [x] `bench/common/timer.h` — serialized cycle timer (`LFENCE; RDTSC; <op>; LFENCE;
      RDTSC`). Note SPR TSC tick ≈ 0.5 ns; record TSC frequency.
- [x] `bench/avx512_probe.c` — baseline: time one FMA after a controlled idle
      interval. Cheap, runs anywhere; validates the whole timing harness.
- [x] `bench/amx_probe.c` — `LDTILECFG` once, then loop: busy-wait/sleep for
      interval Δ → one `TDPBSSD` → record cycles. Sweep Δ from ~100 ns to ~10 ms,
      log-spaced, many repeats per Δ.
- [x] `scripts/run_slot.sh` (slots 1-2), `scripts/run_phase12.sh` (slot 3: gaps below) — drive the sweeps, write raw CSV.
- [x] Slot 3 gaps (`scripts/run_phase12.sh`): ~20k state (sleep idle, TILERELEASE,
      long sweeps), what decides the 5.7k vs 9.4k deep state (sibling / neighbour
      load + `bench/sys_logger.sh`), fine grids at each threshold, fma128 control,
      second core. → `docs/notes/2026-11-27_turbo_off_slot3.md`
- **Status:** ladder 135/682/7.7k/5.7k reproduced on 2 cores, thresholds
  extracted (316–365 ns, 1.78–3.16 µs, 3.16–5.6 µs); AVX gating is >128-bit
  only. Open: 9.4k state seen only in slot 1 (cause unknown); no ~20k state;
  policy adapts to usage density and to halt vs. spin (new 3.3k level).

### Data to collect → `data/phase1/`
- `amx_latency_vs_delta.csv`: columns `delta_ns, trial, cycles`.
- `avx512_latency_vs_delta.csv`: same schema.

### Analysis → `analysis/`
- [x] `analysis/plotting/plot_state_ladder.py`: latency vs. Δ, log-x. **Replicate** the 5-state
      AMX ladder (~50/600/6k/9k/20k cycles). Confirm AVX-512 shows a much
      smaller (~150-cycle) penalty.
- [x] `analysis/processing/find_thresholds.py`: detect the Δ values where latency jumps between
      states (step/change-point detection). **This is the novel extension —
      the idle-timeout thresholds GateBleed did not publish.**

**Exit criterion:** reproduced AMX ladder + extracted per-state idle thresholds.

## Phase 2 — Power characterization (on-core)

### Scripts / benchmarks
- [x] `bench/power_sampler.sh` — read RAPL
      (`perf stat -e power/energy-pkg/,power/energy-cores/ ...` or
      `/sys/class/powercap/intel-rapl/.../energy_uj`) at fixed sample rate.
- [x] `bench/hold_state.c` — drive the unit so it sits in a chosen power state
      (fixed reuse distance), sustained for N seconds while power is sampled.
      **Slots 1-2: no signal** (1 core vs. +-1 W socket drift).
- [x] `bench/power_ab.c` — fix: N cores in the same state, ABBA-alternating
      windows, paired A-B differences + null test; core-count scaling.
- [x] Slot 3 run: **null test failed** (−2.3 ± 0.7 W) — a 1 Hz, 2.7 W pulse
      from another tenant aliased onto the 1 s ABBA period. Fixed: 1 s windows
      + random ABBA/BAAB order (2-core null after fix: 0.16 ± 0.61 W).
- [x] Rerun Phase 2 only: `scripts/run_phase12.sh 10 p2` (17:34; null OK; first 8 runs valid, stale timer restored turbo at 17:44)
- [x] Finish Phase 2 leftovers: `scripts/run_phase12.sh 10 p2b` (19:51, verify PASS start+end; see slot-3 note)

### Data to collect → `data/phase2/`
- `amx_power_by_state.csv`: `state, pkg_watts, core_watts, ops_per_s`.
- Per-op energy derived from sustained-throughput runs.
- Record ambient controls: pinned freq, turbo off, core temp if available.

### Analysis
- [x] `power_vs_latency.py`: join Phase-1 latency states with Phase-2 power (prefers in-run latency from power_ab lat_out=).
      Reproduce the small power delta (~3.6 W warm→cold) against the large
      latency delta.
- [x] `compute_fom.py`: compute the leakage-efficiency FoM (AMX done; AVX needs `scripts/run_phase12.sh 10 p2c`)
      `= distinguishable states (bits) / power saved (W)` per unit.

**Exit criterion:** latency↔power table + FoM for AMX and AVX-512.

## Phase 3 — Off-core characterization (QAT or DSA)

> Highest-uncertainty phase; depends on Phase-0 hardware. The async model means
> raw timing is noisier — the method must separate wake penalty from
> submit/queue/DMA overhead.

### DSA path
- [ ] `bench/dsa_probe.c` — configure WQ via `accel-config`; submit a simple
      memcpy/memfill descriptor (ENQCMD shared WQ or MOVDIR64B dedicated WQ);
      poll completion record; time end-to-end. Vary inter-submission idle Δ.
- [ ] Decompose latency: measure steady-state (warm) submit→complete as the
      floor; attribute excess on first-after-idle op to wake.

### QAT path (preferred if available)
- [ ] `bench/qat_probe.c` — `qatlib` sample (compress or symmetric crypto);
      measure first-request-after-idle vs. steady state.
- [ ] `bench/qat_pm_logger.sh` — poll `pm_status` while probing; **correlate
      observed latency with the device-reported power state** (ground truth).
- [ ] Run each config with PM **enabled** vs. **disabled**
      (`/etc/4xxx_devX.conf`) to isolate the PM latency contribution.

### Data to collect → `data/phase3/`
- `<unit>_latency_vs_delta.csv` (+ decomposed warm floor).
- `qat_latency_vs_pmstate.csv` (latency joined to `pm_status`).
- `<unit>_power_by_state.csv` (RAPL package; note QAT may not appear in a
  dedicated RAPL domain — document what the domain actually covers).

### Analysis
- [ ] Does an off-core state ladder exist at all? How many states, what penalty?
- [ ] FoM for the off-core unit; place it on the same axis as AMX/AVX-512.
- [ ] Test the core hypothesis: bigger power saving + lower observability?

**Exit criterion:** one off-core unit characterized and placed on the FoM axis
against AMX, with the hypothesis supported or refuted by the numbers.

## Phase 4 — Covert-channel feasibility (target, not promised)

- [ ] `attack/cc_sender.c` / `attack/cc_receiver.c` — one process modulates the
      unit's power state, another probes latency to recover bits.
- [ ] Measure bandwidth (bps) and bit-error rate; compare AMX vs. the off-core
      unit vs. the AVX-512 baseline.
- [ ] Optional cross-core / cross-tenant variant if time and isolation allow.

**Data → `data/phase4/`:** `channel_capacity.csv` (`unit, bps, ber, noise_sigma`).

## Phase 5 — Residual-state probe (stretch)

- [ ] Test whether internal state (AMX tiles; off-core scratch/descriptor
      buffers) survives a gate→wake cycle, and whether a context switch /
      `TILERELEASE` resets observable residency.
- [ ] Frame strictly as "is residency reset across a domain switch, and is it
      observable?" — **not** as reading another context's data.

## Phase 6 — Write-up

- [ ] Consolidate figures: state ladders, idle thresholds, latency↔power, FoM
      comparison table, (if reached) channel capacity.
- [ ] Headline result: the on-core vs. off-core latency/power/observability
      tradeoff, quantified.
- [ ] Note mitigations and their measured power/perf cost where relevant.

---

## Dependency order (summary)

```
Phase 0 (env + hardware) ─┬─> Phase 1 (AMX ladder + thresholds) ─> Phase 2 (AMX power/FoM) ─┐
                          │                                                                 ├─> Phase 6 (write-up)
                          └─> Phase 3 (off-core) ──> Phase 4 (covert channel) ─> Phase 5 ───┘
```

- Phases 1–2 are the **must-hit core** and depend only on AMX (available
  anywhere). They form a complete project on their own.
- Phase 3 is the **comparative target** and depends on Phase-0 off-core
  hardware.
- Phases 4–5 are **stretch**; attempt only after 1–3 land.

## Repo layout

```
.
├── BACKGROUND.md / PLAN.md / README.md
├── Makefile             # builds bench/ attack/ env/ *.c → build/; `make verify`, `make hostinfo`
├── requirements.txt     # Python analysis deps (use a venv)
├── env/                 # collect_hostinfo.sh, verify_env.sh, setup_env.sh (root),
│                        # check_amx.c, freq_probe.c, hostinfo_<host>_<date>.txt
├── bench/               # probes, timers, power samplers
│   └── common/          # shared headers (timer.h, pinning helpers)
├── attack/              # covert-channel sender/receiver (Phase 4)
├── scripts/             # multi-step run orchestration (campaigns)
├── data/
│   ├── raw/phase{1..5}/        # untouched probe output + *.meta.txt
│   └── processed/phase{1..5}/  # cleaned / aggregated
├── analysis/
│   ├── processing/      # thresholds, state clustering, power join, FoM
│   └── plotting/        # figure generation only
├── figures/             # generated plots
├── results/             # summary tables for the write-up
├── docs/                # notes, write-up drafts
└── build/               # binaries (git-ignored)
```

Data paths in the phases above (`data/phaseN/`) map to `data/raw/phaseN/` for
probe output and `data/processed/phaseN/` for analysis output.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| No off-core hardware | Phases 1–2 (AMX) stand alone as a full project |
| Frequency scaling masquerades as gating | **Observed on artemisia**: sysfs `no_turbo=1` but cores run 2.5-2.7 GHz with an AMX-specific license. Root runs `setup_env.sh`; gate every run on `verify_env.sh`; log APERF/MPERF per sample as a fallback |
| No root / device-group access | Phases 1-2 need none. Request `dsausers`, `qat`, and a `pm_status` read path early (week 1) |
| Shared machine noise (other users, backup agent) | Use isolated node-1 cores + node-1 devices; idle SMT sibling; record `verify_env.sh` with each dataset; many repeats + robust stats |
| Off-core wake signal lost in async noise | Use QAT `pm_status` as ground truth; decompose steady-state floor from first-after-idle |
| QAT not in a dedicated RAPL domain | Document exactly what the measured power domain covers; use relative deltas |
| Scope creep into "reading leftover data" | Keep Phase 5 as a residency-reset question, framed honestly |
