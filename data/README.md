# data/

- `raw/phaseN/`: direct probe output, never edited by hand. Each run writes a
  sibling `*.meta.txt` containing `env/verify_env.sh` output + git commit hash.
- `processed/phaseN/`: outputs of `analysis/processing/` (aggregated, filtered).

File naming: `<unit>_<experiment>_<host>_<YYYYMMDD-HHMM>.csv`.
