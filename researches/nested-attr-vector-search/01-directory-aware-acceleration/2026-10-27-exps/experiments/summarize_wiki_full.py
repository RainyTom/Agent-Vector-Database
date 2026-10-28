#!/usr/bin/env python3
"""Aggregate raw query counters; exclude empty scopes from macro recall."""
import csv
import json
from pathlib import Path
import statistics

DAY = Path(__file__).resolve().parents[1]
N = 1941679
FILES = {
    "global_and_node_hnsw": "wiki_full_all3_dsq_perquery.csv",
    "node_flat_control": "wiki_full_node_flat_dsq_perquery.csv",
    "ivf_full_scan_control": "wiki_full_ivf_exact_dsq_perquery.csv",
}


def percentile(xs, p):
    xs = sorted(xs)
    index = (len(xs) - 1) * p
    lo = int(index)
    hi = min(lo + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (index - lo)


def main():
    output = []
    reference = None
    for run, name in FILES.items():
        groups = {}
        with (DAY / "data" / name).open() as f:
            for row in csv.DictReader(f):
                key = (row["design"], row["executor"])
                groups.setdefault(key, []).append(row)
        for (design, executor), rows in groups.items():
            assert len(rows) == 456
            assert {int(r["query_index"]) for r in rows} == set(range(456))
            scopes = {int(r["query_index"]): (int(r["scope_card"]), int(r["gt_count"])) for r in rows}
            if reference is None:
                reference = scopes
            assert scopes == reference, (run, design, "GT metadata differs")
            for r in rows:
                g, h, v = (int(r[k]) for k in ("gt_count", "hit_count", "valid_return_count"))
                assert g == min(10, int(r["scope_card"])) and 0 <= h <= g
                assert h <= v <= 10
                if design == "NODE":
                    assert float(r["bitmap_us"]) == 0
            for qtype in ("all", "recursive", "non-recursive"):
                rr = [r for r in rows if qtype == "all" or r["query_type"] == qtype]
                nonempty = [r for r in rr if int(r["gt_count"]) > 0]
                total_gt = sum(int(r["gt_count"]) for r in rr)
                hits = sum(int(r["hit_count"]) for r in rr)
                latencies = [float(r["e2e_ms"]) for r in rr]
                mean_ms = statistics.mean(latencies)
                item = {
                    "run": run, "design": design, "executor": executor, "query_type": qtype,
                    "queries": len(rr), "zero_result_queries": len(rr) - len(nonempty),
                    "gt_pairs": total_gt, "hit_pairs": hits,
                    "query_average_recall_nonempty": statistics.mean(int(r["hit_count"]) / int(r["gt_count"]) for r in nonempty) if nonempty else None,
                    "global_pair_recall": hits / total_gt if total_gt else None,
                    "legacy_mean_hit_div_10_including_empty": statistics.mean(float(r["recall10"]) for r in rr),
                    "ndcg_mean_including_empty_as_one": statistics.mean(float(r["ndcg10"]) for r in rr),
                    "mean_e2e_ms": mean_ms, "p95_e2e_ms": percentile(latencies, 0.95),
                    "serial_qps": 1000 / mean_ms,
                    "mean_directory_us": statistics.mean(float(r["dir_us"]) for r in rr),
                    "mean_search_us": statistics.mean(float(r["search_us"]) for r in rr),
                    "median_scope_card": statistics.median(int(r["scope_card"]) for r in rr),
                    "mean_selectivity": statistics.mean(int(r["scope_card"]) / N for r in rr),
                }
                output.append(item)
    # The three directory designs feed the same index and must have identical hits.
    global_rows = [r for r in output if r["run"] != "node_flat_control" and r["design"] != "NODE"]
    for run, executor in {(r["run"], r["executor"]) for r in global_rows}:
        for qtype in ("all", "recursive", "non-recursive"):
            same = [r for r in global_rows if r["run"] == run and r["executor"] == executor and r["query_type"] == qtype]
            assert len({r["hit_pairs"] for r in same}) == 1, (run, executor, qtype, "designs differ")
    csv_path = DAY / "data/wiki_full_summary.csv"
    with csv_path.open("w") as f:
        writer = csv.DictWriter(f, fieldnames=list(output[0]))
        writer.writeheader()
        writer.writerows(output)
    (DAY / "data/wiki_full_summary.json").write_text(json.dumps(output, indent=2) + "\n")
    print(csv_path)
    for item in output:
        if item["query_type"] != "all":
            print(item["run"], item["design"], item["executor"], item["query_type"],
                  "macro", round(item["query_average_recall_nonempty"], 6),
                  "global", round(item["global_pair_recall"], 6),
                  "mean_ms", round(item["mean_e2e_ms"], 6),
                  "empty", item["zero_result_queries"])


if __name__ == "__main__":
    main()
