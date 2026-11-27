"""Shared helpers for the analysis scripts (numpy only; runs on Python 3.9+).

Latency CSVs come from bench/amx_probe.c and bench/avx512_probe.c:
    unit,delta_ns,trial,tsc,cyc,tsc_mhz,cpu[,idle_ns,t_ns]
`cyc` is core cycles (RDPMC) for the timed op, timer overhead included.
"""
import csv
import os

import numpy as np


def read_csv(path):
    """CSV -> dict of column name -> numpy array (numeric where possible)."""
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    if not rows:
        return {}
    head, body = rows[0], [r for r in rows[1:] if len(r) == len(rows[0])]
    out = {}
    for i, h in enumerate(head):
        col = [r[i] for r in body]
        try:
            out[h] = np.array([float(x) for x in col])
        except ValueError:
            out[h] = np.array(col)
    return out


def label_of(path):
    """'data/raw/slot_x/amx_dpbssd_st.csv' -> 'slot_x/amx_dpbssd_st'."""
    d, f = os.path.split(os.path.abspath(path))
    return os.path.basename(d) + "/" + os.path.splitext(f)[0]


def x_of(t):
    """Idle used for the x axis: measured idle_ns when the probe slept
    (requested delta is only a lower bound), requested delta_ns otherwise."""
    if "idle_ns" in t and str(t["unit"][0]).endswith("_sleep"):
        return t["idle_ns"]
    return t["delta_ns"]


def per_delta(t):
    """Group samples by requested delta. Yields (delta, idx) sorted by delta."""
    d = t["delta_ns"]
    for v in np.unique(d):
        yield v, np.nonzero(d == v)[0]


def levels(cyc, gap=1.10, merge=1.20, min_frac=0.01):
    """Find latency levels (states) in a 1-D sample of cycle counts.

    Sort log(cyc); split wherever two consecutive values differ by more than
    `gap` (x1.10). Drop clusters holding < min_frac of samples (stray
    outliers are not states; classify() assigns them to the nearest level).
    Then merge neighbouring clusters whose medians are within `merge` (x1.20):
    those are a state's tail, not a separate state. Tuned on slots 1-2:
    recovers AMX 133/672/5.7k/7.7k/9.4k and AVX 124/232/327.
    Returns the sorted list of level medians.
    """
    v = np.sort(np.log(np.asarray(cyc, float)))
    cuts = np.nonzero(np.diff(v) > np.log(gap))[0]
    groups = [g for g in np.split(v, cuts + 1) if len(g) >= max(2, min_frac * len(v))]
    out = []
    for g in groups:
        if out and np.median(g) - np.median(out[-1]) < np.log(merge):
            out[-1] = np.concatenate([out[-1], g])
        else:
            out.append(g)
    return [float(np.exp(np.median(g))) for g in out]


def classify(cyc, lv):
    """Index of the nearest level (in log space) for each sample."""
    lv = np.log(np.asarray(lv))
    return np.abs(np.log(np.asarray(cyc, float))[:, None] - lv[None, :]).argmin(1)
