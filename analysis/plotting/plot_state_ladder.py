"""Phase 1 figure: latency vs. idle time (log-log), one line per sweep.

Line = median core cycles per delta; band = 10th-90th percentile; dots =
fraction of samples above the warm level are not drawn (see
find_thresholds.py for state fractions). Sleep sweeps use measured idle_ns.

Usage:
  python analysis/plotting/plot_state_ladder.py data/raw/slot_*/amx_dpbssd_st.csv ... \
         -o figures/amx_ladder.png [--title "..."] [--ref 50,600,6000,9000,20000]
--ref draws horizontal reference lines (e.g. GateBleed's reported levels).
"""
import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "processing"))
from common import label_of, per_delta, read_csv, x_of  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--title", default="")
    ap.add_argument("--ref", default="", help="comma-separated reference levels (cycles)")
    a = ap.parse_args()

    fig, ax = plt.subplots(figsize=(8, 5))
    for p in a.csv:
        t = read_csv(p)
        x = x_of(t)
        xs, med, lo, hi = [], [], [], []
        for d, idx in per_delta(t):
            xv = np.median(x[idx])
            xs.append(max(xv, 30.0))        # delta 0 drawn at 30 ns on the log axis
            c = t["cyc"][idx]
            med.append(np.median(c)); lo.append(np.percentile(c, 10)); hi.append(np.percentile(c, 90))
        o = np.argsort(xs)
        xs, med, lo, hi = (np.asarray(v)[o] for v in (xs, med, lo, hi))
        line, = ax.plot(xs, med, marker="o", ms=3, label=label_of(p))
        ax.fill_between(xs, lo, hi, color=line.get_color(), alpha=0.15, lw=0)
    for r in filter(None, a.ref.split(",")):
        ax.axhline(float(r), color="grey", ls=":", lw=1)
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("idle before op (ns)   [delta 0 plotted at 30 ns]")
    ax.set_ylabel("core cycles (median, 10-90% band; incl. ~54 timer)")
    ax.set_title(a.title)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=7)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    fig.tight_layout(); fig.savefig(a.out, dpi=150)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
