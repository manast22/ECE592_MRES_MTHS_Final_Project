"""Leakage-efficiency figure of merit (PLAN.md Phase 2):

    FoM = information leaked by the latency states (bits) / power saved by gating (W per core)

High FoM = the unit leaks a lot for the little power its gating saves.

Bits, two ways, from a Phase-1 states CSV (find_thresholds.py output: per
delta, the fraction of samples in each latency level):
  * levels_bits = log2(K), K = distinct majority levels across the sweep
    (PLAN's "distinguishable states");
  * capacity_bits = capacity of the one-probe channel "sender picks the idle
    time, receiver times one op" (Blahut-Arimoto over the swept deltas, with
    P(level | delta) from the measured fractions). <= levels_bits; it accounts
    for noise/overlap between levels.
Power saved: gating_summary.csv from power_vs_latency.py (warm = op every
300 ns, gated = every 20 us, per core).
If the saving's 95 % CI includes 0, the FoM has no upper bound; the lower
bound bits / (saving + ci) is still reported.

Usage:
  python analysis/processing/compute_fom.py [--gating data/processed/phase2/gating_summary.csv]
Writes data/processed/phase2/fom.csv.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from common import read_csv  # noqa: E402
from power_vs_latency import SWEEP  # noqa: E402


def channel(path):
    """(K, levels_bits, capacity_bits) from a states CSV."""
    t = read_csv(path)
    fc = [c for c in t if c.startswith("frac_")]
    P = np.vstack([t[c] for c in fc]).T            # rows: delta, cols: level
    P = P / P.sum(axis=1, keepdims=True)
    K = len(np.unique(t["majority_cyc"]))
    # Blahut-Arimoto
    p = np.full(len(P), 1.0 / len(P))
    for _ in range(2000):
        q = p @ P
        with np.errstate(divide="ignore", invalid="ignore"):
            D = np.nansum(np.where(P > 0, P * np.log2(P / q), 0.0), axis=1)
        p_new = p * np.exp2(D); p_new /= p_new.sum()
        if np.abs(p_new - p).max() < 1e-10:
            p = p_new; break
        p = p_new
    q = p @ P
    with np.errstate(divide="ignore", invalid="ignore"):
        C = float(np.nansum(p[:, None] * np.where(P > 0, P * np.log2(P / q), 0.0)))
    return K, np.log2(K), C


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gating", default="data/processed/phase2/gating_summary.csv")
    ap.add_argument("--out", default="data/processed/phase2/fom.csv")
    a = ap.parse_args()
    g = read_csv(a.gating)
    rows = []
    hdr = "%-8s %2s %6s %6s %10s %16s %12s %12s  %s"
    print(hdr % ("unit", "K", "log2K", "C", "penalty", "saved W/core", "FoM log2K/W", "FoM C/W", "note"))
    for i, op in enumerate(g["op"]):
        unit = g["unit"][i]
        K, lb, C = channel(SWEEP[op]) if os.path.exists(SWEEP.get(op, "")) else (0, np.nan, np.nan)
        sv = float(g["saving_w"][i]) if "saving_w" in g and str(g["saving_w"][i]) not in ("", "nan") else np.nan
        ci = float(g["saving_ci"][i]) if np.isfinite(sv) else np.nan
        pen = float(g["penalty_cyc"][i]) if np.isfinite(sv) else np.nan
        if not np.isfinite(sv):
            note = "power saving not measured yet: " + g["method"][i]
            f_l = f_c = lo_l = lo_c = np.nan
        else:
            f_l, f_c = lb / sv, C / sv
            lo_l, lo_c = lb / (sv + ci), C / (sv + ci)
            note = ("saving CI includes 0 -> FoM >= lower bound only" if sv - ci <= 0
                    else "FoM range %.0f-%.0f bits/W (capacity)" % (C / (sv + ci), C / (sv - ci)))
        rows.append((unit, op, K, lb, C, pen, sv, ci, f_l, f_c, lo_l, lo_c, note))
        print(hdr % (unit, K, "%.2f" % lb, "%.2f" % C, "%.0f" % pen if np.isfinite(pen) else "-",
                     "%.3f+-%.3f" % (sv, ci) if np.isfinite(sv) else "-",
                     "%.1f (>=%.1f)" % (f_l, lo_l) if np.isfinite(f_l) else "-",
                     "%.1f (>=%.1f)" % (f_c, lo_c) if np.isfinite(f_c) else "-", note))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as f:
        f.write("unit,op,n_levels,levels_bits,capacity_bits,penalty_cyc,saving_w_per_core,saving_ci95,"
                "fom_levels_bits_per_w,fom_capacity_bits_per_w,fom_levels_lower,fom_capacity_lower,note\n")
        for r in rows:
            f.write("%s,%s,%d,%.3f,%.3f,%.0f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,\"%s\"\n" % r)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
