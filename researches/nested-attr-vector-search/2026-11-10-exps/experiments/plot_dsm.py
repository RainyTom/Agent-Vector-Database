#!/usr/bin/env python3
"""Figures for the DSM study. Every number is read from the CSVs on disk."""
import argparse
import csv
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

DPI = 150
COLORS = {"rowpath": "#c0392b", "nodebitmap": "#2471a3", "trie": "#1e8449"}


def read_csv(path):
    with open(path) as handle:
        return list(csv.DictReader(handle))


def numeric(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def fig_latency(rows, out):
    figure, axes = plt.subplots(figsize=(6.4, 4.2))
    for name, color in COLORS.items():
        for tree, marker, style in (("uniform", "o", "-"), ("skewed", "s", "--")):
            points = []
            for row in rows:
                if row["tree"] != tree or row["representation"] != name:
                    continue
                if row["primitive"] != "move":
                    continue
                size = numeric(row["actual_subtree_size"])
                latency = numeric(row["latency_p50_ms"])
                if size and latency is not None:
                    points.append((size, latency))
            if not points:
                continue
            points.sort()
            xs, ys = zip(*points)
            axes.plot(xs, np.maximum(ys, 1e-3), style, marker=marker, ms=4,
                      color=color, label=f"{name} ({tree})")
    axes.set_xscale("log")
    axes.set_yscale("log")
    axes.set_xlabel("subtree size |S| (rows)")
    axes.set_ylabel("MOVE latency p50 (ms)")
    axes.set_title("MOVE cost vs subtree size")
    axes.grid(alpha=0.3, which="both")
    axes.legend(fontsize=7, ncol=2)
    figure.tight_layout()
    figure.savefig(out, dpi=DPI)
    plt.close(figure)


def fig_bytes(rows, out):
    figure, axes = plt.subplots(figsize=(6.4, 4.2))
    for name, color in COLORS.items():
        points = []
        for row in rows:
            if row["representation"] != name or row["primitive"] != "move":
                continue
            size = numeric(row["actual_subtree_size"])
            written = numeric(row["bytes_written"])
            theory = numeric(row["theoretical_bytes"])
            if size and written is not None:
                points.append((size, written, theory))
        if not points:
            continue
        points.sort()
        xs = [p[0] for p in points]
        axes.plot(xs, [max(p[1], 1) for p in points], "o-", color=color,
                  ms=4, label=f"{name} measured")
        axes.plot(xs, [max(p[2] or 1, 1) for p in points], "x:", color=color,
                  ms=6, label=f"{name} theoretical")
    axes.set_xscale("log")
    axes.set_yscale("log")
    axes.set_xlabel("subtree size |S| (rows)")
    axes.set_ylabel("bytes written per MOVE")
    axes.set_title("MOVE write amplification: measured vs theory")
    axes.grid(alpha=0.3, which="both")
    axes.legend(fontsize=7, ncol=2)
    figure.tight_layout()
    figure.savefig(out, dpi=DPI)
    plt.close(figure)


def fig_mask_build(rows, out):
    figure, axes = plt.subplots(figsize=(6.4, 4.2))
    for name, color in COLORS.items():
        points = []
        for row in rows:
            if row["representation"] != name:
                continue
            n = numeric(row["N"])
            ms = numeric(row["mask_build_ms"])
            if n and ms is not None:
                points.append((n, ms))
        if len(points) < 2:
            continue
        points.sort()
        xs, ys = zip(*points)
        axes.plot(xs, ys, "o-", color=color, ms=5, label=f"{name} measured")
        slope, intercept = np.polyfit(xs, ys, 1)
        fitted = [slope * x + intercept for x in xs]
        axes.plot(xs, fitted, ":", color=color, lw=1.2,
                  label=f"linear fit (slope {slope*1e3:.3f} us/row)")
    axes.set_xlabel("N (rows)")
    axes.set_ylabel("mask build time (ms)")
    axes.set_title("Scope mask construction cost")
    axes.grid(alpha=0.3)
    axes.legend(fontsize=7)
    figure.tight_layout()
    figure.savefig(out, dpi=DPI)
    plt.close(figure)


def fig_concurrency(rows, out):
    figure, axes = plt.subplots(figsize=(5.2, 3.8))
    labels = ["non-atomic", "atomic"]
    values = []
    for row in rows:
        values.append(float(row["violation_rate"]))
    axes.bar(labels[:len(values)], values, color=["#c0392b", "#1e8449"])
    axes.set_ylabel("violation rate")
    axes.set_title("Consistency violations during MOVE")
    for index, value in enumerate(values):
        axes.text(index, value, f"{value:.4f}", ha="center", va="bottom", fontsize=9)
    axes.set_ylim(0, max(1e-6, max(values) * 1.4 if values else 1e-3))
    figure.tight_layout()
    figure.savefig(out, dpi=DPI)
    plt.close(figure)


def fig_pareto(paths, out):
    figure, axes = plt.subplots(figsize=(6.4, 4.4))
    palette = {"nodeann": "#2471a3", "acorn": "#c0392b"}
    for label, path in paths:
        if not os.path.exists(path):
            continue
        points = []
        with open(path) as handle:
            for line in handle:
                line = line.strip()
                if not line.startswith("{"):
                    continue
                record = json.loads(line)
                points.append((record["qps"], record["recall"]))
        if not points:
            continue
        points.sort()
        xs, ys = zip(*points)
        axes.plot(xs, ys, "o-", color=palette.get(label, None), ms=4, label=label)
    axes.set_xscale("log")
    axes.set_xlabel("QPS (1 thread)")
    axes.set_ylabel("recall@10")
    axes.set_title("NodeANN vs ACORN on SIFT1M (M=32, efC=40)")
    axes.grid(alpha=0.3, which="both")
    axes.legend()
    figure.tight_layout()
    figure.savefig(out, dpi=DPI)
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True)
    parser.add_argument("--figures", required=True)
    parser.add_argument("--pareto-nodeann", default="")
    parser.add_argument("--pareto-acorn", default="")
    args = parser.parse_args()
    os.makedirs(args.figures, exist_ok=True)

    rows = []
    for tree in ("uniform", "skewed"):
        path = os.path.join(args.data, f"results_dsm_{tree}.csv")
        if os.path.exists(path):
            rows.extend(read_csv(path))
    if rows:
        fig_latency(rows, os.path.join(args.figures, "fig_dsm_latency_vs_size.png"))
        fig_bytes(rows, os.path.join(args.figures, "fig_move_bytes.png"))

    query_path = os.path.join(args.data, "results_query.csv")
    if os.path.exists(query_path):
        fig_mask_build(read_csv(query_path),
                       os.path.join(args.figures, "fig_mask_build_vs_N.png"))

    concurrency_path = os.path.join(args.data, "results_concurrency.csv")
    if os.path.exists(concurrency_path):
        fig_concurrency(read_csv(concurrency_path),
                        os.path.join(args.figures, "fig_violation_vs_concurrency.png"))

    if args.pareto_nodeann or args.pareto_acorn:
        fig_pareto([("nodeann", args.pareto_nodeann), ("acorn", args.pareto_acorn)],
                   os.path.join(args.figures, "fig_pareto_nodeann_acorn.png"))

    print("figures written to", args.figures)


if __name__ == "__main__":
    main()
