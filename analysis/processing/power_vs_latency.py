"""Phase 2: join each power_ab run with the latency state the unit is in.

For every run in the given power_ab.csv files:
  * power: per-block A-B difference (same method as power_by_state.py), per core;
  * latency of the op in state A and B:
      - in-run (preferred): median of every op timed inside the power windows
        (power_lat.csv next to power_ab.csv; written by power_ab lat_out=,
        slot 5 onward). Dense, steady usage changes the AMX policy (slot 3),
        so the Phase-1 sweep is only a proxy;
      - sweep (fallback): majority latency level at that delta in a Phase-1
        states CSV (find_thresholds.py output).
Then, per unit, the gating summary that compute_fom.py needs:
  warm = op every 300 ns, gated = op every 20 us (both burst 1)
  saving  = P(warm) - P(gated) per core
  penalty = latency(gated) - latency(warm) cycles
Sources for the saving, best first: a direct run (A = gated, B = warm), else
the difference of the two "vs none" runs from the SAME session (background
power differs between sessions by several W: slot 4).

Invalid runs (stale-timer turbo restore, 17:34) are dropped by --drop.

Usage:
  python analysis/processing/power_vs_latency.py data/raw/phase12_*/power_ab.csv \
      [--fig figures/power_vs_latency.png]
Writes data/processed/phase2/power_vs_latency.csv and gating_summary.csv.
"""
import argparse
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from common import read_csv  # noqa: E402
from power_by_state import blocks  # noqa: E402

P1 = "data/processed/phase1/phase12_artemisia_20261127-1704__"
SWEEP = {"dpbssd": P1 + "amx_cond_quiet_r1_states.csv",
         "fma512": P1 + "avx_fma512_states.csv",
         "fma256": P1 + "avx_fma256_states.csv"}
UNIT = {"dpbssd": "AMX", "fma512": "AVX-512", "fma256": "AVX-256"}
WARM, GATED = 300, 20000
# 17:04 (slot 3): 1 Hz pulse aliased onto fixed ABBA -> whole session invalid.
# 17:34 rerun: turbo came back at 17:44:09 (stale timer) -> these are invalid
DROP = ("phase12_artemisia_20261127-1704:*;"
        "phase12_artemisia_20261127-1734_p2:avx256_sat,avx512_20us,amx_sat_n1,amx_sat_n4,amx_sat_n8")


def spec(s):
    op, d, b = s.split(":")
    return op, float(d), int(b)


def sweep_lat(op, d, cache={}):
    """Majority latency level at the nearest swept delta (log scale)."""
    if op not in SWEEP or not os.path.exists(SWEEP[op]):
        return np.nan
    if op not in cache:
        cache[op] = read_csv(SWEEP[op])
    t = cache[op]
    x = t["delta_ns"]
    i = np.argmin(np.abs(np.log10(np.maximum(x, 1)) - np.log10(max(d, 1))))
    return float(t["majority_cyc"][i])


def load_runs(paths, drop, skip):
    runs = []
    for p in paths:
        sess = os.path.basename(os.path.dirname(os.path.abspath(p)))
        t = read_csv(p)
        lat = {}
        lp = os.path.join(os.path.dirname(p), "power_lat.csv")
        if os.path.exists(lp):
            lt = read_csv(lp)
            for L, st, p50 in zip(lt["label"], lt["state"], lt["p50"]):
                lat[(L, st)] = float(p50)
        for L in dict.fromkeys(t["label"]):
            if L in drop.get(sess, ()) or "*" in drop.get(sess, ()):
                continue
            m = t["label"] == L
            o = np.argsort(t["win"][m].astype(int))
            d = blocks(t["pkg_w"][m][o], t["state"][m][o], skip)
            if len(d) < 2:
                continue
            n = int(t["n_cores"][m][0])
            r = dict(session=sess, label=L, a=t["a"][m][0], b=t["b"][m][0], n=n,
                     diff=d.mean(), ci=1.96 * d.std(ddof=1) / np.sqrt(len(d)))
            r["w"], r["wci"] = r["diff"] / n, r["ci"] / n
            for k, s in (("a", r["a"]), ("b", r["b"])):
                op, dl, b = spec(s)
                r[k + "_inrun"] = lat.get((L, k.upper()), np.nan)
                r[k + "_sweep"] = sweep_lat(op, dl) if b == 1 or dl == 0 else np.nan
            runs.append(r)
    return runs


def lat_of(r, k):
    v = r[k + "_inrun"]
    return (v, "in-run") if np.isfinite(v) else (r[k + "_sweep"], "sweep")


def gating(runs, op):
    """Best estimate of warm->gated saving for one op (W/core) + latency."""
    w, g = "%s:%d:1" % (op, WARM), "%s:%d:1" % (op, GATED)
    direct = [r for r in runs if r["a"] == g and r["b"] == w]
    if direct:
        r = min(direct, key=lambda r: r["wci"])
        sv, ci, how, sess = -r["w"], r["wci"], "direct A=gated B=warm", r["session"]
        lw, sw = lat_of(r, "b"); lg, sg = lat_of(r, "a")
    else:
        best = None
        for sess in sorted({r["session"] for r in runs}):
            rw = [r for r in runs if r["session"] == sess and r["a"] == w and r["b"].startswith("none:")]
            rg = [r for r in runs if r["session"] == sess and r["a"] == g and r["b"].startswith("none:")]
            if rw and rg:
                ci = np.hypot(rw[0]["wci"], rg[0]["wci"])
                if best is None or ci < best[1]:
                    best = (rw[0]["w"] - rg[0]["w"], ci, rw[0], rg[0], sess)
        if best is None:
            return dict(unit=UNIT[op], op=op, method="missing (needs %s vs %s; scripts/run_phase12.sh 10 p2c)" % (g, w))
        sv, ci, rw, rg, sess = best
        how = "same-session pair (warm vs none) - (gated vs none)"
        lw, sw = lat_of(rw, "a"); lg, sg = lat_of(rg, "a")
    return dict(unit=UNIT[op], op=op, session=sess, method=how, warm_cyc=lw, gated_cyc=lg,
                penalty_cyc=lg - lw, lat_src=sw if sw == sg else sw + "/" + sg,
                saving_w=sv, saving_ci=ci)


def plot(runs, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    col = {"dpbssd": "#257de0", "fma512": "#f47230", "fma256": "#00b894"}
    mk = {"dpbssd": "o", "fma512": "s", "fma256": "^"}
    fig, ax = plt.subplots(figsize=(7, 4.2), dpi=150)
    for op in col:
        pts = []
        for r in runs:
            oa, da, ba = spec(r["a"])
            if oa != op or not r["b"].startswith("none:") or r["n"] < 15:
                continue
            pts.append((max(da, 30), r["w"], r["wci"], lat_of(r, "a")[0], r["session"]))
        if not pts:
            continue
        pts.sort()
        x, y, e, lat, _ = zip(*pts)
        ax.errorbar(x, y, yerr=e, fmt=mk[op], ms=7, color=col[op], ecolor=col[op], elinewidth=1,
                    capsize=0, mec="white", mew=1.5, label=UNIT[op], zorder=3)
        for xi, yi, li in zip(x, y, lat):
            if np.isfinite(li):
                ax.annotate("%d cyc" % li, (xi, yi), textcoords="offset points", xytext=(8, -3),
                            fontsize=7, color="#555555")
    ax.axhline(0, color="#999999", lw=0.8, zorder=1)
    ax.set_xscale("log")
    ax.set_xlabel("time between ops (ns; 0 = back-to-back, plotted at 30)")
    ax.set_ylabel("extra package power per core vs spin (W)")
    ax.set_title("Power vs. latency state, 15 cores at 2.0 GHz (labels: op latency)", fontsize=10)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.grid(axis="y", color="#e6e6e6", lw=0.6)
    ax.legend(frameon=False, fontsize=8)
    fig.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    fig.savefig(path)
    print("wrote", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="*", help="power_ab.csv files (default: all data/raw/phase12_*/power_ab.csv)")
    ap.add_argument("--skip", type=int, default=4)
    ap.add_argument("--drop", default=DROP, help="session:label,label;session:... runs to exclude")
    ap.add_argument("--outdir", default="data/processed/phase2")
    ap.add_argument("--fig", default="")
    a = ap.parse_args()
    paths = a.csv or sorted(glob.glob("data/raw/phase12_*/power_ab.csv"))
    paths = [p for p in paths if "quick" not in p]
    drop = {}
    for part in filter(None, a.drop.split(";")):
        s, labs = part.split(":")
        drop[s] = set(labs.split(","))
    runs = load_runs(paths, drop, a.skip)

    os.makedirs(a.outdir, exist_ok=True)
    out = os.path.join(a.outdir, "power_vs_latency.csv")
    with open(out, "w") as f:
        f.write("session,label,n_cores,a,b,w_per_core,ci95_per_core,a_lat_inrun,a_lat_sweep,b_lat_inrun,b_lat_sweep\n")
        for r in runs:
            f.write("%s,%s,%d,%s,%s,%.4f,%.4f,%.0f,%.0f,%.0f,%.0f\n" % (
                r["session"], r["label"], r["n"], r["a"], r["b"], r["w"], r["wci"],
                r["a_inrun"], r["a_sweep"], r["b_inrun"], r["b_sweep"]))
    print("%-34s %-20s %3s %-16s %-16s %8s %7s %8s %8s" % ("session", "label", "n", "A", "B", "W/core", "ci", "lat_A", "src"))
    for r in runs:
        la, sa = lat_of(r, "a")
        print("%-34s %-20s %3d %-16s %-16s %8.3f %7.3f %8.0f %8s" % (
            r["session"][:34], r["label"], r["n"], r["a"], r["b"], r["w"], r["wci"], la, sa))
    print("wrote", out)

    g = [gating(runs, op) for op in ("dpbssd", "fma512", "fma256")]
    out = os.path.join(a.outdir, "gating_summary.csv")
    cols = ["unit", "op", "session", "method", "warm_cyc", "gated_cyc", "penalty_cyc", "lat_src", "saving_w", "saving_ci"]
    with open(out, "w") as f:
        f.write(",".join(cols) + "\n")
        for x in g:
            f.write(",".join(("%.4f" % x[c]) if isinstance(x.get(c), float) else str(x.get(c, "")) for c in cols) + "\n")
    print("\ngating (warm = op every %d ns, gated = every %d us):" % (WARM, GATED // 1000))
    for x in g:
        if "saving_w" in x:
            print("  %-8s latency %5.0f -> %5.0f cyc (+%.0f, %s) | power saved %.3f +- %.3f W/core | %s [%s]" % (
                x["unit"], x["warm_cyc"], x["gated_cyc"], x["penalty_cyc"], x["lat_src"],
                x["saving_w"], x["saving_ci"], x["method"], x["session"]))
        else:
            print("  %-8s %s" % (x["unit"], x["method"]))
    print("wrote", out)
    if a.fig:
        plot(runs, a.fig)


if __name__ == "__main__":
    main()
