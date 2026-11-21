# Plan of Action: Measuring Power-Gating Channels in AMX vs. Off-Core Accelerators

> Execution plan for the project described in `BACKGROUND.md`. Ordered by
> dependency and risk: we front-load the de-risking steps (hardware access) and
> the universally-available baseline, then extend to the harder off-core work.

## Phase 0 — Environment & de-risking (do first, week 1)

Goal: confirm what hardware we can actually measure before writing analysis code.

- [ ] Identify test machine(s): CPU generation, Xeon SKU, which accelerators are
      enabled. Record in `env/hostinfo.txt`.
- [ ] Confirm AMX present: `cat /proc/cpuinfo | grep -o 'amx[a-z_]*'`.
- [ ] Check off-core availability:
  - DSA: `accel-config list` (needs `idxd` driver). Is a device + WQ present?
  - QAT: `lspci | grep -i qat`; is `qat_4xxx` driver loaded; does
        `/sys/kernel/debug/qat_4xxx_*/pm_status` exist?
  - DLB: `lspci | grep -i 2710` / `libdlb` present?
- [ ] Decide off-core target: **QAT** if `pm_status` is available (ground truth),
      else **DSA** (availability). Record the decision and why.
- [ ] Lock down measurement environment (script `env/setup_env.sh`):
  - Pin core(s): `isolcpus`, `nohz_full` on the test core.
  - Disable turbo; fix P-state: `cpupower frequency-set -g performance` + set
    min=max freq.
  - Restrict C-states on the test core (`cpupower idle-set` / kernel param).
  - Confirm with `turbostat` that frequency is actually pinned under load.

**Deliverable:** `env/` dir with host info + a reproducible setup script. If no
off-core hardware is available, the project still proceeds (AMX + AVX-512 only).

## Phase 1 — Baseline + AMX replication (on-core)

### Scripts / benchmarks
- [ ] `bench/timer.h` — serialized cycle timer (`LFENCE; RDTSC; <op>; LFENCE;
      RDTSC`). Note SPR TSC tick ≈ 0.5 ns; record TSC frequency.
- [ ] `bench/avx512_probe.c` — baseline: time one FMA after a controlled idle
      interval. Cheap, runs anywhere; validates the whole timing harness.
- [ ] `bench/amx_probe.c` — `LDTILECFG` once, then loop: busy-wait/sleep for
      interval Δ → one `TDPBSSD` → record cycles. Sweep Δ from ~100 ns to ~10 ms,
      log-spaced, many repeats per Δ.
- [ ] `bench/run_reuse_sweep.sh` — drives the sweep, writes raw CSV.

### Data to collect → `data/phase1/`
- `amx_latency_vs_delta.csv`: columns `delta_ns, trial, cycles`.
- `avx512_latency_vs_delta.csv`: same schema.

### Analysis → `analysis/`
- [ ] `plot_state_ladder.py`: latency vs. Δ, log-x. **Replicate** the 5-state
      AMX ladder (~50/600/6k/9k/20k cycles). Confirm AVX-512 shows a much
      smaller (~150-cycle) penalty.
- [ ] `find_thresholds.py`: detect the Δ values where latency jumps between
      states (step/change-point detection). **This is the novel extension —
      the idle-timeout thresholds GateBleed did not publish.**

**Exit criterion:** reproduced AMX ladder + extracted per-state idle thresholds.

## Phase 2 — Power characterization (on-core)

### Scripts / benchmarks
- [ ] `bench/power_sampler.sh` — read RAPL
      (`perf stat -e power/energy-pkg/,power/energy-cores/ ...` or
      `/sys/class/powercap/intel-rapl/.../energy_uj`) at fixed sample rate.
- [ ] `bench/hold_state.c` — drive the unit so it sits in a chosen power state
      (fixed reuse distance), sustained for N seconds while power is sampled.

### Data to collect → `data/phase2/`
- `amx_power_by_state.csv`: `state, pkg_watts, core_watts, ops_per_s`.
- Per-op energy derived from sustained-throughput runs.
- Record ambient controls: pinned freq, turbo off, core temp if available.

### Analysis
- [ ] `power_vs_latency.py`: join Phase-1 latency states with Phase-2 power.
      Reproduce the small power delta (~3.6 W warm→cold) against the large
      latency delta.
- [ ] `compute_fom.py`: compute the leakage-efficiency FoM
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
├── BACKGROUND.md
├── PLAN.md
├── env/            # host info, setup/pinning scripts
├── bench/          # probes, timers, power samplers
├── attack/         # covert-channel sender/receiver (Phase 4)
├── data/           # raw CSVs, by phase
└── analysis/       # plotting + stats scripts, figures
```

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| No off-core hardware | Phases 1–2 (AMX) stand alone as a full project |
| Frequency scaling masquerades as gating | Pin freq, disable turbo, verify with `turbostat` before trusting any result |
| Off-core wake signal lost in async noise | Use QAT `pm_status` as ground truth; decompose steady-state floor from first-after-idle |
| QAT not in a dedicated RAPL domain | Document exactly what the measured power domain covers; use relative deltas |
| Scope creep into "reading leftover data" | Keep Phase 5 as a residency-reset question, framed honestly |
