#!/usr/bin/env python3
"""Summarize offline bounds without treating Python timings as ANN speedups."""
import argparse
import csv
from collections import defaultdict
import json
from pathlib import Path
import statistics


def rows(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def save_csv(path, data):
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(data[0]))
        w.writeheader()
        w.writerows(data)


def grouped(data, keys):
    out = defaultdict(list)
    for r in data:
        out[tuple(r[k] for k in keys)].append(r)
    return out


def summary(data, keys):
    result = []
    for values, group in sorted(grouped(data, keys).items()):
        denominator = sum(int(r["eligible"]) for r in group)
        row = dict(zip(keys, values))
        row.update({"query_cases": len(group), "eligible_pair_denominator": denominator})
        for field in ("whole_skip_items", "conditional_skip_items", "gap_skip_items", "exact_oracle_skip_items",
                      "box_extra_items", "gap_extra_over_box_items", "gap_skip_multi_items"):
            total = sum(int(r[field]) for r in group)
            row[field] = total
            row[field + "_fraction"] = total / denominator if denominator else 0
        row["false_pruned_blocks"] = sum(int(r["false_pruned_blocks"]) for r in group)
        result.append(row)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path)
    ap.add_argument("--reference", type=Path)
    args = ap.parse_args()
    query = rows(args.input / "queries.csv")
    cert = rows(args.input / "certificates.csv")
    costs = rows(args.input / "indexed-costs.csv")
    out = args.input / "analysis"
    out.mkdir(exist_ok=True)
    qualified = [r for r in query if int(r["eligible"]) >= 10 and r["predicate"] != "scope_only"]
    for threshold in ("filtered_topk", "scope_radius"):
        selected = [r for r in qualified if r["threshold_mode"] == threshold]
        save_csv(out / (threshold + "-grid.csv"), summary(selected, ["dataset", "clusters", "basis", "r", "control"]))
        save_csv(out / (threshold + "-scopes.csv"), summary(selected, ["dataset", "scope", "predicate", "basis", "r", "control"]))
        if threshold == "scope_radius":
            save_csv(out / "scope_radius-pooled.csv", summary(selected, ["dataset", "r", "control"]))
    filtered = [r for r in qualified if r["threshold_mode"] == "filtered_topk"]
    pooled = summary(filtered, ["dataset", "r", "control"])
    save_csv(out / "pooled-information.csv", pooled)
    key = ["dataset", "clusters", "basis", "scope", "predicate", "control"]
    cert_by_key = {tuple(r[k] for k in key): r for r in cert}
    query_by_key = {tuple(r[k] for k in key + ["qidx", "r"]): r for r in filtered}
    joint = []
    for c in costs:
        rowkey = tuple(c[k] for k in key)
        qr = query_by_key.get(tuple(c[k] for k in key + ["qidx", "r"]))
        if qr is None:
            continue
        ce = cert_by_key[rowkey]
        dim = 1024 if c["dataset"].startswith("wiki") else 128
        for mechanism, saved, logical, probes in (
            ("conditional_box", int(qr["box_extra_items"]), int(ce["logical_extrema_read_bytes"]), int(ce["extrema_membership_probes"])),
            ("conditional_gap", int(qr["gap_skip_items"]) - int(qr["whole_skip_items"]), int(c["logical_read_bytes"]), int(c["membership_probes"]))):
            joint.append({**{k: c[k] for k in key}, "qidx": c["qidx"], "mechanism": mechanism,
                          "eligible": int(qr["eligible"]), "extra_skipped_items": saved,
                          "logical_certificate_bytes": logical, "membership_probes": probes,
                          "extra_avoided_full_vector_bytes": saved * dim * 4,
                          "all_eligible_full_vector_bytes": int(c["full_eligible_vector_bytes"]),
                          "sorted_arrays_bytes": int(ce["sorted_arrays_bytes"]),
                          "projection_multiply_adds": int(c["query_projection_multiply_adds"]),
                          "cold_attribute_comparisons": int(ce.get("attribute_comparisons", 0)),
                          "cold_scope_mask_positions": int(ce.get("scope_mask_positions", 0))})
    save_csv(out / "indexed-work.csv", joint)
    cost_summary = []
    for groupkey, group in sorted(grouped(joint, ["dataset", "clusters", "basis", "control", "mechanism"]).items()):
        logical = sum(r["logical_certificate_bytes"] for r in group)
        avoided = sum(r["extra_avoided_full_vector_bytes"] for r in group)
        cost_summary.append({**dict(zip(["dataset", "clusters", "basis", "control", "mechanism"], groupkey)),
                             "sample_query_cases": len(group),
                             "zero_extra_gain_cases": sum(r["extra_skipped_items"] == 0 for r in group),
                             "logical_certificate_bytes": logical, "extra_avoided_full_vector_bytes": avoided,
                             "logical_over_avoided_bytes": logical / avoided if avoided else "no_gain",
                             "membership_probes": sum(r["membership_probes"] for r in group),
                             "mean_sorted_arrays_bytes": statistics.mean(r["sorted_arrays_bytes"] for r in group)})
    save_csv(out / "indexed-cost-summary.csv", cost_summary)
    audit = {"queries": len(query), "certificates": len(cert), "indexed_costs": len(costs),
             "eligible_below_k_rows": sum(int(r["eligible"]) < 10 for r in query),
             "false_pruned_blocks": sum(int(r["false_pruned_blocks"]) for r in query),
             "interpretation": "Pruning denominators exclude scope_only and eligible<k; empties never count as geometric pruning. Byte ratio is a logical access model, not actual DRAM traffic or runtime speedup."}
    if args.reference:
        reference = rows(args.reference / "queries.csv")
        if reference != query:
            raise AssertionError("timing correction changed query information results")
        old_costs = rows(args.reference / "indexed-costs.csv")
        for a, b in zip(old_costs, costs, strict=True):
            for k in a:
                if not k.endswith("_s") and a[k] != b[k]:
                    raise AssertionError("timing correction changed indexed work: " + k)
        audit["reference_query_rows_identical"] = len(query)
        audit["reference_indexed_nontime_rows_identical"] = len(costs)
    (out / "audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    print(json.dumps(audit))
    for r in pooled:
        print(json.dumps(r))


if __name__ == "__main__":
    main()
