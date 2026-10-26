#!/usr/bin/env python3
"""Compare the 100k and 1M motivation curves on the same 1%-30% grid."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
DATA = (ROOT / "researches" / "nested-attr-vector-search" /
        "01-directory-aware-acceleration" / "2026-10-26-exps" / "data")
OUT = DATA.parent / "figures"


def load(path: Path) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return [r for r in csv.DictReader(f) if r["mode"] == "random"]


def series(rows, method, key, ok_only=False):
    out = []
    for r in rows:
        if r["method"] != method:
            continue
        if ok_only and r["ok"] != "1":
            continue
        out.append((float(r["s"]), float(r[key]), int(r["ef"]), r["ok"] == "1"))
    return sorted(out)


def crossover(rows):
    ex = {s: q for s, q, _, _ in series(rows, "exact_list", "qps")}
    hn = {s: q for s, q, _, _ in series(rows, "hnsw", "qps")}
    ss = sorted(set(ex) & set(hn))
    for i in range(len(ss) - 1):
        s1, s2 = ss[i], ss[i + 1]
        if hn[s1] < ex[s1] and hn[s2] > ex[s2]:
            lx1, lx2 = math.log(s1), math.log(s2)
            a1, a2 = math.log(ex[s1]), math.log(ex[s2])
            b1, b2 = math.log(hn[s1]), math.log(hn[s2])
            t = (b1 - a1) / ((a2 - a1) - (b2 - b1))
            return math.exp(lx1 + t * (lx2 - lx1))
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv-100k", type=Path,
                    default=DATA / "selectivity_grid_ext_curve_20261026.csv")
    ap.add_argument("--csv-1m", type=Path,
                    default=DATA / "selectivity_grid_ext_1m_curve_20261026.csv")
    ap.add_argument("--out", type=Path,
                    default=OUT / "selectivity_motivation_scale")
    args = ap.parse_args()
    r100 = load(args.csv_100k)
    r1m = load(args.csv_1m)
    c100 = crossover(r100)
    c1m = crossover(r1m)

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.6))
    ax = axes[0]
    styles = {"100k": ("C0", "o"), "1M": ("C1", "s")}
    for rows, scale in ((r100, "100k"), (r1m, "1M")):
        color, marker = styles[scale]
        ex = series(rows, "exact_list", "qps")
        ax.plot([p[0] for p in ex], [p[1] for p in ex], marker=marker,
                color=color, linestyle="-", label=f"exact list, {scale}")
        hn = series(rows, "hnsw", "qps")
        ok = [p for p in hn if p[3]]
        bad = [p for p in hn if not p[3]]
        ax.plot([p[0] for p in ok], [p[1] for p in ok], marker=marker,
                color=color, linestyle="--", label=f"BF-HNSW@r95, {scale}")
        if bad:
            ax.scatter([p[0] for p in bad], [p[1] for p in bad], marker=marker,
                       facecolors="none", edgecolors=color)
    if c100 is not None:
        ax.axvline(c100, color="0.6", linestyle=":", linewidth=1)
        ax.text(c100 * 1.05, 2e4, f"100k s*≈{c100:.3f}", fontsize=8,
                color="0.35")
    if c1m is not None:
        ax.axvline(c1m, color="0.6", linestyle=":", linewidth=1)
        ax.text(c1m * 1.05, 1.5e4, f"1M s*≈{c1m:.3f}", fontsize=8,
                color="0.35")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("filter selectivity s = |feasible| / n")
    ax.set_ylabel("QPS at Recall@10 ≥ 0.95")
    ax.set_title("Same grid: crossover moves left with n")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower left")

    ax = axes[1]
    for ef in (64, 256):
        for rows, scale, marker in ((r100, "100k", "o"), (r1m, "1M", "s")):
            pts = [(float(r["s"]), float(r["recall"]))
                   for r in rows
                   if r["method"] == "hnsw_fixed" and int(r["ef"]) == ef]
            pts.sort()
            color = styles[scale][0]
            linestyle = "-" if ef == 64 else "--"
            label = f"ef={ef}, {scale}"
            ax.plot([p[0] for p in pts], [p[1] for p in pts],
                    marker=marker, color=color, linestyle=linestyle,
                    label=label)
    ax.axhline(0.95, color="0.4", linestyle=":", linewidth=1)
    ax.text(0.012, 0.955, "Recall@10 = 0.95", fontsize=8, color="0.35")
    ax.set_xscale("log")
    ax.set_xlabel("filter selectivity s")
    ax.set_ylabel("Recall@10")
    ax.set_ylim(0, 1.02)
    ax.set_title("Recall collapse is sharper at 1M")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=7, loc="lower right")

    fig.tight_layout()
    for ext in ("svg", "png"):
        fig.savefig(f"{args.out}.{ext}", dpi=150)
    print(f"wrote {args.out}.svg")
    print(f"crossover 100k={c100:.4f} 1M={c1m:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
