#!/usr/bin/env python3
"""Derive scope/operation slices from REAL runner rows (no extrapolated cost)."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path

import numpy as np


def bucket(n):
    n = int(n)
    for upper in (0, 10, 100, 1000):
        if n <= upper:
            return f"le{upper}"
    return "gt1000"


def dump(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def stats(values, prefix):
    return {prefix + key: float(np.percentile(values, percentile))
            for key, percentile in (("p50", 50), ("p95", 95), ("p99", 99))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    payload = json.loads(args.results.read_text())
    query_rows, update_rows, totals, errors = [], [], [], []
    # For every identical repeat/config, GT sequence must agree across all arms.
    gt_sequences = {}
    for run in payload["runs"]:
        if run.get("status") not in ("ok", "completed", "success", "cached") and "summary" not in run:
            errors.append({"config_id": run["config_id"], "arm": run["arm"], "error": "missing runner summary"})
            continue
        prefix = Path(run["raw_prefix"])
        common = {"config_id": run["config_id"], "arm": run["arm"], "repeat": run["repeat"],
                  "quality_pass": run["quality_pass"]}
        with Path(str(prefix) + ".queries.csv").open() as handle:
            queries = list(csv.DictReader(handle))
        signature = [(q["event"], q["version"], q["scope_card"], q["eligible_card"], q["gt_ids"])
                     for q in queries]
        key = (run["config_id"], run["repeat"])
        if key in gt_sequences and gt_sequences[key] != signature:
            errors.append({"config_id": run["config_id"], "arm": run["arm"], "error": "cross-arm GT/current-version mismatch"})
        gt_sequences[key] = signature
        grouped = defaultdict(list)
        for q in queries:
            grouped[(q["recursive"], q["anchor_depth"], bucket(q["scope_card"]), bucket(q["eligible_card"]))].append(q)
        for (recursive, depth, scope, eligible), qs in grouped.items():
            gt = sum(int(q["gt_count"]) for q in qs)
            row = dict(common, recursive=recursive, anchor_depth=depth, scope_bucket=scope,
                       eligible_bucket=eligible, n_queries=len(qs),
                       macro_recall=sum(float(q["recall"]) for q in qs) / len(qs),
                       pair_recall=sum(int(q["hit_count"]) for q in qs) / gt if gt else 1,
                       mean_dir_us=sum(float(q["dir_us"]) for q in qs) / len(qs),
                       mean_search_us=sum(float(q["search_us"]) for q in qs) / len(qs),
                       mean_distance_calls=sum(int(q["distance_calls"]) for q in qs) / len(qs),
                       mean_ann_hops=sum(int(q["ann_hops"]) for q in qs) / len(qs),
                       mean_fanout=sum(int(q["fanout"]) for q in qs) / len(qs),
                       mean_scan_nodes=sum(int(q["scan_nodes"]) for q in qs) / len(qs),
                       mean_local_indexes=sum(int(q["local_indexes"]) for q in qs) / len(qs))
            row.update(stats([float(q["e2e_us"]) for q in qs], "query_us_"))
            query_rows.append(row)
        with Path(str(prefix) + ".updates.csv").open() as handle:
            updates = list(csv.DictReader(handle))
        grouped = defaultdict(list)
        for u in updates:
            grouped[u["op"]].append(u)
        for op, us in grouped.items():
            total_us = sum(float(u["latency_us"]) for u in us)
            affected = sum(int(u["affected_items"]) for u in us)
            row = dict(common, op=op, n_updates=len(us), affected_item_operations=affected,
                       update_total_us=total_us, update_only_ops_s=len(us) * 1e6 / total_us if total_us else 0,
                       affected_item_ops_s=affected * 1e6 / total_us if total_us else 0,
                       metadata_writes=sum(int(u["metadata_writes"]) for u in us),
                       ann_insertions=sum(int(u["ann_insertions"]) for u in us),
                       ann_deletions=sum(int(u["ann_deletions"]) for u in us),
                       compaction_us=sum(float(u["compaction_us"]) for u in us),
                       compaction_count=sum(int(u["compaction_count"]) for u in us))
            row.update(stats([float(u["latency_us"]) for u in us], "update_us_"))
            update_rows.append(row)
        summary = run["summary"]
        total_us = summary["total_workload_us"]
        total_updates = summary["n_updates"]
        totals.append(dict(common, dataset=run["config"]["dataset"],
                           actual_unique_coverage=run["config"]["actual_unique_coverage"],
                           actual_q_u=run["config"]["actual_q_u"],
                           n_queries=summary["n_queries"], n_updates=total_updates,
                           workload_total_us=total_us,
                           amortized_us_per_query=total_us / max(1, summary["n_queries"]),
                           window_update_ops_s=total_updates * 1e6 / total_us if total_us else 0,
                           update_only_ops_s=total_updates * 1e6 / summary["update_total_us"] if summary["update_total_us"] else 0,
                           write_amplification_metadata_per_affected_item=sum(int(u["metadata_writes"]) for u in updates) /
                           max(1, sum(int(u["affected_items"]) for u in updates)),
                           write_amplification_ann_insertions_per_affected_item=sum(int(u["ann_insertions"]) for u in updates) /
                           max(1, sum(int(u["affected_items"]) for u in updates))))
    dump(args.out / "query_slices.csv", query_rows)
    dump(args.out / "update_slices.csv", update_rows)
    dump(args.out / "workload_totals.csv", totals)
    (args.out / "analysis_validation.json").write_text(json.dumps(
        {"cross_arm_gt_checks": len(gt_sequences), "errors": errors,
         "percentile_method": "numpy linear", "throughput_scope": "serialized event replay, not offered-load concurrency"}, indent=2) + "\n")
    print(json.dumps({"query_slices": len(query_rows), "update_slices": len(update_rows),
                      "runs": len(totals), "errors": errors}))
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
