#!/usr/bin/env python3
"""Plot the selectivity sweep: QPS, required ef, and fixed-ef recall."""

from __future__ import annotations

import csv
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "production" / "nested-attr-vector-search" / "work"
OUT = (ROOT / "researches" / "nested-attr-vector-search" /
       "01-directory-aware-acceleration" / "2026-10-26-exps" / "figures")


def load(path: Path) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def series(rows, method, key, variant="precomputed"):
    pts = []
    for r in rows:
        if r["mode"] == "random" and r["method"] == method and r["variant"] == variant:
            pts.append((float(r["s"]), float(r[key]), int(r["ef"]), r["ok"] == "1"))
    pts.sort()
    return pts


def fit_power(xs, ys):
    lx = [math.log(x) for x in xs]
    ly = [math.log(y) for y in ys]
    n = len(xs)
    mx = sum(lx) / n
    my = sum(ly) / n
    b = sum((x - mx) * (y - my) for x, y in zip(lx, ly)) / sum(
        (x - mx) ** 2 for x in lx)
    a = math.exp(my - b * mx)
    return a, b


def main() -> int:
    rows = load(WORK / "selectivity" / "selectivity.csv")
    OUT.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.4))

    ax = axes[0]
    for method, label, marker in [
        ("exact_list", "exact feasible scan (list+prefetch)", "o"),
        ("hnsw", "BF-HNSW (tuned ef)", "s"),
        ("post", "post-filter (expanding ef)", "^"),
    ]:
        pts = series(rows, method, "qps")
        ok = [(s, q) for s, q, _, good in pts if good]
        bad = [(s, q) for s, q, _, good in pts if not good]
        ax.plot([p[0] for p in ok], [p[1] for p in ok], marker=marker,
                label=label, linewidth=1.8)
        if bad:
            ax.scatter([p[0] for p in bad], [p[1] for p in bad], marker=marker,
                       facecolors="none", edgecolors="0.4", label=f"{label} (r95 fail)")
    ax.axvline(0.0605, color="0.6", linestyle=":", linewidth=1)
    ax.text(0.063, 3e4, "cross-over\ns≈0.06", fontsize=9, color="0.35")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("filter selectivity s = |feasible| / n")
    ax.set_ylabel("QPS at Recall@10 ≥ 0.95")
    ax.set_title("Throughput vs selectivity (SIFT100k)")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower left")

    ax = axes[1]
    pts = series(rows, "hnsw", "qps")
    ef_pts = [(s, ef) for s, _, ef, _ in pts]
    ax.plot([p[0] for p in ef_pts], [p[1] for p in ef_pts], "s-", color="C1")
    a, b = fit_power([p[0] for p in ef_pts], [p[1] for p in ef_pts])
    xs = [p[0] for p in ef_pts]
    ax.plot(xs, [a * x ** b for x in xs], "k--", linewidth=1,
            label=f"fit ef ≈ {a:.2f}·s^{b:.2f}")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("filter selectivity s")
    ax.set_ylabel("efSearch needed for Recall@10 ≥ 0.95")
    ax.set_title("Search breadth required")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=9)

    ax = axes[2]
    for ef in (64, 256, 1024):
        pts = [(float(r["s"]), float(r["recall"]))
               for r in rows
               if r["method"] == "hnsw_fixed" and int(r["ef"]) == ef]
        pts.sort()
        ax.plot([p[0] for p in pts], [p[1] for p in pts], "o-", label=f"ef={ef}")
    ax.axhline(0.95, color="0.4", linestyle=":", linewidth=1)
    ax.text(0.0011, 0.955, "Recall@10 = 0.95", fontsize=8, color="0.35")
    ax.set_xscale("log")
    ax.set_xlabel("filter selectivity s")
    ax.set_ylabel("Recall@10 (fixed ef)")
    ax.set_ylim(0, 1.02)
    ax.set_title("Recall collapse at fixed breadth")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=9)

    fig.tight_layout()
    for ext in ("svg", "png"):
        fig.savefig(OUT / f"selectivity_curve.{ext}", dpi=150)
    print(f"wrote {OUT}/selectivity_curve.svg")
    print(f"ef fit: ef = {a:.3f} * s^{b:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
