# analysis/

- `processing/`: raw → processed (outlier filtering, state clustering,
  threshold/change-point detection, latency↔power join, FoM). Reads
  `data/raw/`, writes `data/processed/` and `results/`.
- `plotting/`: processed → `figures/`. No computation beyond what is plotted.

Done (numpy + matplotlib only; Python 3.9+, runs on artemisia or the laptop):

- `processing/find_thresholds.py <sweep.csv>...` — latency levels (states),
  per-delta state fractions, idle thresholds → `data/processed/phase1/`.
- `plotting/plot_state_ladder.py <sweep.csv>... -o figures/x.png [--ref 50,600,6000,9000,20000]`
  — latency vs. idle, median + 10–90 % band.
- `processing/power_by_state.py <power_ab.csv>` — paired ABBA power
  differences, null test, core scaling → `data/processed/phase2/`.

Planned: `processing/power_vs_latency.py`, `processing/compute_fom.py`,
`plotting/plot_fom.py` (after slot 3 power data).
