#!/usr/bin/env python3
"""Plot LES-v2 relative to shared-vector PSG across all dense corpora."""
import argparse
import csv

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

EXPS = ("/home/yiming/projects/scholar-workspace/vector-database/researches/"
        "nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps")
OUT = ("/home/yiming/projects/scholar-workspace/vector-database/researches/"
       "nested-attr-vector-search/07-laminar-edge-sharing/figures")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--summary", default=f"{EXPS}/data/all_datasets_20261020_summary.csv")
    args = ap.parse_args()
    rows = list(csv.DictReader(open(args.summary)))

    def f(x):
        return float(x) if x not in ("", "nan") else None

    data = []
    for r in rows:
        q = f(r["les_over_psg_r95"]); s = f(r["les_over_psg_space"])
        if s is None:
            continue
        data.append((r["dataset"], q, s, r["quantized"] == "True"))
    # PSG missing r95: LES wins by reaching the threshold; draw at 2.0.
    data.sort(key=lambda t: (t[1] is None, t[1] if t[1] is not None else 2.0))
    labels = [d[0] + ("" if not d[3] else "†") for d in data]
    vals = [2.0 if d[1] is None else min(d[1], 2.0) for d in data]
    space = [d[2] for d in data]
    colors = ["#2ca02c" if (d[1] is None or d[1] >= 1.0) else "#d62728" for d in data]

    fig, axes = plt.subplots(1, 2, figsize=(7.4, 3.2), sharey=True)
    ax = axes[0]
    ax.barh(labels, vals, color=colors)
    ax.axvline(1.0, color="black", linewidth=0.8, linestyle="--")
    for i, (d, v) in enumerate(zip(data, vals)):
        txt = "PSG<r95" if d[1] is None else f"{v:.2f}x"
        ax.text(v + 0.04, i, txt, va="center", fontsize=6.5)
    ax.set_xlim(0, 2.7)
    ax.set_xlabel("LES / PSG QPS at Recall@10 ≥ 0.95")
    ax.set_title("Throughput ratio (>1: LES faster)", fontsize=8.5)
    ax.grid(axis="x", alpha=0.3)

    ax = axes[1]
    ax.barh(labels, space, color="#1f77b4")
    ax.axvline(1.0, color="black", linewidth=0.8, linestyle="--")
    for i, v in enumerate(space):
        ax.text(v + 0.02, i, f"{v:.2f}x", va="center", fontsize=6.5)
    ax.set_xlim(0, max(space) * 1.25)
    ax.set_xlabel("LES index bytes / PSG-shared index bytes")
    ax.set_title("Space ratio (<1: LES smaller)", fontsize=8.5)
    ax.grid(axis="x", alpha=0.3)
    fig.suptitle("11 dense corpora, n=25k, fixed M=16 († quantized to u8, L2 objective)",
                 fontsize=8.5)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(f"{OUT}/fig_all_datasets.png")
    fig.savefig(f"{OUT}/fig_all_datasets.pdf")
    print("wrote", f"{OUT}/fig_all_datasets.png")


if __name__ == "__main__":
    main()
