#!/usr/bin/env python3
"""Plot measured recall/QPS Pareto fronts under explicit index budgets.

Use --b1-data to convert archived B1 blocks, or --points for a future sweep.
Input CSV: method,config,recall,qps,qps_q25,qps_q75,index_bytes,cohort.
Exactly one cohort per invocation; missing/failed measurements are not imputed.
"""
import argparse
import csv
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def dominates(a, b, space=False):
    weak = a["recall"] >= b["recall"] and a["qps"] >= b["qps"]
    strict = a["recall"] > b["recall"] or a["qps"] > b["qps"]
    if space:
        weak = weak and a["index_bytes"] <= b["index_bytes"]
        strict = strict or a["index_bytes"] < b["index_bytes"]
    return weak and strict


def frontier(points, space=False):
    return [p for p in points if not any(dominates(q, p, space) for q in points)]


def from_b1(data):
    summary = json.loads((data / "b1_blocks_summary_20261020.json").read_text())
    rows = list(csv.DictReader((data / "b1_blocks_20261020.csv").open()))
    sizes = list(csv.DictReader((data / "five_l2_strict_summary_20261020.csv").open()))
    index_bytes = {
        "les": int(next(r["index_bytes"] for r in sizes if r["method"] == "LES-v3 varint E0")),
        "m12": int(next(r["index_bytes"] for r in sizes if r["method"] == "PSG-shared M12")),
        "m16": int(next(r["index_bytes"] for r in sizes if r["method"] == "PSG-shared M16")),
    }
    names = {"les": "LES-v3 E0", "m12": "PSG M12 shared", "m16": "PSG M16 shared"}
    points = []
    for config in sorted(summary["recall"]):
        lo, hi = summary["recall"][config]
        if lo != hi:
            raise ValueError(f"Recall varies across blocks: {config}")
        values = [float(r["qps"]) for r in rows if r["config"] == config]
        if len(values) != 15:
            raise ValueError(f"Expected 15 blocks: {config}")
        prefix = config.split("_")[0]
        policy = data / "b1_policies/b1_20261020" / (
            f"policy_{'les' if prefix == 'les' else 'psg_' + prefix}_{config[-3:]}.txt")
        extra = policy.stat().st_size if "_pol_" in config else 0
        points.append(dict(method=names[prefix], config=config, recall=lo,
                           qps=float(np.median(values)),
                           qps_q25=float(np.percentile(values, 25)),
                           qps_q75=float(np.percentile(values, 75)),
                           index_bytes=index_bytes[prefix] + extra,
                           cohort="b1_historical_holdout300_batchwall_15blocks",
                           space_note="archived index bytes plus policy file; not peak RSS"))
    return points


def read_points(path):
    points = list(csv.DictReader(path.open()))
    for p in points:
        for key in ("recall", "qps", "qps_q25", "qps_q75"):
            p[key] = float(p[key])
        p["index_bytes"] = int(p["index_bytes"])
    return points


def write_csv(path, rows):
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--b1-data", type=Path)
    source.add_argument("--points", type=Path)
    ap.add_argument("--out-prefix", required=True, type=Path)
    ap.add_argument("--budgets-mb", default="54,64")
    ap.add_argument("--title", default="Measured Pareto frontiers (parameter coverage incomplete)")
    args = ap.parse_args()
    points = from_b1(args.b1_data) if args.b1_data else read_points(args.points)
    if not points or len({p["cohort"] for p in points}) != 1:
        raise ValueError("Require nonempty points from one comparable cohort")
    if len({p["config"] for p in points}) != len(points):
        raise ValueError("Configuration identifiers must be unique")
    for p in points:
        vals = [p[k] for k in ("recall", "qps", "qps_q25", "qps_q75")]
        if (not all(np.isfinite(vals)) or not 0 <= p["recall"] <= 1
                or not 0 < p["qps_q25"] <= p["qps"] <= p["qps_q75"]
                or p["index_bytes"] <= 0):
            raise ValueError(f"Invalid measured point: {p}")
    budgets = [float(x) for x in args.budgets_mb.split(",")]
    if not all(np.isfinite(x) and x > 0 for x in budgets):
        raise ValueError("Budgets must be finite and positive")
    args.out_prefix.parent.mkdir(parents=True, exist_ok=True)
    write_csv(args.out_prefix.with_suffix(".points.csv"), points)
    methods = sorted({p["method"] for p in points})
    colors = dict(zip(methods, plt.get_cmap("tab10").colors))
    figures, axes = plt.subplots(1, len(budgets), figsize=(7 * len(budgets), 5.8),
                                 squeeze=False, sharex=True, sharey=True)
    report = []
    for ax, budget in zip(axes[0], budgets):
        eligible = [p for p in points if p["index_bytes"] <= budget * 1e6]
        joint = sorted(frontier(eligible), key=lambda p: p["recall"])
        for method in methods:
            group = [p for p in eligible if p["method"] == method]
            if not group:
                continue
            own = sorted(frontier(group), key=lambda p: p["recall"])
            color = colors[method]
            for p in group:
                ax.vlines(p["recall"], p["qps_q25"] / 1000, p["qps_q75"] / 1000,
                          color=color, alpha=.35, linewidth=1.2)
            ax.scatter([p["recall"] for p in group], [p["qps"] / 1000 for p in group],
                       edgecolor=color, facecolor="white", s=36, zorder=3)
            ax.plot([p["recall"] for p in own], [p["qps"] / 1000 for p in own],
                    "o-", color=color, linewidth=2, markersize=5, label=method)
        if joint:
            ax.plot([p["recall"] for p in joint], [p["qps"] / 1000 for p in joint],
                    "--", color="#222222", alpha=.8, label="Pooled measured frontier")
            ax.scatter([p["recall"] for p in joint], [p["qps"] / 1000 for p in joint],
                       s=115, facecolors="none", edgecolors="#222222", zorder=4)
        ax.set_title(f"Index budget <= {budget:g} MB (decimal)")
        ax.set_xlabel("Recall@10 (higher is better)")
        ax.grid(alpha=.2)
        ax.legend(loc="upper right", fontsize=8)
        for p in eligible:
            report.append(dict(budget_mb=budget, config=p["config"], method=p["method"],
                               recall=p["recall"], qps=p["qps"], index_bytes=p["index_bytes"],
                               pooled_frontier=p in joint,
                               method_frontier=p in frontier([q for q in eligible if q["method"] == p["method"]]),
                               frontier_3d=p in frontier(points, space=True)))
    axes[0, 0].set_ylabel("Throughput (thousand queries/s; median)")
    figures.suptitle(args.title, fontsize=13)
    figures.text(.5, .02,
                 "Circles: measured configurations. Vertical bars: timing IQR, not recall CI.\n"
                 "Lines connect measured nondominated points only; intermediate recall is NOT measured.\n"
                 + ("B1: 300 reused historical queries; no post-hoc runs. Budget is index bytes, not RSS."
                  if args.b1_data else
                  "One comparable cohort per plot. Budget is index bytes, not peak RSS."),
                 ha="center", fontsize=9)
    figures.tight_layout(rect=(0, .12, 1, .94))
    for ext in (".png", ".pdf"):
        figures.savefig(args.out_prefix.with_suffix(ext), dpi=180)
    plt.close(figures)
    if report:
        write_csv(args.out_prefix.with_suffix(".frontiers.csv"), report)
    print(f"Wrote {len(points)} points and {len(report)} budget rows to {args.out_prefix}.*")


if __name__ == "__main__":
    main()
