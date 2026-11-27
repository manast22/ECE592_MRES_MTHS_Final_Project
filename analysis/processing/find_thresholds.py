"""Phase 1: latency states and idle-time thresholds from latency sweeps.

For each CSV (amx_probe / avx512_probe output):
  1. find the latency levels (states) across all samples (common.levels),
  2. assign every sample to its nearest level,
  3. per delta: fraction of samples in each state and the majority state,
  4. thresholds = consecutive deltas where the majority state changes; the
     threshold lies in (d_lo, d_hi], reported with its geometric midpoint.
     The fraction table shows states that never become the majority (e.g. a
     deep state reached by only some samples).

Usage:
  python analysis/processing/find_thresholds.py data/raw/slot_*/amx_dpbssd_st.csv ...
         [--out data/processed/phase1]
Writes <out>/<slot>__<file>_states.csv (delta, n, frac per state, majority)
and appends to <out>/thresholds.csv
  (source, from_state_cyc, to_state_cyc, penalty_cyc, d_lo_ns, d_hi_ns, d_mid_ns).
Penalty = level - level of the delta=0 (warm) state.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from common import classify, label_of, levels, per_delta, read_csv  # noqa: E402


def analyse(path, out, quiet=False):
    t = read_csv(path)
    cyc = t["cyc"]
    lv = levels(cyc)
    st = classify(cyc, lv)
    rows, maj = [], []
    for d, idx in per_delta(t):
        fr = np.bincount(st[idx], minlength=len(lv)) / len(idx)
        rows.append((d, len(idx), fr))
        maj.append(int(fr.argmax()))
    warm = lv[maj[0]]
    src = label_of(path)

    os.makedirs(out, exist_ok=True)
    fn = os.path.join(out, src.replace("/", "__") + "_states.csv")
    with open(fn, "w") as f:
        f.write("delta_ns,n," + ",".join("frac_%.0f" % l for l in lv) + ",majority_cyc\n")
        for (d, n, fr), m in zip(rows, maj):
            f.write("%.0f,%d,%s,%.0f\n" % (d, n, ",".join("%.3f" % x for x in fr), lv[m]))

    th = []
    for i in range(1, len(rows)):
        if maj[i] != maj[i - 1]:
            lo, hi = rows[i - 1][0], rows[i][0]
            mid = np.sqrt(lo * hi) if lo > 0 else hi / 2
            th.append((src, lv[maj[i - 1]], lv[maj[i]], lv[maj[i]] - warm, lo, hi, mid))
    tf = os.path.join(out, "thresholds.csv")
    new = not os.path.exists(tf)
    with open(tf, "a") as f:
        if new:
            f.write("source,from_state_cyc,to_state_cyc,penalty_cyc,d_lo_ns,d_hi_ns,d_mid_ns\n")
        for r in th:
            f.write("%s,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f\n" % r)

    if not quiet:
        print("## %s  (%d samples)" % (src, len(cyc)))
        print("   levels (median cyc): " + "  ".join("%.0f" % l for l in lv) + "   warm=%.0f" % warm)
        print("   %12s %5s  " % ("delta_ns", "n") + " ".join("%7.0f" % l for l in lv) + "   majority")
        for (d, n, fr), m in zip(rows, maj):
            print("   %12.0f %5d  " % (d, n) + " ".join("%6.0f%%" % (100 * x) for x in fr) + "   %.0f" % lv[m])
        for r in th:
            print("   threshold %6.0f -> %6.0f cyc (penalty %6.0f): idle in (%.0f, %.0f] ns, mid %.0f ns" % r[1:])
        print()
    return lv, th


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--out", default="data/processed/phase1")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    for p in a.csv:
        analyse(p, a.out, a.quiet)


if __name__ == "__main__":
    main()
