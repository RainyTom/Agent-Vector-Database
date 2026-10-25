#!/usr/bin/env python3
"""Compare the 100k and 1M selectivity curves."""

from __future__ import annotations

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


def load(path: Path, scale: str) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        rows = [r for r in csv.DictReader(f) if r["mode"] == "random"]
    for r in rows:
        r["scale"] = scale
    return rows


def pts(rows, method, key, only_ok=False):
    out = []
    for r in rows:
        if r["method"] != method:
            continue
        if only_ok and r["ok"] != "1":
            continue
        out.append((float(r["s"]), float(r[key]), int(r["ef"]), r["ok"] == "1"))
    return sorted(out)


def gated_hnsw(rows):
    """Per bucket: tuned row if gated, else the smallest fixed ef that is gated."""
    out = []
    for bucket in sorted({r["bucket"] for r in rows}, key=float):
        tuned = [r for r in rows
                 if r["bucket"] == bucket and r["method"] == "hnsw"][0]
        if tuned["ok"] == "1":
            out.append((float(tuned["s"]), float(tuned["qps"]),
                        int(tuned["ef"]), True))
            continue
        fixed = [r for r in rows
                 if r["bucket"] == bucket and r["method"] == "hnsw_fixed"
                 and r["ok"] == "1"]
        if fixed:
            best = min(fixed, key=lambda r: int(r["ef"]))
            out.append((float(best["s"]), float(best["qps"]),
                        int(best["ef"]), True))
        else:
            out.append((float(tuned["s"]), float(tuned["qps"]),
                        int(tuned["ef"]), False))
    return out


def required_ef(rows):
    out = []
    for s, _, ef, gated in gated_hnsw(rows):
        out.append((s, ef if gated else 8192))
    return out


def fit(rows):
    p = required_ef(rows)
    lx = [math.log(x) for x, _ in p]
    ly = [math.log(y) for _, y in p]
    n = len(p)
    mx, my = sum(lx) / n, sum(ly) / n
    b = sum((x - mx) * (y - my) for x, y in zip(lx, ly)) / sum(
        (x - mx) ** 2 for x in lx)
    return math.exp(my - b * mx), b


def main() -> int:
    r100 = load(DATA / "selectivity_curve_20261026.csv", "100k")
    r1m = load(DATA / "selectivity_1m_curve_20261026.csv", "1M")
    fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.5))
    styles = {"100k": ("C0", "o"), "1M": ("C1", "s")}
    for rows, scale in ((r100, "100k"), (r1m, "1M")):
        color, marker = styles[scale]
        ex = pts(rows, "exact_list", "qps")
        axes[0].plot([p[0] for p in ex], [p[1] for p in ex], marker=marker,
                     color=color, linestyle="-", label=f"exact list, {scale}")
        hn = gated_hnsw(rows)
        ok = [p for p in hn if p[3]]
        bad = [p for p in hn if not p[3]]
        axes[0].plot([p[0] for p in ok], [p[1] for p in ok], marker=marker,
                     color=color, linestyle="--", label=f"BF-HNSW@r95, {scale}")
        if bad:
            axes[0].scatter([p[0] for p in bad], [p[1] for p in bad],
                            marker=marker, facecolors="none", edgecolors=color)
        e = required_ef(rows)
        a, b = fit(rows)
        axes[1].plot([p[0] for p in e], [p[1] for p in e], marker=marker,
                     color=color, linestyle="-",
                     label=f"{scale}: ef={a:.1f}·s^{b:.2f}")

    for ax, ylabel in ((axes[0], "QPS"), (axes[1], "efSearch for r95")):
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlabel("filter selectivity s = |feasible| / n")
        ax.set_ylabel(ylabel)
        ax.grid(True, which="both", alpha=0.25)
        ax.legend(fontsize=8)
    axes[0].axvline(0.0605, color="0.5", linestyle=":", linewidth=1)
    axes[0].axvline(0.0194, color="0.5", linestyle=":", linewidth=1)
    axes[0].text(0.066, 1.2e4, "100k s*≈0.06", fontsize=8, color="0.35")
    axes[0].text(0.021, 2.6e4, "1M s*≈0.019", fontsize=8, color="0.35")
    axes[0].set_title("Throughput: crossover moves left with n")
    axes[1].set_title("Search breadth grows as s falls")
    fig.tight_layout()
    for ext in ("svg", "png"):
        fig.savefig(OUT / f"selectivity_scale_compare.{ext}", dpi=150)
    print(f"wrote {OUT}/selectivity_scale_compare.svg")
    for rows, scale in ((r100, "100k"), (r1m, "1M")):
        a, b = fit(rows)
        print(f"{scale}: ef = {a:.2f} * s^{b:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
