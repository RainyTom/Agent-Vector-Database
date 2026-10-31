#!/usr/bin/env python3
"""Figures for workstream 2 (summary reclaim) and workstream 1 (concurrency)."""
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"


def fig_summary():
    rows = list(csv.DictReader(open(DATA / "summary_reclaim_curve.csv")))
    # keep conservative epoch mode only (aggressive misses most truth)
    sel = [r for r in rows if r["mode"] in ("or", "conservative", "")]
    fig, axes = plt.subplots(1, 2, figsize=(10.5, 3.9))
    styles = {("wiki", "0.0"): ("#1f77b4", "wiki ρ=0"), ("wiki", "0.8"): ("#2ca02c", "wiki ρ=0.8"),
              ("d4_balanced", "0.8"): ("#d62728", "D4bal ρ=0.8")}
    for key, (color, label) in styles.items():
        rs = sorted([r for r in sel if r["dataset"] == key[0] and r["rho"] == key[1]
                     and r["semantics"] == "or"], key=lambda r: float(r["churn"]))
        if not rs:
            continue
        x = [float(r["churn"]) * 100 for r in rs]
        axes[0].plot(x, [float(r["fp_all"]) for r in rs], "o-", ms=4, color=color, label=label)
        ratio = [float(r["probe_ratio_over_count"]) for r in rs]
        axes[1].plot(x, ratio, "s-", ms=4, color=color, label=label)
    axes[0].set_xlabel("churn (%)")
    axes[0].set_ylabel("stale-bit fraction (monotone OR)")
    axes[0].set_title("false positives grow with churn", fontsize=9)
    axes[1].set_xlabel("churn (%)")
    axes[1].set_ylabel("probe cost: OR / exact-count")
    axes[1].axhline(1.0, color="#7f7f7f", ls=":", lw=1)
    axes[1].set_title("but the paid penalty is ~1.2×, not 1.4× (D4bal: none)", fontsize=9)
    for ax in axes:
        ax.grid(alpha=0.25)
        ax.legend(fontsize=7)
    fig.suptitle("attribute summaries under churn: stale bits ≠ query cost "
                 "(synthetic attributes, 200 predicates, fixed seed)", fontsize=9)
    fig.tight_layout(rect=[0, 0, 1, 0.93])
    fig.savefig(ROOT / "figures/fig6_summary_reclaim.png", dpi=160)
    plt.close(fig)


def fig_concurrency():
    files = ["conc5_wiki_add-del_v0", "conc5_wiki_add-del_v1",
             "conc5_wiki_del-add_v0", "conc5_wiki_del-add_v1",
             "conc_hot_wiki_add-del_v0", "conc_hot_wiki_add-del_v1",
             "conc_hot_wiki_del-add_v0", "conc_hot_wiki_del-add_v1",
             "conc_hot_wiki_control"]
    labels, expo, lat = [], [], []
    for f in files:
        p = DATA / f"{f}.csv"
        if not p.is_file():
            continue
        r = next(csv.DictReader(open(p)))
        proto = "add-del" if "add-del" in f else "del-add"
        scope = "hot16" if "hot" in f else "uniform"
        v = "validate" if f.endswith("v1") else "raw"
        labels.append(f"{proto}\n{scope}/{v}" + ("\n(no updates)" if "control" in f else ""))
        expo.append(max(1e-9, float(r["exposure_ns_per_op"])))
        lat.append(float(r["lat_us_per_query"]))
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.2))
    xs = range(len(labels))
    colors = ["#1f77b4" if "add-del" in l else "#d62728" for l in labels]
    axes[0].bar(xs, expo, color=colors)
    axes[0].set_yscale("log")
    axes[0].set_ylabel("anomaly window (ns per move, log)")
    axes[0].set_title("delete-then-add window is 60–115× larger", fontsize=9)
    axes[1].bar(xs, lat, color=colors)
    axes[1].set_ylabel("query latency (µs)")
    axes[1].set_title("query latency: node size dominates, locking adds 21–43%", fontsize=9)
    for ax in axes:
        ax.set_xticks(list(xs))
        ax.set_xticklabels(labels, fontsize=6, rotation=25, ha="right")
        ax.grid(alpha=0.25, axis="y")
    fig.suptitle("NodeANN local indexes under concurrent attribute moves "
                 "(WIKI, 1,142 indexed nodes, ef=64, fan-out 4)", fontsize=9)
    fig.tight_layout(rect=[0, 0, 1, 0.92])
    fig.savefig(ROOT / "figures/fig7_concurrency.png", dpi=160)
    plt.close(fig)


def main():
    (ROOT / "figures").mkdir(exist_ok=True)
    fig_summary()
    fig_concurrency()
    print("wrote fig6, fig7")


if __name__ == "__main__":
    main()
