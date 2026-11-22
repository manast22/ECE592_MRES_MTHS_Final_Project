# Power-Gating Side Channels: AMX vs. Off-Core Accelerators

ECE592 final project. Background in [BACKGROUND.md](BACKGROUND.md); plan, platform
specs and status in [PLAN.md](PLAN.md).

## Layout
```
env/        host info, environment verification & (root) setup scripts
bench/      probes, timers, power samplers  (bench/common = shared headers)
attack/     covert-channel sender/receiver (Phase 4)
data/       raw/phaseN (probe output) and processed/phaseN
analysis/   processing/ (stats, thresholds, FoM) and plotting/
figures/    generated plots
results/    summary tables (CSV/MD) feeding the write-up
scripts/    run orchestration (multi-step campaigns)
docs/       notes, write-up drafts
build/      compiled binaries (git-ignored)
```

## Quick start (artemisia)
```bash
make                      # builds env/ + bench/ + attack/ tools into build/
make verify CPU=30        # pre-flight: is frequency actually pinned?
make hostinfo             # snapshot config into env/
python3 -m venv .venv && . .venv/bin/activate && pip install -r requirements.txt
```
Run probes on the isolated socket-1 cores (`28-55,84-111`), e.g.
`taskset -c 30 build/amx_probe ...`, and keep the SMT sibling (`86`) idle.
