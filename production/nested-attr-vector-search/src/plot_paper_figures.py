#!/usr/bin/env python3
"""Generate paper figures from measured experiment data only.

Reads the CSV/txt artifacts under
researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps/data/
and writes PNG figures into .../07-laminar-edge-sharing/figures/.

All series are measured points; no interpolation or synthetic data is added.
"""
import csv
import json
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
os.makedirs(OUT, exist_ok=True)

plt.rcParams.update({
    "font.size": 9,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.dpi": 160,
})


def read_csv(name):
    with open(os.path.join(DATA, name)) as f:
        return list(csv.DictReader(f))


# ---------------------------------------------------------------- Figure 2
# QPS vs Recall@10 measured operating points (Subtree, 1365-node workload).
les = read_csv("ccs-les.csv")
base = read_csv("ccs-baselines.csv")

series = {
    "LES-Plain": [],
    "BF-HNSW": [],
    "PSG": [],
    "PFS (Subtree)": [],
}
for r in les:
    if r["method"] == "LES_prototype":
        series["LES-Plain"].append(
            (float(r["recall_at_10"]), float(r["qps"])))
for r in base:
    if r["method"] == "filtered_hnsw":
        series["BF-HNSW"].append(
            (float(r["recall_at_10"]), float(r["qps"])))
    elif r["method"] == "B4_per_scope":
        series["PSG"].append(
            (float(r["recall_at_10"]), float(r["qps"])))
    elif r["method"] == "B0_subtree_scan":
        series["PFS (Subtree)"].append(
            (float(r["recall_at_10"]), float(r["qps"])))

fig, ax = plt.subplots(figsize=(4.6, 3.0))
styles = {
    "LES-Plain": ("o", "-", "#1f77b4"),
    "BF-HNSW": ("s", "--", "#d62728"),
    "PSG": ("^", ":", "#2ca02c"),
    "PFS (Subtree)": ("D", ":", "#7f7f7f"),
}
for name, pts in series.items():
    if not pts:
        continue
    pts.sort()
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    marker, ls, color = styles[name]
    ax.plot(xs, ys, marker=marker, linestyle=ls, color=color, label=name,
            markersize=4, linewidth=1.2)
ax.set_xlabel("Recall@10")
ax.set_ylabel("QPS (queries/s)")
ax.set_yscale("log")
ax.legend(frameon=False, fontsize=7, loc="upper left")
ax.set_title("Subtree workload, 1365 nodes (measured points)", fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_qps_recall.pdf"))
fig.savefig(os.path.join(OUT, "fig_qps_recall.png"))
plt.close(fig)

# ---------------------------------------------------------------- Figure 3
# Operating-point QPS vs index footprint (Subtree).
fig, ax = plt.subplots(figsize=(4.2, 2.8))
points = [
    ("PFS", 3008.5, 51.0, "#7f7f7f", "D"),
    ("PSG", 74706.0, 194.0, "#2ca02c", "^"),
    ("BF-HNSW ef384", 1282.0, 75.0, "#d62728", "s"),
    ("LES-Plain ef32", 15694.0, 144.314, "#1f77b4", "o"),
    ("LES-Plain ef64", 9940.0, 144.314, "#1f77b4", "o"),
    ("LES-Plain ef128", 5764.0, 144.314, "#1f77b4", "o"),
    ("LES-Run ef32", 12898.0, 124.029, "#9467bd", "P"),
]
for label, qps, mb, color, mk in points:
    ax.scatter(mb, qps, color=color, marker=mk, s=36)
    pos = (-90, -16) if label == "LES-Run ef32" else (5, 4)
    ax.annotate(label, (mb, qps), textcoords="offset points", xytext=pos,
                fontsize=6.5)
ax.set_xlabel("Index size (MB)")
ax.set_ylabel("QPS (queries/s)")
ax.set_yscale("log")
ax.set_xlim(30, 220)
ax.set_title("Measured operating points, Subtree", fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_qps_space.png"))
fig.savefig(os.path.join(OUT, "fig_qps_space.pdf"))
plt.close(fig)

# ---------------------------------------------------------------- Build times
# LES base-graph construction is not measured in the LES run. A time from
# another method cannot be added to the LES dedup time.
fig, ax = plt.subplots(figsize=(4.4, 2.6))
labels = ["PSG total\n(single thread)", "BF-HNSW\n(OMP=1)",
          "BF-HNSW\n(default threads)", "LES-Plain dedup\n(base reused)"]
times = [57.0, 28.31, 4.8, 4.5]
bars = ax.bar(labels, times, color=["#2ca02c", "#d62728", "#d62728", "#1f77b4"])
for bar, value in zip(bars, times):
    ax.text(bar.get_x() + bar.get_width() / 2, value + 1.0,
            f"{value:g}s", ha="center", fontsize=7)
ax.set_ylabel("Observed elapsed time (s)")
ax.set_title("LES base-graph build not measured in this run", fontsize=8.5)
ax.tick_params(axis="x", labelsize=7)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_build_time.png"))
fig.savefig(os.path.join(OUT, "fig_build_time.pdf"))
plt.close(fig)

# ---------------------------------------------------------------- Figure 5
# Static edge-dedup analysis (85-node hierarchy).
vals = {}
with open(os.path.join(DATA, "les-analysis.txt")) as f:
    for line in f:
        if line.startswith("observed_directed_edges"):
            vals["observed"] = int(line.split()[1])
        elif line.startswith("unique_directed_edges"):
            vals["unique"] = int(line.split()[1])
        elif line.startswith("two_segment:"):
            parts = line.split()
            vals["exact"] = int(parts[2])
            vals["cross"] = int(parts[4])

fig, axes = plt.subplots(1, 2, figsize=(6.4, 2.5))
ax = axes[0]
ax.bar(["observed\nedges", "unique\nedges", "exact\nentries",
        "cross\nentries"],
       [vals["observed"], vals["unique"], vals["exact"], vals["cross"]],
       color=["#bbbbbb", "#1f77b4", "#2ca02c", "#ff7f0e"])
for i, v in enumerate([vals["observed"], vals["unique"], vals["exact"],
                       vals["cross"]]):
    ax.text(i, v * 1.01, f"{v/1e6:.2f}M", ha="center", fontsize=7)
ax.set_ylabel("entries (millions)")
ax.set_title("Edge dedup, 85-node hierarchy", fontsize=8.5)
ax = axes[1]
red = 1.0 - vals["unique"] / vals["observed"]
ax.bar(["duplicate\nfraction"], [red * 100], color="#1f77b4", width=0.45)
ax.text(0, red * 100 + 1, f"{red*100:.1f}%", ha="center", fontsize=9)
ax.set_ylim(0, 70)
ax.set_ylabel("percent of observed edges")
ax.set_title("Redundancy removed by edge dedup", fontsize=8.5)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_dedup.png"))
fig.savefig(os.path.join(OUT, "fig_dedup.pdf"))
plt.close(fig)

# ---------------------------------------------------------------- Figure 6
# PSG optimization progression (85-node hierarchy): throughput vs stage.
rows = read_csv("primary-progression.csv")
stages = [r["stage"].replace("_", "\n") for r in rows]
qps = [float(r["qps"]) for r in rows]
rec = [float(r["recall_at_10"]) for r in rows]
fig, ax1 = plt.subplots(figsize=(6.6, 2.7))
x = range(len(rows))
ax1.plot(x, qps, marker="o", color="#1f77b4", linewidth=1.2, markersize=4,
         label="QPS")
ax1.set_ylabel("QPS (queries/s)", color="#1f77b4")
ax1.tick_params(axis="y", labelcolor="#1f77b4")
ax2 = ax1.twinx()
ax2.plot(x, rec, marker="s", color="#d62728", linestyle="--", linewidth=1.2,
         markersize=4, label="Recall@10")
ax2.set_ylabel("Recall@10", color="#d62728")
ax2.tick_params(axis="y", labelcolor="#d62728")
ax2.grid(False)
ax1.set_xticks(list(x))
ax1.set_xticklabels(stages, fontsize=6.2)
ax1.set_title("PSG: measured optimization stages",
              fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_progression.png"))
fig.savefig(os.path.join(OUT, "fig_progression.pdf"))
plt.close(fig)

print("figures written to", OUT)
for name in sorted(os.listdir(OUT)):
    print(" ", name)

# ---------------------------------------------------------------- Figure 7
# Run metadata includes offsets and starts; 3B/run omitted access metadata.
fig, ax = plt.subplots(figsize=(3.6, 2.5))
names = ["LES-Plain\n(u16)", "LES-Run\n(index)", "entropy\nreference"]
vals_mb = [26.775408, 6.490862, 1.148]
colors = ["#bbbbbb", "#1f77b4", "#2ca02c"]
bars = ax.bar(names, vals_mb, color=colors)
for b, v in zip(bars, vals_mb):
    ax.text(b.get_x() + b.get_width() / 2, v + 0.7, f"{v:.2f} MB",
            ha="center", fontsize=7)
ax.set_ylabel("activation metadata (MB, decimal)")
ax.set_title("H2 activation metadata, measured vs reference", fontsize=8)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_act_rle.png"))
fig.savefig(os.path.join(OUT, "fig_act_rle.pdf"))
plt.close(fig)

# ---------------------------------------------------------------- Figure 8
# Subtree covering-radius ratio by depth (measured; >1 means no shrinkage).
rows = read_csv("subtree-radius.csv")
by_depth = {}
for r in rows:
    by_depth.setdefault(int(r["depth"]), []).append(float(r["gamma"]))
depths = sorted(by_depth)
means = [float(np.mean(by_depth[d])) for d in depths]
mins = [min(by_depth[d]) for d in depths]
maxs = [max(by_depth[d]) for d in depths]
fig, ax = plt.subplots(figsize=(3.6, 2.5))
ax.errorbar(depths, means,
            yerr=[np.array(means) - np.array(mins),
                  np.array(maxs) - np.array(means)],
            marker="o", color="#1f77b4", capsize=3, linewidth=1.2)
ax.axhline(1.0, color="#d62728", linestyle="--", linewidth=1)
ax.set_xlabel("hierarchy depth")
ax.set_ylabel(r"$\gamma_u = R(u)/R(root)$")
ax.set_title("Covering radius does not shrink with depth", fontsize=8.5)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_radius.png"))
fig.savefig(os.path.join(OUT, "fig_radius.pdf"))
plt.close(fig)
print("extra figures written")

# ---------------------------------------------------------------- Figure 9
# Query latency vs feasible-set size (single-repeat LES run, ef=32).
rows = read_csv("latency-vs-scope.csv")
feas = np.array([float(r["feasible_sum"]) for r in rows])
lat = np.array([float(r["lat_us"]) for r in rows])
bins = [0, 1e3, 1e4, 2e4, 4e4, 6e4, 1e5]
centers, p50s, p95s, counts = [], [], [], []
for lo, hi in zip(bins[:-1], bins[1:]):
    m = (feas >= lo) & (feas < hi)
    if m.sum() <= 5:
        continue
    centers.append((lo + hi) / 2 if lo > 0 else hi / 2)
    p50s.append(np.median(lat[m]))
    p95s.append(np.percentile(lat[m], 95))
    counts.append(int(m.sum()))
fig, ax = plt.subplots(figsize=(4.4, 2.7))
x = np.arange(len(centers))
ax.bar(x - 0.18, p50s, width=0.36, color="#1f77b4", label="p50")
ax.bar(x + 0.18, p95s, width=0.36, color="#d62728", label="p95")
for i, c in enumerate(counts):
    ax.text(i, max(p50s[i], p95s[i]) + 6, f"n={c}", ha="center", fontsize=6.2)
labels = ["<1k", "1–10k", "10–20k", "20–40k", "40–60k", "60–100k"]
ax.set_xticks(x)
ax.set_xticklabels(labels[:len(centers)], fontsize=7)
ax.set_xlabel("feasible-set size (sum over queried nodes)")
ax.set_ylabel("per-query latency (us)")
ax.set_title("Measured latency by scope-size bin (LES-Plain ef=32)",
             fontsize=8.5)
ax.legend(frameon=False, fontsize=7)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_latency_scope.png"))
fig.savefig(os.path.join(OUT, "fig_latency_scope.pdf"))
plt.close(fig)
print("latency figure written")

# ---------------------------------------------------------------- Figure 10
# Exact semantics: scan vs shared-graph search (measured, ef=32, single run).
fig, axes = plt.subplots(1, 2, figsize=(5.6, 2.4))
ax = axes[0]
labels_e = ["PFS (Exact)", "diagnostic exact graph\n(post-filter)"]
qps_e = [191914, 45078]
bars = ax.bar(labels_e, qps_e, color=["#2ca02c", "#d62728"])
ax.set_yscale("log")
ax.set_ylim(3e4, 4e5)
ax.set_ylabel("QPS (log)")
for b, v in zip(bars, qps_e):
    ax.text(b.get_x() + b.get_width() / 2, v * 1.25, f"{v:,}", ha="center",
            fontsize=7)
ax.set_title("Exact: PFS vs diagnostic graph", fontsize=8.5)
ax.tick_params(axis="x", labelsize=6.8)
ax = axes[1]
labels_v = ["no post-filter", "with post-filter"]
viol = [739 / 9991 * 100, 0.0]
bars = ax.bar(labels_v, viol, color=["#ff7f0e", "#1f77b4"])
ax.set_ylabel("% of returned ids violating scope")
for b, v in zip(bars, viol):
    ax.text(b.get_x() + b.get_width() / 2, v + 0.15, f"{v:.1f}%", ha="center",
            fontsize=7)
ax.set_title("Scope violations on direct edges", fontsize=8.5)
ax.tick_params(axis="x", labelsize=6.8)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig_exact_modes.png"))
fig.savefig(os.path.join(OUT, "fig_exact_modes.pdf"))
plt.close(fig)
print("exact-mode figure written")
