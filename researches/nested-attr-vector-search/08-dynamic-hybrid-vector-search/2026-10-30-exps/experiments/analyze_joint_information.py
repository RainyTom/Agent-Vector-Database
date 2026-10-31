#!/usr/bin/env python3
"""Aggregate preregistered joint-information cases; no ANN performance estimates."""
from __future__ import annotations
import argparse
import csv
import gzip
import json
from pathlib import Path
from collections import defaultdict

COUNTS = ("eligible", "whole_skip_items", "conditional_skip_items", "filter_extra_items",
          "joint_extra_over_g1_items", "conditional_multi_skip_items", "raw_oracle_skip_items",
          "false_pruned_blocks", "eligible_min_reduction_values", "whole_min_reduction_values")


def aggregate(rows, fields):
    groups = {}
    for row in rows:
        if row["predicate"] == "scope_only":
            continue
        key = tuple(row[f] for f in fields)
        if key not in groups:
            groups[key] = {**dict(zip(fields, key)), "diagnostic_cases": 0, **{c: 0 for c in COUNTS}}
        out = groups[key]
        out["diagnostic_cases"] += 1
        for c in COUNTS:
            out[c] += int(row[c] or 0)
    for out in groups.values():
        total = out["eligible"]
        for col in ("whole_skip_items", "conditional_skip_items", "filter_extra_items",
                    "joint_extra_over_g1_items", "conditional_multi_skip_items", "raw_oracle_skip_items"):
            out[col.replace("_items", "_pct")] = 100 * out[col] / total if total else ""
        exact = out["raw_oracle_skip_items"]
        out["fraction_raw_exclusion_recovered_pct"] = 100 * out["conditional_skip_items"] / exact if exact else ""
    return list(groups.values())


def save(path, rows):
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def plot(rows, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(2, 2, figsize=(11, 7), sharey=True)
    colors = {16: "#8c564b", 64: "#1f77b4", 128: "#2ca02c", 256: "#ff7f0e", 1024: "#2ca02c"}
    for j, dataset in enumerate(("wiki-root3626", "sift12000")):
        for i, basis in enumerate(("coordinate", "pca")):
            ax = axes[i, j]
            chosen = [row for row in rows if row["dataset"] == dataset and row["basis"] == basis
                      and row["threshold_mode"] == "filtered_topk" and row["control"] == "correlated"
                      and row["family"] == "group" and row["order"] == "contiguous"]
            for r in sorted({int(row["r"]) for row in chosen}):
                curve = sorted((row for row in chosen if int(row["r"]) == r), key=lambda row: int(row["g"]))
                ax.plot([int(row["g"]) for row in curve], [row["conditional_skip_pct"] for row in curve],
                        marker="o", markersize=3, label=f"r={r}", color=colors[r])
            if chosen:
                ax.axhline(chosen[0]["raw_oracle_skip_pct"], color="black", linestyle=":", label="raw oracle")
            ax.set_xscale("log", base=2)
            ax.set_ylim(-2, 102)
            ax.set_title(f"{dataset} / {basis}")
            ax.set_xlabel("same-object group dimensions g")
            if j == 0:
                ax.set_ylabel("eligible ITEM weight excluded (%)")
            ax.grid(alpha=.2)
            ax.legend(fontsize=8)
    fig.suptitle("Joint information only — correlated attributes, fixed filtered-GT threshold\n"
                 "K/scopes/predicates pooled; all controls/orders retained in CSV; not online latency")
    fig.tight_layout(rect=(0, 0, 1, .91))
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root", type=Path)
    ap.add_argument("--figure", type=Path, required=True)
    ap.add_argument("--reference", type=Path)
    args = ap.parse_args()
    out = args.root / "analysis"
    out.mkdir(exist_ok=False)
    rows, audits = [], []
    for dataset in ("wiki-root3626", "sift12000"):
        folder = args.root / dataset
        audit = json.loads((folder / "audit.json").read_text())
        audits.append(audit)
        with gzip.open(folder / "queries.csv.gz", "rt") as f:
            part = list(csv.DictReader(f))
        assert len(part) == audit["query_rows"]
        assert all(int(row["false_pruned_blocks"]) == 0 for row in part)
        if args.reference:
            with gzip.open(args.reference / dataset / "queries.csv.gz", "rt") as f:
                before = list(csv.DictReader(f))
            excluded = {"eligible_min_reduction_values", "whole_min_reduction_values",
                        "eligible_oracle_feature_terms", "whole_oracle_feature_terms"}
            keep = lambda row: {key: value for key, value in row.items() if key not in excluded}
            assert len(before) == len(part) and all(keep(a) == keep(b) for a, b in zip(before, part))
        rows.extend(part)
    common = ("dataset", "family", "r", "g", "order", "threshold_mode", "control")
    pooled = aggregate(rows, common)
    by_basis = aggregate(rows, (*common, "basis"))
    grid = aggregate(rows, (*common, "basis", "clusters"))
    by_scope = aggregate(rows, (*common, "basis", "scope"))
    save(out / "pooled.csv", pooled)
    save(out / "by-basis.csv", by_basis)
    save(out / "grid.csv", grid)
    save(out / "by-scope.csv", by_scope)
    plot(by_basis, args.figure)
    summary = {"query_rows": len(rows), "unique_source_queries": 16,
               "old_g1_query_matches": sum(a["old_g1_query_matches"] for a in audits),
               "false_pruned_blocks": 0, "main_excludes_scope_only": True,
               "permuted_and_contiguous_not_double_pooled": True,
               "reference_information_rows_identical": len(rows) if args.reference else None,
               "pooled_rows": len(pooled), "grid_rows": len(grid)}
    (out / "audit.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))
    for dataset in ("wiki-root3626", "sift12000"):
        print(dataset)
        for row in pooled:
            if row["dataset"] != dataset or row["threshold_mode"] != "filtered_topk" or row["control"] != "correlated":
                continue
            if (row["family"] == "group" and row["order"] == "contiguous" and row["g"] == row["r"]) or row["family"] == "radial_lift":
                print({k: row[k] for k in ("family", "r", "g", "whole_skip_pct", "conditional_skip_pct",
                                          "filter_extra_pct", "raw_oracle_skip_pct")})


if __name__ == "__main__":
    main()
