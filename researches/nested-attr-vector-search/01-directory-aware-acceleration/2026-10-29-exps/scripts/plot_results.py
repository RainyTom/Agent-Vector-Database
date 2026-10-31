#!/usr/bin/env python3
"""Figures for the 2026-10-29 NodeANN-first hnswlib report (clean-serial only).

Frontier panels use serial re-measurements; parallel-contended runs are
archived in data/frontier_agg_pass1.csv and never drawn here.
"""
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
FIG = ROOT / "figures"
CLEAN_RUNS = ROOT / "work/nodeann_matrix_clean/runs"

DATASETS = ["wiki", "d4_balanced", "d4_skewed", "d6_balanced", "d6_skewed",
            "d12_balanced", "d12_skewed"]
TITLES = {"wiki": "WIKI 1.94M×1024", "d4_balanced": "SIFT D4 bal",
          "d4_skewed": "SIFT D4 skew", "d6_balanced": "SIFT D6 bal",
          "d6_skewed": "SIFT D6 skew", "d12_balanced": "SIFT D12 bal",
          "d12_skewed": "SIFT D12 skew"}


def load_agg(path):
    rows = list(csv.DictReader(open(path)))
    for r in rows:
        r["ef"] = int(r["ef"])
        for k in ("median_mean_e2e_ms", "median_p95_e2e_ms",
                  "median_macro_recall", "median_build_wall_s",
                  "median_peak_rss_bytes", "median_total_distance_calls"):
            r[k] = float(r[k])
    return rows


def fig1_frontier(agg):
    """Clean frontier: full ef curves where measured serially; ef=64 clean
    points otherwise. B3 exact shown as a vertical diagnostic line."""
    by = {}
    for r in agg:
        by.setdefault((r["dataset"], r["method"]), []).append(r)
    fig, axes = plt.subplots(2, 4, figsize=(14.5, 6.6))
    axes = axes.ravel()
    for i, ds in enumerate(DATASETS):
        ax = axes[i]
        for method, color, marker in (("b0", "#d62728", "s"), ("b1", "#1f77b4", "o")):
            pts = sorted([p for p in by.get((ds, method), [])
                          if p.get("source") == "clean"], key=lambda r: r["ef"])
            if not pts:
                continue
            if len(pts) > 2:  # full clean curve
                ax.plot([p["median_mean_e2e_ms"] for p in pts],
                        [p["median_macro_recall"] for p in pts],
                        marker=marker, ms=4.5, lw=1.3, color=color,
                        label=method.upper())
                for p in pts:
                    ax.annotate(f'{p["ef"]}',
                                (p["median_mean_e2e_ms"], p["median_macro_recall"]),
                                fontsize=5.5, xytext=(0, 4), textcoords="offset points")
            else:  # single clean ef=64 reference point
                for p in pts:
                    ax.plot([p["median_mean_e2e_ms"]], [p["median_macro_recall"]],
                            marker=marker, ms=8, color=color, mfc="none", mew=2,
                            label=method.upper() + " ef=64")
        b3 = by.get((ds, "b3"), [])
        if b3:
            x = b3[0]["median_mean_e2e_ms"]
            ax.axvline(x, color="#7f7f7f", ls=":", lw=1.2)
            ax.text(x * 1.05, 0.9953, "B3 exact (diagnostic)", fontsize=6,
                    color="#555555", va="bottom")
        ax.set_title(TITLES[ds], fontsize=9)
        ax.set_xscale("log")
        from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
        ax.xaxis.set_major_locator(LogLocator(base=10.0, numticks=4))
        ax.xaxis.set_minor_formatter(NullFormatter())
        ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:.3g}"))
        ax.set_ylim(0.995, 1.0006)
        ax.set_xlabel("mean e2e ms (log)", fontsize=8)
        ax.set_ylabel("macro recall", fontsize=8)
        ax.grid(True, which="both", alpha=0.2)
        ax.legend(fontsize=6.5, loc="lower left")
    axes[-1].axis("off")
    fig.suptitle("NodeANN on hnswlib — serial measurements, single-thread query, "
                 "shared RawL2Space, M16/efC200. WIKI B0 infeasible (see report §D1).",
                 fontsize=10)
    fig.tight_layout(rect=[0, 0.02, 1, 0.95])
    fig.savefig(FIG / "fig1_frontier.png", dpi=160)
    plt.close(fig)


def fig2_tscan():
    def rd(p):
        return [(int(r["n"]), float(r["scan_ns"]), float(r["hnsw_ns"]))
                for r in csv.DictReader(open(p))]
    d128 = sorted(rd(DATA / "tscan_bench_d128.csv") + rd(DATA / "tscan_bench_d128_ext.csv"))
    d1024 = sorted(rd(DATA / "tscan_bench_d1024.csv") + rd(DATA / "tscan_bench_d1024_ext.csv"))
    fig, axes = plt.subplots(1, 2, figsize=(9.5, 3.6))
    for ax, data, dim, t in ((axes[0], d128, 128, 1024), (axes[1], d1024, 1024, 512)):
        n = [x[0] for x in data]
        ax.plot(n, [x[1] for x in data], "o-", label="RawL2Space scan", color="#1f77b4")
        ax.plot(n, [x[2] for x in data], "s-", label="hnswlib searchKnn (ef=64)", color="#d62728")
        ax.axvline(t, color="#2ca02c", ls="--", lw=1)
        ymax = max(x[2] for x in data)
        ax.text(t * 1.15, ymax * 0.5, f"frozen T={t}", color="#2ca02c", fontsize=8)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_title(f"d={dim}", fontsize=9)
        ax.set_xlabel("local node size n", fontsize=8)
        ax.set_ylabel("ns / query (median)", fontsize=8)
        ax.grid(True, which="both", alpha=0.2)
        ax.legend(fontsize=7)
    fig.suptitle("T_scan microbenchmark: exact scan beats per-node HNSW below the crossover",
                 fontsize=10)
    fig.tight_layout(rect=[0, 0, 1, 0.93])
    fig.savefig(FIG / "fig2_tscan.png", dpi=160)
    plt.close(fig)


def fig3_cost(agg):
    """ef=64 serial build wall / peak RSS / distance calls: B0 vs B1."""
    sel = [r for r in agg if r["ef"] == 64 and r["method"] in ("b0", "b1")]
    by = {(r["dataset"], r["method"]): r for r in sel}
    dss = [d for d in DATASETS if (d, "b0") in by and (d, "b1") in by]
    x = list(range(len(dss)))
    fig, axes = plt.subplots(1, 3, figsize=(14, 3.9))
    specs = [("median_build_wall_s", "build wall s (log)", 1.0),
             ("median_peak_rss_bytes", "peak RSS MiB (log)", 2**-20),
             ("median_total_distance_calls", "distance calls / 1000 queries (log)", 1.0)]
    for ax, (field, ylabel, scale) in zip(axes, specs):
        for j, (method, color) in enumerate((("b0", "#d62728"), ("b1", "#1f77b4"))):
            vals = [by[(d, method)][field] * scale for d in dss]
            bars = ax.bar([i + j * 0.38 for i in x], vals, width=0.36,
                          color=color, label=method.upper(), alpha=0.85)
            for b, v in zip(bars, vals):
                ax.text(b.get_x() + b.get_width() / 2, v * 1.1,
                        f"{v:.2g}", ha="center", fontsize=6)
        ax.set_xticks([i + 0.19 for i in x])
        ax.set_xticklabels([TITLES[d].replace("SIFT ", "") for d in dss], fontsize=7, rotation=15)
        ax.set_yscale("log")
        ax.set_ylabel(ylabel, fontsize=8)
        ax.grid(True, axis="y", alpha=0.2)
        ax.legend(fontsize=7)
    fig.suptitle("Index-side cost at ef=64 (serial): B1 cuts RSS 10–34× and build 2.8–6.1× on "
                 "balanced; on skewed both build the same giant nodes", fontsize=10)
    fig.tight_layout(rect=[0, 0, 1, 0.92])
    fig.savefig(FIG / "fig3_index_cost.png", dpi=160)
    plt.close(fig)


def fig4_decomposition():
    """Per-query time decomposition from clean runs + WIKI scope-bucket latency."""
    import statistics as st
    from collections import defaultdict

    def load(ds, label):
        p = CLEAN_RUNS / ds / label / "run.perquery.csv"
        return list(csv.DictReader(open(p)))

    panels = [("wiki", "b1_ef64_rep1", "WIKI B1 ef64"),
              ("d6_balanced", "b0_ef64_rep1", "SIFT D6 bal B0 ef64"),
              ("d6_balanced", "b1_ef64_rep1", "SIFT D6 bal B1 ef64")]
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.0))
    ax = axes[0]
    keys = [("dir_us", "dir enum"), ("scan_us", "small-node scan"),
            ("hnsw_us", "HNSW search"), ("merge_us", "merge")]
    colors = ["#7f7f7f", "#1f77b4", "#d62728", "#2ca02c"]
    bottoms = [0.0] * len(panels)
    for (field, name), color in zip(keys, colors):
        vals = []
        for ds, label, _ in panels:
            rows = load(ds, label)
            total = sum(float(r["e2e_ms"]) * 1000 for r in rows)
            vals.append(sum(float(r[field]) for r in rows) / total * 100)
        ax.bar([p[2] for p in panels], vals, bottom=bottoms, label=name, color=color)
        bottoms = [b + v for b, v in zip(bottoms, vals)]
    ax.set_ylabel("share of total query time (%)", fontsize=8)
    ax.tick_params(axis="x", labelsize=7)
    ax.legend(fontsize=7)
    ax.set_title("time decomposition (clean runs)", fontsize=9)

    ax = axes[1]
    rows = load("wiki", "b1_ef64_rep1")
    rows_b3 = load("wiki", "b3_exact")
    def buckets(rows):
        out = defaultdict(list)
        for r in rows:
            sc = int(r["scope_card"])
            b = ("0" if sc == 0 else "1–9" if sc < 10 else "10–99" if sc < 100
                 else "100–999" if sc < 1000 else "1k–10k" if sc < 10000 else "≥10k")
            out[b].append(float(r["e2e_ms"]) * 1000)
        return out
    order = ["0", "1–9", "10–99", "100–999", "1k–10k", "≥10k"]
    b1, b3 = buckets(rows), buckets(rows_b3)
    xs = list(range(len(order)))
    ax.bar([i - 0.2 for i in xs], [st.mean(b1[o]) for o in order], width=0.38,
           label="B1 ef64", color="#1f77b4")
    ax.bar([i + 0.2 for i in xs], [st.mean(b3[o]) for o in order], width=0.38,
           label="B3 exact", color="#7f7f7f")
    for i, o in enumerate(order):
        ax.text(i - 0.2, st.mean(b1[o]) * 1.15, f"n={len(b1[o])}", ha="center", fontsize=6)
    ax.set_xticks(xs)
    ax.set_xticklabels(order, fontsize=8)
    ax.set_yscale("log")
    ax.set_ylabel("mean µs / query (log)", fontsize=8)
    ax.set_title("WIKI latency by scope bucket", fontsize=9)
    ax.legend(fontsize=7)
    ax.grid(True, axis="y", alpha=0.2)
    fig.tight_layout()
    fig.savefig(FIG / "fig4_decomposition.png", dpi=160)
    plt.close(fig)


def main():
    FIG.mkdir(exist_ok=True)
    agg = load_agg(DATA / "frontier_agg_clean.csv")
    fig1_frontier(agg)
    fig2_tscan()
    fig3_cost(agg)
    fig4_decomposition()
    print(f"wrote figures to {FIG}")


if __name__ == "__main__":
    main()
