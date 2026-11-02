#!/usr/bin/env python3
"""Counterfactual for the cross-owner-edge attribution in the two-case report.

Reads the edge-prune ablation (same-session DAG timings) and same-session
NodeANN reference batches. The reference QPS is never rescaled or invented;
if a needed reference batch is missing the script stops.
"""
import argparse
import csv
import gzip
import json
import pathlib
import statistics
import sys

ARCHIVE_NOTE = (
    "distances_match compares the ablation work counters with the archived "
    "run2 per-query audit; the archived run is used only for that audit, "
    "never for timing."
)


def median(values):
    return statistics.median(values)


def open_csv(path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", newline="")
    return path.open(newline="")


def read_group_batches(path):
    """median batch_us by (group, k) from fanout-batches.csv."""
    rows = {}
    with path.open() as f:
        for row in csv.DictReader(f):
            rows.setdefault((row["group"], int(row["k"])), []).append(float(row["batch_us"]))
    return {key: median(values) for key, values in rows.items()}


def read_all_batches(path):
    """median batch_us by (arm, k, local_n) from pareto-batches.csv."""
    rows = {}
    with path.open() as f:
        for row in csv.DictReader(f):
            rows.setdefault((row["arm"], int(row["k"]), int(row["n"])), []).append(
                float(row["batch_us"]))
    return {key: median(values) for key, values in rows.items()}


def qps(batch_us, queries):
    # 16 passes per batch in both protocols.
    return queries * 16 * 1e6 / batch_us


def archived_work(method_dir, k):
    """Per-group sums of the archived merged arm at one (k, ef) cell."""
    sums = {}
    field = ("distances", "item_checks", "adjacency_entries")
    with open_csv(method_dir / "pareto-queries.csv.gz") as f:
        for row in csv.DictReader(f):
            if row["arm"] != "merged" or int(row["k"]) != k:
                continue
            group = "one_graph" if int(row["eligible_owner_groups"]) == 1 else "many_graphs"
            for target in (group, "all"):
                acc = sums.setdefault(target, {name: 0 for name in field})
                for name in field:
                    acc[name] += int(row[name])
    return sums


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", required=True, help="edge-prune output directory")
    parser.add_argument("--nq", type=int, default=120)
    parser.add_argument(
        "--reference",
        default=None,
        help="same-session DAG + NodeANN runs (default: <dir>/envcheck)",
    )
    parser.add_argument(
        "--archive",
        default="researches/nested-attr-vector-search/2026-11-02-exps/data/dag-two-cases-m64-run2",
        help="archived run2, used only for the per-query work audit",
    )
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    root = pathlib.Path(args.dir)
    reference = pathlib.Path(args.reference) if args.reference else root / "envcheck"
    archive = pathlib.Path(args.archive)
    out = pathlib.Path(args.out) if args.out else root / "analysis"
    out.mkdir(parents=True, exist_ok=True)

    gap_rows = []
    work_rows = []
    for semantics in ("recursive", "direct"):
        run = root / f"nq{args.nq}-{semantics}"
        summary_path = run / f"edge-prune-summary-{semantics}.csv"
        if not summary_path.exists():
            raise SystemExit(f"missing {summary_path}")
        ref = reference / f"nq{args.nq}-{semantics}"
        nodeann = read_group_batches(ref / "nodeann" / "fanout-batches.csv")
        merged_ref = read_group_batches(ref / "dag_merge" / "fanout-batches.csv")
        nodeann_all = read_all_batches(ref / "nodeann" / "pareto-batches.csv")
        merged_all = read_all_batches(ref / "dag_merge" / "pareto-batches.csv")
        archived_dir = archive / f"nq{args.nq}-{semantics}"
        with summary_path.open() as f:
            summary = list(csv.DictReader(f))
        for row in summary:
            k = int(row["k"])
            group = row["group"]
            queries = int(row["queries"])
            work = archived_work(archived_dir / "dag_merge", k)[group]
            work_row = {
                "semantics": semantics,
                "k": k,
                "ef_requested": int(row["ef_requested"]),
                "group": group,
                "queries": queries,
                "distances": work["distances"],
                "item_checks": work["item_checks"],
                "adjacency_entries": work["adjacency_entries"],
                "rejected_offers": work["item_checks"] - work["distances"],
                "reject_fraction": (work["item_checks"] - work["distances"]) / work["item_checks"],
                "ablation_reject_fraction": float(row["reject_fraction"]),
                "ablation_distances_mean": float(row["distances_mean"]),
                "archived_distances_mean": work["distances"] / queries,
                "distances_match": abs(work["distances"] / queries - float(row["distances_mean"])) < 1e-6,
                "ablation_expansions_mean": float(row["expansions_mean"]),
            }
            work_rows.append(work_row)
            if group == "all":
                nodeann_qps = qps(nodeann_all[("nodeann_pergraph", k, k)], queries)
                reference_qps = qps(merged_all[("merged", k, 0)], queries)
            else:
                key = (group, k)
                if key not in nodeann or key not in merged_ref:
                    raise SystemExit(f"missing reference fan-out for {semantics} {group} k={k}")
                nodeann_qps = qps(nodeann[key], queries)
                reference_qps = qps(merged_ref[key], queries)
            gap_rows.append({
                "semantics": semantics,
                "k": k,
                "ef_requested": int(row["ef_requested"]),
                "group": group,
                "queries": queries,
                "nodeann_qps": nodeann_qps,
                "reference_merged_qps": reference_qps,
                "ablation_merged_qps": float(row["qps_merged"]),
                "pruned_qps": float(row["qps_merged_pruned"]),
                "ratio_before": nodeann_qps / reference_qps,
                "ratio_after_prune": nodeann_qps / float(row["qps_merged_pruned"]),
                "prune_delta_fraction": float(row["prune_delta_fraction"]),
                "removed_fraction": float(row["removed_fraction"]),
                "expanded_removed_fraction": float(row["expanded_removed_fraction"]),
                "reject_fraction": float(row["reject_fraction"]),
                "us_merged": float(row["us_merged"]),
                "us_merged_pruned": float(row["us_merged_pruned"]),
                "prune_build_us_mean": float(row["prune_build_us_mean"]),
                "distances_match": work_row["distances_match"],
            })

    columns = [
        "semantics", "k", "ef_requested", "group", "queries", "nodeann_qps",
        "reference_merged_qps", "ablation_merged_qps", "pruned_qps", "ratio_before",
        "ratio_after_prune", "prune_delta_fraction", "removed_fraction",
        "expanded_removed_fraction", "reject_fraction", "us_merged", "us_merged_pruned",
        "prune_build_us_mean", "distances_match",
    ]
    with (out / "gap-counterfactual.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        for row in gap_rows:
            writer.writerow({key: row[key] for key in columns})

    work_columns = [
        "semantics", "k", "ef_requested", "group", "queries", "distances",
        "item_checks", "adjacency_entries", "rejected_offers", "reject_fraction",
        "ablation_reject_fraction", "ablation_distances_mean", "archived_distances_mean",
        "distances_match", "ablation_expansions_mean",
    ]
    with (out / "rejection-ratios.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=work_columns)
        writer.writeheader()
        for row in work_rows:
            writer.writerow({key: row[key] for key in work_columns})

    declared = {
        "prune_equals_unpruned": all(row["distances_match"] for row in gap_rows),
        "note": "ID and distance/expansion equality are enforced inside the C++ runner; "
                "a failed check aborts before any CSV row is written. " + ARCHIVE_NOTE,
        "source": str(root),
        "reference": str(reference),
        "archive": str(archive),
        "prune_build_cost": "per-query copy construction, measured outside every timer",
    }
    (out / "edge-prune.json").write_text(json.dumps(declared, indent=2) + "\n")

    for row in gap_rows:
        print(
            f"{row['semantics']:9s} k={row['k']:3d} {row['group']:11s} "
            f"n={row['queries']:3d} delta={row['prune_delta_fraction']:+.4f} "
            f"exp_removed={row['expanded_removed_fraction']:.4f} "
            f"reject={row['reject_fraction']:.4f} "
            f"ratio_before={row['ratio_before']:.2f} ratio_after={row['ratio_after_prune']:.2f}"
        )


if __name__ == "__main__":
    sys.exit(main())
