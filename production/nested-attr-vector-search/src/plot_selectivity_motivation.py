#!/usr/bin/env python3
"""Selectivity: exact scan, FAISS filtered IVF/HNSW, and post-filter."""

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


def fit_power(xs, ys):
    lx = [math.log(x) for x in xs]
    ly = [math.log(y) for y in ys]
    n = len(xs)
    mx, my = sum(lx) / n, sum(ly) / n
    b = sum((x - mx) * (y - my) for x, y in zip(lx, ly)) / sum(
        (x - mx) ** 2 for x in lx)
    return math.exp(my - b * mx), b


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", type=Path,
                    default=DATA / "selectivity_grid_ext_ivf_curve_20261026.csv")
    ap.add_argument("--out", type=Path,
                    default=OUT / "selectivity_motivation")
    ap.add_argument("--exclude", nargs="*", default=[],
                    help="method series to drop from the QPS panel")
    args = ap.parse_args()
    rows = load(args.csv)
    excluded = set(args.exclude)

    # Interpolate the first exact-to-valid-index crossing; not a universal threshold.
    ex = series(rows, "exact_list", "qps")
    hn = series(rows, "hnsw", "qps")
    ivf = series(rows, "ivf", "qps")
    exd = {s: q for s, q, _, _ in ex}
    hnd = {s: q for s, q, _, ok in hn if ok}
    ivd = {s: q for s, q, _, ok in ivf if ok}
    best = {s: max(hnd.get(s, 0), ivd.get(s, 0))
            for s in set(hnd) | set(ivd)}
    cross = None
    ss = sorted(set(exd) & set(best))
    for i in range(len(ss) - 1):
        s1, s2 = ss[i], ss[i + 1]
        if best[s1] < exd[s1] and best[s2] > exd[s2]:
            lx1, lx2 = math.log(s1), math.log(s2)
            a1, a2 = math.log(exd[s1]), math.log(exd[s2])
            b1, b2 = math.log(best[s1]), math.log(best[s2])
            t = (b1 - a1) / ((a2 - a1) - (b2 - b1))
            cross = math.exp(lx1 + t * (lx2 - lx1))
            break

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.6))
    ax = axes[0]
    for method, label, marker in [
        ("exact_list", "exact feasible scan (list)", "o"),
        ("ivf", "IVF + selector, nprobe tuned", "D"),
        ("hnsw", "BF-HNSW, ef tuned for r95", "s"),
        ("post", "post-filter, expanding ef", "^"),
    ]:
        if method in excluded:
            continue
        pts = series(rows, method, "qps")
        ok = [(s, q) for s, q, _, good in pts if good]
        bad = [(s, q) for s, q, _, good in pts if not good]
        ax.plot([p[0] for p in ok], [p[1] for p in ok], marker=marker,
                label=label, linewidth=1.8)
        if bad:
            ax.scatter([p[0] for p in bad], [p[1] for p in bad], marker=marker,
                       facecolors="none", edgecolors="0.4",
                       label=f"{label} (r95 fail)")
    if cross is not None and "exact_list" not in excluded:
        ax.axvline(cross, color="0.5", linestyle=":", linewidth=1)
        ax.text(cross * 1.05, 1.8e4, f"cross-over\ns≈{cross:.3f}", fontsize=9,
                color="0.35")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("filter selectivity s = |feasible| / n")
    ax.set_ylabel("QPS at Recall@10 ≥ 0.95")
    if "exact_list" in excluded:
        ax.set_title("SIFT100k: IVF vs BF-HNSW vs post-filter")
    else:
        ax.set_title("SIFT100k: exact vs IVF vs BF-HNSW vs post-filter")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower left")

    ax = axes[1]
    for ef in (16, 64, 256, 1024):
        pts = [(float(r["s"]), float(r["recall"]))
               for r in rows
               if r["method"] == "hnsw_fixed" and int(r["ef"]) == ef]
        pts.sort()
        ax.plot([p[0] for p in pts], [p[1] for p in pts], "o-",
                label=f"ef={ef}")
    ax.axhline(0.95, color="0.4", linestyle=":", linewidth=1)
    ax.text(0.012, 0.955, "Recall@10 = 0.95", fontsize=8, color="0.35")
    ax.set_xscale("log")
    ax.set_xlabel("filter selectivity s")
    ax.set_ylabel("Recall@10")
    ax.set_ylim(0, 1.02)
    ax.set_title("BF-HNSW recall at fixed search breadth")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower right")

    fig.tight_layout()
    for ext in ("svg", "png"):
        fig.savefig(f"{args.out}.{ext}", dpi=150)
    print(f"wrote {args.out}.svg")
    print(f"crossover = {cross:.4f}" if cross is not None else "no crossover")
    a, b = fit_power([s for s, _, _, _ in hn], [ef for _, _, ef, _ in hn])
    print(f"HNSW ef fit: ef = {a:.2f} * s^{b:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
