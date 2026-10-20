#!/usr/bin/env python3
"""Plot the five-input Subtree comparison from a run_five_l2 CSV.

Reads the newest researches/.../data/five_l2_*.csv (or --csv) and writes
figures/fig_five_l2.png|pdf. Only measured rows with status=ok are used.
No interpolation: points are the fixed operating point of each method.
"""
import argparse
import csv
import glob
import os

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
DATA = os.path.join(ROOT, "researches", "nested-attr-vector-search",
                    "07-laminar-edge-sharing", "2026-10-20-exps", "data")
OUT = os.path.join(ROOT, "researches", "nested-attr-vector-search",
                   "07-laminar-edge-sharing", "figures")

LABEL = {
    "pfs_subtree_scan": "PFS",
    "psg": "PSG",
    "les_ef32": "LES-Run",
    "bf_hnsw": "BF-HNSW",
    "triehi_hnsw": "TRIEHI+HNSW",
    "native_pathseer": "PathSeer",
}
STYLE = {
    "pfs_subtree_scan": ("D", "#7f7f7f"),
    "psg": ("^", "#2ca02c"),
    "les_ef32": ("o", "#1f77b4"),
    "bf_hnsw": ("s", "#d62728"),
    "triehi_hnsw": ("P", "#9467bd"),
    "native_pathseer": ("X", "#ff7f0e"),
}
ORDER = ["sift25k", "sift50k", "sift100k", "mnist25k", "fashion-mnist25k"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv")
    ap.add_argument("--sweep")
    args = ap.parse_args()
    path = args.csv or sorted(
        p for p in glob.glob(os.path.join(DATA, "five_l2_20*.csv"))
        if "sweep" not in p and "summary" not in p and "degree" not in p)[-1]
    rows = [r for r in csv.DictReader(open(path)) if r["status"] == "ok"]
    sweep_path = args.sweep or path.replace("five_l2_", "five_l2_sweep_")
    if os.path.exists(sweep_path):
        for r in csv.DictReader(open(sweep_path)):
            rows.append({"dataset": r["dataset"], "method": r["method"],
                         "recall_at_10": r["recall_at_10"], "qps": r["qps"],
                         "status": "ok"})
    datasets = [d for d in ORDER if any(r["dataset"] == d for r in rows)]
    fig, axes = plt.subplots(1, len(datasets), figsize=(2.7 * len(datasets), 2.6),
                             squeeze=False, sharey=True)
    for ax, ds in zip(axes[0], datasets):
        for method in LABEL:
            pts = sorted({(round(float(r["recall_at_10"]), 5), round(float(r["qps"]), 2))
                          for r in rows if r["dataset"] == ds and r["method"] == method})
            if not pts:
                continue
            x = [p[0] for p in pts]
            y = [p[1] for p in pts]
            marker, color = STYLE[method]
            if method in ("psg", "les_ef32", "bf_hnsw"):
                ax.plot(x, y, marker=marker, color=color, markersize=3.2,
                        linewidth=0.9)
            else:
                ax.scatter(x, y, marker=marker, color=color, s=26)
        ax.set_title(ds, fontsize=8.5)
        ax.set_xlabel("Recall@10", fontsize=8)
        ax.grid(alpha=0.3)
        ax.set_xlim(0.55, 1.02)
    axes[0][0].set_ylabel("QPS (queries/s)")
    axes[0][0].set_yscale("log")
    handles = [plt.Line2D([], [], marker=STYLE[m][0], color=STYLE[m][1],
                          linestyle="", label=LABEL[m])
               for m in LABEL if any(r["method"] == m for r in rows)]
    fig.legend(handles=handles, loc="lower center", ncol=len(handles),
               frameon=False, fontsize=7)
    fig.suptitle("Recall-QPS, fixed per-method parameters (synthetic hierarchy)",
                 fontsize=9)
    fig.tight_layout(rect=(0, 0.06, 1, 0.95))
    os.makedirs(OUT, exist_ok=True)
    fig.savefig(os.path.join(OUT, "fig_five_l2.png"))
    fig.savefig(os.path.join(OUT, "fig_five_l2.pdf"))
    print("wrote", os.path.join(OUT, "fig_five_l2.png"), "from", path,
          "+", sweep_path)


if __name__ == "__main__":
    main()
