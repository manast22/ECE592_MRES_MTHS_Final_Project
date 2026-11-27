"""Phase 2: power per unit state from bench/power_ab.c windows.

For each label: drop the first `skip` windows, pair windows in ABBA blocks
(diff = (A0 + A3 - B1 - B2) / 2), report mean A-B with a 95% CI, the
per-core value (divide by n_cores), and whether the null test (A = B) is
consistent with 0. A scaling fit (W vs. n_cores for the amx_sat_n* labels
plus amx_sat) checks that the difference grows linearly with cores.

Usage:
  python analysis/processing/power_by_state.py data/raw/phase12_*/power_ab.csv \
         [--skip 4] [--out data/processed/phase2/power_by_state.csv]
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from common import read_csv  # noqa: E402


def blocks(pw, st, skip):
    """Per 4-window block: (sum A - sum B) / 2. Works for fixed ABBA (slot 3)
    and for the random ABBA/BAAB order (power_ab after slot 3)."""
    k0 = (skip + 3) // 4 * 4
    sgn = np.where(st == "A", 1.0, -1.0)
    return np.array([(sgn[k:k + 4] * pw[k:k + 4]).sum() / 2 for k in range(k0, len(pw) - 3, 4)])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--skip", type=int, default=4)
    ap.add_argument("--out", default="data/processed/phase2/power_by_state.csv")
    a = ap.parse_args()
    t = read_csv(a.csv)
    labels = list(dict.fromkeys(t["label"]))   # keep run order
    res = []
    print("%-20s %3s %-18s %-18s %8s %8s %8s %8s %4s" % ("label", "n", "A", "B", "A_W", "B_W", "A-B_W", "ci95", "blk"))
    for L in labels:
        m = t["label"] == L
        win = t["win"][m].astype(int); o = np.argsort(win)
        pw = t["pkg_w"][m][o]; st = t["state"][m][o]
        n = int(t["n_cores"][m][0]); A = t["a"][m][0]; B = t["b"][m][0]
        d = blocks(pw, st, a.skip)
        mu = d.mean() if len(d) else np.nan
        ci = 1.96 * d.std(ddof=1) / np.sqrt(len(d)) if len(d) > 1 else np.nan
        keep = np.arange(len(pw)) >= a.skip
        aw = pw[keep & (st == "A")].mean(); bw = pw[keep & (st == "B")].mean()
        res.append((L, n, A, B, aw, bw, mu, ci, mu / n, ci / n, len(d)))
        print("%-20s %3d %-18s %-18s %8.2f %8.2f %8.3f %8.3f %4d   per core %.3f +- %.3f W"
              % (L, n, A, B, aw, bw, mu, ci, len(d), mu / n, ci / n))
    for r in res:
        if r[0] == "null":
            ok = abs(r[6]) <= r[7]
            print("\nnull test: %.3f +- %.3f W -> %s" % (r[6], r[7], "OK (consistent with 0)" if ok else "FAILS: drift/bias larger than CI"))
    sc = [(r[1], r[6]) for r in res if r[0].startswith("amx_sat")]
    if len(sc) >= 3:
        n, w = np.array(sc, float).T
        slope, icpt = np.polyfit(n, w, 1)
        print("amx_sat scaling: %.3f W/core (intercept %.2f W) from n = %s" % (slope, icpt, ",".join("%d" % x for x in n)))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as f:
        f.write("label,n_cores,a,b,a_w,b_w,diff_w,ci95_w,diff_w_per_core,ci95_w_per_core,blocks\n")
        for r in res:
            f.write("%s,%d,%s,%s,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%d\n" % r)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
