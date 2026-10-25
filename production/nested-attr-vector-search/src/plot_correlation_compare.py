#!/usr/bin/env python3
"""Compare random vs correlated-cluster predicates at SIFT100k."""

from __future__ import annotations

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
DATA = (ROOT / "researches" / "nested-attr-vector-search" /
        "01-directory-aware-acceleration" / "2026-10-26-exps" / "data")
OUT = DATA.parent / "figures"


def load(path: Path, mode: str) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return [r for r in csv.DictReader(f) if r["mode"] == mode]


def series(rows, method, key, ef=None, ok_only=False):
    out = []
    for r in rows:
        if r["method"] != method:
            continue
        if ef is not None and int(r["ef"]) != ef:
            continue
        if ok_only and r["ok"] != "1":
            continue
        out.append((float(r["s"]), float(r[key]), int(r["ef"]), r["ok"] == "1"))
    return sorted(out)


def plot_qps(ax, rows, label, color, marker):
    ex = series(rows, "exact_list", "qps")
    ax.plot([p[0] for p in ex], [p[1] for p in ex], marker=marker, color=color,
            linestyle="-", label=f"exact list, {label}")
    hn = series(rows, "hnsw", "qps")
    ok = [p for p in hn if p[3]]
    bad = [p for p in hn if not p[3]]
    if len(ok) > 1:
        ax.plot([p[0] for p in ok], [p[1] for p in ok], marker=marker,
                color=color, linestyle="--", label=f"BF-HNSW, {label}")
    else:
        ax.scatter([p[0] for p in ok], [p[1] for p in ok], marker=marker,
                   color=color, label=f"BF-HNSW (gated), {label}")
    if bad:
        ax.scatter([p[0] for p in bad], [p[1] for p in bad], marker=marker,
                   facecolors="none", edgecolors=color)


def main() -> int:
    rnd = load(DATA / "selectivity_curve_20261026.csv", "random")
    clu = load(DATA / "selectivity_cluster_curve_20261026.csv", "cluster")
    near = load(DATA / "selectivity_cluster_near_curve_20261026.csv", "cluster_near")
    probe = load(DATA / "selectivity_cluster_probe_32768_20261026.csv", "cluster")

    fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.6))
    ax = axes[0]
    plot_qps(ax, rnd, "random", "C0", "o")
    plot_qps(ax, clu, "cluster-far", "C1", "s")
    plot_qps(ax, near, "cluster-near", "C2", "D")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("filter selectivity s = |feasible| / n")
    ax.set_ylabel("QPS (open = r95 fail)")
    ax.set_title("Throughput: correlation direction decides HNSW")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=7, loc="lower left")
    ax.axvline(0.0605, color="0.5", linestyle=":", linewidth=1)
    ax.text(0.063, 2e4, "random s*≈0.06", fontsize=8, color="0.35")
    ax.text(0.0105, 3.2e4, "near s*≈0.012", fontsize=8, color="0.35")
    ax.text(0.07, 3.3e4, "far: no HNSW win below s=1", fontsize=8, color="0.35")

    ax = axes[1]
    for rows, label, color, marker, ef in [
        (rnd, "random, tuned ef", "C0", "o", None),
        (clu, "far, ef=16384", "C1", "s", 16384),
        (probe, "far, ef=32768", "C1", "^", 32768),
        (near, "near, ef=32", "C2", "D", None),
    ]:
        pts = series(rows, "hnsw", "recall", ef=ef)
        if not pts:
            continue
        ax.plot([p[0] for p in pts], [p[1] for p in pts], marker=marker,
                color=color, label=label)
    ax.axhline(0.95, color="0.4", linestyle=":", linewidth=1)
    ax.text(0.0012, 0.955, "Recall@10 = 0.95", fontsize=8, color="0.35")
    ax.set_xscale("log")
    ax.set_xlabel("filter selectivity s")
    ax.set_ylabel("Recall@10")
    ax.set_ylim(0, 1.02)
    ax.set_title("HNSW recall: far vs near")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower right")

    fig.tight_layout()
    for ext in ("svg", "png"):
        fig.savefig(OUT / f"selectivity_correlation_compare.{ext}", dpi=150)
    print(f"wrote {OUT}/selectivity_correlation_compare.svg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
