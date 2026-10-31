#!/usr/bin/env python3
"""Plot offline information only, not ANN quality/latency."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("input", type=Path)
ap.add_argument("output", type=Path)
a = ap.parse_args()
with a.input.open(newline="") as f:
    data = [r for r in csv.DictReader(f) if r["r"] == "64"]
fig, axes = plt.subplots(1, 2, figsize=(12, 4), sharey=True)
fields = ["whole_skip_items_fraction", "conditional_skip_items_fraction", "gap_skip_items_fraction", "exact_oracle_skip_items_fraction"]
labels = ["Whole box", "Conditional box", "Conditional gap", "Exact block min\n(oracle only)"]
for ax, dataset in zip(axes, ["wiki-root3626", "sift12000"]):
    for offset, control, color in [(-.18, "correlated", "#3569a8"), (.18, "shuffled", "#dd8d32")]:
        row = next(r for r in data if r["dataset"] == dataset and r["control"] == control)
        vals = [100 * float(row[k]) for k in fields]
        bars = ax.bar([i + offset for i in range(4)], vals, .35, label=control, color=color)
        ax.bar_label(bars, fmt="%.1f", fontsize=8, padding=2)
    ax.set_xticks(range(4), labels, fontsize=9)
    ax.set_title(dataset)
    ax.set_ylim(0, 102)
    ax.grid(axis="y", alpha=.2)
axes[0].set_ylabel("Eligible ITEM weight in pruned nonempty blocks (%)")
axes[1].legend(loc="upper left", fontsize=8)
fig.suptitle("Offline conditional information, r=64; 8 query vectors/dataset\nPooled predeclared scenarios; matched block cardinalities; GT tau, not online ANN")
fig.tight_layout()
a.output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(a.output, dpi=150)
print(a.output)
