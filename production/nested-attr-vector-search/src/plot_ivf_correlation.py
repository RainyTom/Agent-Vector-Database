#!/usr/bin/env python3
"""IVF vs exact vs BF-HNSW under correlated predicates (cluster far/near).

Reads archived curve CSVs (far/near, no IVF) and the new IVF runs:
  work/selectivity_cluster_ivf/selectivity.csv
  work/selectivity_cluster_near_ivf/selectivity.csv
Output: 2026-10-26-exps/figures/selectivity_ivf_correlation.svg
"""

from __future__ import annotations

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / "production" / "nested-attr-vector-search" / "work"
DATA = (ROOT / "researches" / "nested-attr-vector-search" /
        "01-directory-aware-acceleration" / "2026-10-26-exps" / "data")
OUT = DATA.parent / "figures"


def load(path: Path, mode: str) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return [r for r in csv.DictReader(f) if r["mode"] == mode]


def series(rows, method):
    pts = []
    for r in rows:
        if r["method"] != method:
            continue
        pts.append((float(r["s"]), float(r["qps"]), r["ok"] == "1"))
    return sorted(pts)


def add(ax, rows, method, style, label, gated_hollow=True):
    pts = series(rows, method)
    ok = [p for p in pts if p[2]]
    bad = [p for p in pts if not p[2]]
    if len(ok) > 1:
        ax.plot([p[0] for p in ok], [p[1] for p in ok], label=label, **style)
    elif ok:
        ax.scatter([p[0] for p in ok], [p[1] for p in ok], label=label,
                   marker=style.get("marker", "o"), color=style.get("color"))
    if bad and gated_hollow:
        ax.scatter([p[0] for p in bad], [p[1] for p in bad],
                   marker=style.get("marker", "o"), facecolors="none",
                   edgecolors=style.get("color"))


def panel(ax, rows, title):
    add(ax, rows, "exact_list",
        dict(linestyle="-", marker="s", color="black"), "exact list")
    add(ax, rows, "hnsw",
        dict(linestyle="--", marker="o", color="tab:blue"), "BF-HNSW")
    add(ax, rows, "ivf",
        dict(linestyle="-.", marker="^", color="tab:red"), "IVF+sel")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("selectivity s")
    ax.set_ylabel("QPS @r95")
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)


def main() -> int:
    # Use only the new runs' CSVs: each contains exact_list + hnsw + ivf rows
    # measured in the same run over identical predicates. The archived
    # far/near runs used a different bucket grid, so their rows are NOT
    # pairing-compatible with these (verified: exact_list QPS differs up to
    # 2.5x at shared s between runs).
    far = load(BASE / "selectivity_cluster_ivf" / "selectivity.csv", "cluster")
    near = load(BASE / "selectivity_cluster_near_ivf" / "selectivity.csv",
                "cluster_near")

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), sharey=True)
    panel(axes[0], far, "cluster-far (query independent)")
    panel(axes[1], near, "cluster-near (query = centroid)")
    axes[0].legend(fontsize=8)
    fig.tight_layout()
    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / "selectivity_ivf_correlation.svg"
    fig.savefig(out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
