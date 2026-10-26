#!/usr/bin/env python3
"""Figures for the Directory-Aware (arXiv:2606.16903) reproduction.

Reads the reproduction CSVs and writes figures next to the report:
  fig_repro_dir_latency.{png,svg}   — Table IV analog: directory-only latency
  fig_repro_ndcg_buckets.{png,svg}  — wiki nDCG by scope cardinality bucket
  fig_repro_dsm.{png,svg}           — Fig. 9 analog: DSM wall-clock latency
  fig_repro_dir_size.{png,svg}      — directory-module size vs PE-Online
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

DESIGNS = ["PE-Online", "PE-Offline", "TrieHI"]
COLORS = {"PE-Online": "tab:blue", "PE-Offline": "tab:orange", "TrieHI": "tab:green"}


def load(path: Path) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def save(fig, out: Path, name: str):
    for ext in ("png", "svg"):
        fig.savefig(out / f"{name}.{ext}", bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out}/{name}.{{png,svg}}")


def fig_dir_latency(rows, out: Path):
    groups = [("wiki", "recursive"), ("wiki", "non-recursive"),
              ("arxiv", "recursive"), ("arxiv", "non-recursive")]
    fig, ax = plt.subplots(figsize=(8, 4.5))
    width = 0.26
    xticks, xlabels = [], []
    for gi, (ds, qt) in enumerate(groups):
        for di, d in enumerate(DESIGNS):
            r = next(x for x in rows
                     if x["dataset"] == ds and x["design"] == d
                     and x["query_type"] == qt and x["scope_bucket"] == "all"
                     and x["executor"].startswith("PG"))
            v = float(r["dir_mean_us"])
            x = gi + (di - 1) * width
            ax.bar(x, v, width, color=COLORS[d], label=d if gi == 0 else None)
            ax.text(x, v * 1.15, f"{v:.0f}" if v >= 10 else f"{v:.1f}",
                    ha="center", fontsize=7)
        xticks.append(gi)
        xlabels.append(f"{ds}\n{qt}")
    ax.set_yscale("log")
    ax.set_xticks(xticks)
    ax.set_xticklabels(xlabels)
    ax.set_ylabel("directory-only latency (µs, mean, log)")
    ax.set_title("DSQ scope resolution: PE-Online expansion vs materialized sets")
    ax.legend()
    ax.grid(True, axis="y", which="both", alpha=0.3)
    save(fig, out, "fig_repro_dir_latency")


def fig_ndcg_buckets(rows, out: Path):
    buckets = ["card<10", "card10-1000", "card>=1000"]
    execs = ["PG-hnsw-M16", "IVF-nlist1024"]
    fig, ax = plt.subplots(figsize=(7, 4.5))
    width = 0.35
    for ei, ex in enumerate(execs):
        vals = []
        for b in buckets:
            r = next(x for x in rows
                     if x["dataset"] == "wiki" and x["design"] == "TrieHI"
                     and x["query_type"] == "recursive"
                     and x["scope_bucket"] == b and x["executor"] == ex)
            vals.append(float(r["ndcg10"]))
        xs = [i + (ei - 0.5) * width for i in range(len(buckets))]
        ax.bar(xs, vals, width, label=ex, color=["tab:blue", "tab:red"][ei])
        for x, v in zip(xs, vals):
            ax.text(x, v + 0.02, f"{v:.2f}", ha="center", fontsize=8)
    ax.set_ylim(0, 1.1)
    ax.set_xticks(range(len(buckets)))
    ax.set_xticklabels(buckets)
    ax.set_ylabel("nDCG@10 (wiki, recursive)")
    ax.set_title("Filtered-ANN quality collapses only at extreme low selectivity")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    save(fig, out, "fig_repro_ndcg_buckets")


def fig_dsm(rows, out: Path):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    width = 0.26
    for di, d in enumerate(DESIGNS):
        for oi, op in enumerate(["MOVE", "MERGE"]):
            r = next(x for x in rows if x["design"] == d and x["op"] == op)
            x = oi + (di - 1) * width
            ax.bar(x, float(r["mean_ms"]), width, color=COLORS[d],
                   label=d if oi == 0 else None)
            ax.text(x, float(r["mean_ms"]) * 1.15,
                    f"{float(r['mean_ms']):.2f}", ha="center", fontsize=8)
    top = max(float(r["mean_ms"]) for r in rows) * 3
    ax.set_ylim(top=top)
    ax.set_yscale("log")
    ax.set_xticks([0, 1])
    ax.set_xticklabels(["MOVE", "MERGE"])
    ax.set_ylabel("wall-clock (ms, mean, log)")
    ax.set_title("DSM: TrieHI subtree relinking vs expansion-based rewriting")
    ax.legend()
    ax.grid(True, axis="y", which="both", alpha=0.3)
    save(fig, out, "fig_repro_dsm")


def fig_dir_size(rows, out: Path):
    fig, ax = plt.subplots(figsize=(6, 4))
    vals = []
    for d in DESIGNS:
        # wiki module rows are the first three (largest)
        r = max((x for x in rows if x["design"] == d), key=lambda x: float(x["size_mb"]))
        vals.append(float(r["size_mb"]))
    ax.bar(DESIGNS, vals, color=[COLORS[d] for d in DESIGNS])
    for i, v in enumerate(vals):
        ax.text(i, v + 0.05, f"{v:.2f}", ha="center", fontsize=9)
    ax.set_ylabel("directory module size (MB, serialized)")
    ax.set_title("Directory-module storage vs design (wiki namespace)")
    ax.grid(True, axis="y", alpha=0.3)
    save(fig, out, "fig_repro_dir_size")


def main():
    ap = argparse.ArgumentParser()
    repro = (Path(__file__).resolve().parents[3] / "researches" /
             "nested-attr-vector-search" / "01-directory-aware-acceleration" /
             "2026-10-26-exps" / "repro-2606.16903")
    ap.add_argument("--data", type=Path, default=repro / "data")
    ap.add_argument("--out", type=Path, default=repro / "figures")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    dsq = load(args.data / "results_dsq.csv")
    dsm = load(args.data / "results_dsm.csv")
    size = load(args.data / "results_size.csv")
    fig_dir_latency(dsq, args.out)
    fig_ndcg_buckets(dsq, args.out)
    fig_dsm(dsm, args.out)
    fig_dir_size(size, args.out)


if __name__ == "__main__":
    main()
