# analysis/

- `processing/`: raw → processed (outlier filtering, state clustering,
  threshold/change-point detection, latency↔power join, FoM). Reads
  `data/raw/`, writes `data/processed/` and `results/`.
- `plotting/`: processed → `figures/`. No computation beyond what is plotted.

Planned: `processing/find_thresholds.py`, `processing/power_vs_latency.py`,
`processing/compute_fom.py`, `plotting/plot_state_ladder.py`,
`plotting/plot_fom.py`.
