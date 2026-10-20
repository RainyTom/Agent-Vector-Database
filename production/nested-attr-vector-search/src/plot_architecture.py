#!/usr/bin/env python3
"""Draw the LES architecture schematic (vector output, text-safe).

Panels: (a) attribute tree with a highlighted subtree interval,
(b) shared level-l navigation graph with per-edge MA ownership,
(c) interval-sliced CSR with two-segment buckets.
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
OUT = os.path.join(ROOT, "researches", "nested-attr-vector-search",
                   "07-laminar-edge-sharing", "figures")
os.makedirs(OUT, exist_ok=True)

plt.rcParams.update({"font.size": 8, "figure.dpi": 180})

fig = plt.figure(figsize=(7.2, 2.9))
gs = fig.add_gridspec(1, 3, width_ratios=[1.0, 1.15, 1.55], wspace=0.28)

# ---------------------------------------------------------------- (a) tree
ax = fig.add_subplot(gs[0, 0])
ax.set_xlim(0, 10)
ax.set_ylim(0, 10)
ax.axis("off")
ax.set_title("(a) attribute tree T", fontsize=8, ha="left", x=0.0)

nodes = {
    "r": (5.0, 8.6),
    "u1": (2.9, 6.2), "u2": (7.1, 6.2),
    "a": (1.5, 3.8), "b": (3.9, 3.8), "c": (6.4, 3.8), "d": (8.5, 3.8),
}
edges = [("r", "u1"), ("r", "u2"), ("u1", "a"), ("u1", "b"),
         ("u2", "c"), ("u2", "d")]
for p, q in edges:
    ax.plot([nodes[p][0], nodes[q][0]], [nodes[p][1], nodes[q][1]],
            color="#444444", lw=1.1, zorder=1)
for name, (x, y) in nodes.items():
    ax.scatter([x], [y], s=150, facecolor="white", edgecolor="#444444",
               zorder=3, linewidth=1.1)
    ax.text(x, y + 0.55, name, ha="center", va="bottom", fontsize=7.5,
            color="#222266")
ax.add_patch(Rectangle((0.55, 2.9), 4.15, 2.2, facecolor="#93b8f7",
                       alpha=0.35, edgecolor="none", zorder=0))
ax.text(2.6, 2.35, "subtree(u1)", ha="center", fontsize=7, color="#2563eb")
ax.text(5.0, 0.55, "DFS -> [tin(u1), tout(u1)]", ha="center", fontsize=7,
        color="#666666")

# ---------------------------------------------------------------- (b) graph
ax = fig.add_subplot(gs[0, 1])
ax.set_xlim(0, 10)
ax.set_ylim(0, 10)
ax.axis("off")
ax.set_title("(b) shared level-l graph", fontsize=8, ha="left", x=0.0)

pts = [(2.0, 7.4), (4.6, 8.4), (7.0, 7.0), (8.6, 8.2),
       (3.4, 4.6), (5.9, 4.0), (8.0, 4.9), (6.3, 6.0)]
gedges = [(0, 1), (1, 2), (2, 3), (0, 4), (4, 5), (5, 6), (6, 3),
          (1, 7), (7, 5), (2, 7)]
for i, j in gedges:
    ax.plot([pts[i][0], pts[j][0]], [pts[i][1], pts[j][1]],
            color="#9aa4b2", lw=0.9, zorder=1)
for i, (x, y) in enumerate(pts):
    ax.scatter([x], [y], s=34, facecolor="white", edgecolor="#2563eb",
               zorder=3, linewidth=1.2)
ax.text(5.0, 1.9, "edge (i,j) stored once\nat min(A_i and A_j)", ha="center",
        fontsize=7, color="#666666")

# ---------------------------------------------------------------- (c) CSR
ax = fig.add_subplot(gs[0, 2])
ax.set_xlim(0, 10)
ax.set_ylim(0, 10)
ax.axis("off")
ax.set_title("(c) interval-sliced CSR", fontsize=8, ha="left", x=0.0)

# header
ax.add_patch(Rectangle((0.2, 8.55), 2.6, 0.75, facecolor="#f1f5f9",
                       edgecolor="#444444", lw=0.7))
ax.text(1.5, 8.93, "offsets", ha="center", va="center", fontsize=7)
ax.add_patch(Rectangle((2.9, 8.55), 6.9, 0.75, facecolor="#f1f5f9",
                       edgecolor="#444444", lw=0.7))
ax.text(6.35, 8.93, "act (preorder, monotone)", ha="center", va="center",
        fontsize=7)

rows = [
    ["#93b8f7", "#93b8f7", "#f6c97a", "#f6c97a", "#f6c97a"],
    ["#93b8f7", "#f6c97a", "#f6c97a", "#ffffff", "#ffffff"],
    ["#93b8f7", "#93b8f7", "#93b8f7", "#f6c97a", "#ffffff"],
    ["#f6c97a", "#f6c97a", "#ffffff", "#ffffff", "#ffffff"],
]
cell_w, cell_h = 1.38, 0.72
for r, colors in enumerate(rows):
    y = 7.55 - r * cell_h
    ax.add_patch(Rectangle((0.2, y), 2.6, cell_h, facecolor="#f8fafc",
                           edgecolor="#444444", lw=0.6))
    ax.text(1.5, y + cell_h / 2, f"i={r}", ha="center", va="center",
            fontsize=6.8, color="#222266")
    for c, col in enumerate(colors):
        ax.add_patch(Rectangle((2.9 + c * cell_w, y), cell_w, cell_h,
                               facecolor=col, edgecolor="#444444", lw=0.6))

ax.annotate("", xy=(2.9, 4.85), xytext=(4.28, 4.85),
            arrowprops=dict(arrowstyle="<->", color="#2563eb", lw=1.2))
ax.text(3.6, 4.35, "slice [tin, tout]", ha="center", fontsize=7,
        color="#2563eb")

ax.add_patch(Rectangle((0.2, 2.55), 0.55, 0.34, facecolor="#93b8f7",
                       edgecolor="#444444", lw=0.5))
ax.text(0.9, 2.72, "B_exact", va="center", fontsize=6.8, color="#222266")
ax.add_patch(Rectangle((3.3, 2.55), 0.55, 0.34, facecolor="#f6c97a",
                       edgecolor="#444444", lw=0.5))
ax.text(4.0, 2.72, "B_cross", va="center", fontsize=6.8, color="#222266")

ax.text(0.2, 0.95, "Subtree: one slice.  Exact: one segment.\n"
                   "Boolean Phi: disjoint intervals.",
        fontsize=6.8, color="#666666")

fig.savefig(os.path.join(OUT, "fig_architecture.png"), bbox_inches="tight")
fig.savefig(os.path.join(OUT, "fig_architecture.pdf"), bbox_inches="tight")
print("wrote fig_architecture.{png,pdf}")
