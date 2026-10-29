#!/usr/bin/env python3
"""Audit topology-paired C++ traces and plot real query/update cost frontiers."""
import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from analyze_dynamic_triad import bucket, dump, stats
from plot_dynamic_triad import COLORS, LABELS

CATEGORIES = ("tombstone_compaction", "threshold_build", "threshold_drop")
FAMILIES = ("root_recursive", "broad_recursive", "narrow_recursive",
            "nonempty_nonrecursive", "empty_nonrecursive")


def close(left, right):
    return math.isclose(float(left), float(right), rel_tol=1e-8, abs_tol=1e-5)


def read_csv(path):
    with path.open() as handle:
        return list(csv.DictReader(handle))


def root_of(row):
    return int(row["config"]["cohort_root"])


def analyze(payload, out):
    errors, maintenance, slices = [], [], []
    pairs = defaultdict(dict)
    sequence_checks = 0
    gt_mode = payload["execution"]["settings"].get("gt_mode", "inline")
    for run in payload["runs"]:
        if run["validation_errors"] or not run.get("summary"):
            errors.append({"config_id": run["config_id"], "arm": run["arm"],
                           "error": "incomplete or invalid raw run"})
            continue
        config, summary = run["config"], run["summary"]
        prefix = Path(run["raw_prefix"])
        queries = read_csv(Path(str(prefix) + ".queries.csv"))
        updates = read_csv(Path(str(prefix) + ".updates.csv"))
        common = {"config_id": run["config_id"], "cohort_root": root_of(run),
                  "topology": config["topology"], "arm": run["arm"],
                  "repeat": run["repeat"], "actual_q_u": config["actual_q_u"],
                  "quality_pass": run["quality_pass"]}
        if gt_mode == "precomputed":
            if summary.get("gt_mode") != gt_mode or summary.get("gt_distance_calls") != 0:
                errors.append(dict(common, error="benchmark recomputed vector GT"))
            if any(int(q["gt_distance_calls"]) != 0 for q in queries):
                errors.append(dict(common, error="per-query vector GT not zero"))
        if not close(sum(summary[c + "_us"] for c in CATEGORIES), summary["compaction_total_us"]):
            errors.append(dict(common, error="summary maintenance time partition"))
        if sum(summary[c + "_count"] for c in CATEGORIES) != summary["compaction_count"]:
            errors.append(dict(common, error="summary maintenance count partition"))
        for update in updates:
            if not close(sum(float(update[c + "_us"]) for c in CATEGORIES), update["compaction_us"]):
                errors.append(dict(common, event=update["event"], error="update maintenance time partition"))
            if sum(int(update[c + "_count"]) for c in CATEGORIES) != int(update["compaction_count"]):
                errors.append(dict(common, event=update["event"], error="update maintenance count partition"))
            if sum(int(update[c + "_ann_insertions"]) for c in CATEGORIES) > int(update["ann_insertions"]):
                errors.append(dict(common, event=update["event"], error="excess maintenance insertion attribution"))
            if update["op"] != "M":
                errors.append(dict(common, event=update["event"], error="not pure ITEM movement"))
        for category in CATEGORIES:
            for suffix in ("_us", "_count", "_ann_insertions"):
                if not close(sum(float(u[category + suffix]) for u in updates), summary[category + suffix]):
                    errors.append(dict(common, error="update/summary category mismatch", field=category+suffix))
        expected_graphs = config["initial_hnsw_node_count"] if run["arm"] == "A" else 1
        if summary["initial_hnsw_node_count"] != expected_graphs:
            errors.append(dict(common, error="initial graph count differs from density contract"))
        expected_live = config["density_stats"]["direct_items_above_threshold"] if run["arm"] == "A" else config["n_live"]
        if summary["initial_index_live_count"] != expected_live or summary["initial_index_dead_count"] != 0:
            errors.append(dict(common, error="initial graph live/dead state differs from source density"))
        if run["arm"] != "A" and (summary["ann_insertions"] or summary["ann_deletions"]):
            errors.append(dict(common, error="global graph changed under attribute-only move"))
        mrow = dict(common, n_updates=summary["n_updates"],
                    mean_update_us=summary["update_total_us"] / max(1, summary["n_updates"]),
                    max_update_us=max((float(u["latency_us"]) for u in updates), default=0),
                    initial_hnsw_nodes=summary["initial_hnsw_node_count"],
                    final_hnsw_nodes=summary["final_hnsw_node_count"],
                    final_index_live=summary["final_index_live_count"],
                    final_index_dead=summary["final_index_dead_count"],
                    metadata_writes=summary["metadata_writes"],
                    ann_insertions=summary["ann_insertions"], ann_deletions=summary["ann_deletions"])
        for category in CATEGORIES:
            for suffix in ("_us", "_count", "_ann_insertions"):
                mrow[category + suffix] = summary[category + suffix]
        maintenance.append(mrow)
        grouped = defaultdict(list)
        for query in queries:
            family = FAMILIES[int(query["query_index"]) % 5]
            if family == "nonempty_nonrecursive" and (int(query["scope_card"]) == 0 or int(query["eligible_card"]) == 0):
                errors.append(dict(common, event=query["event"], error="nonempty family has no eligible item"))
            if family == "empty_nonrecursive" and int(query["scope_card"]) != 0:
                errors.append(dict(common, event=query["event"], error="empty family not empty"))
            grouped[family, "all", "all"].append(query)
            grouped[family, bucket(query["scope_card"]), bucket(query["eligible_card"])].append(query)
        for (family, scope, eligible), qs in grouped.items():
            gt = sum(int(q["gt_count"]) for q in qs)
            row = dict(common, family=family, scope_bucket=scope, eligible_bucket=eligible,
                       n_queries=len(qs), mean_scope=sum(int(q["scope_card"]) for q in qs)/len(qs),
                       mean_eligible=sum(int(q["eligible_card"]) for q in qs)/len(qs),
                       mean_dir_us=sum(float(q["dir_us"]) for q in qs)/len(qs),
                       mean_search_us=sum(float(q["search_us"]) for q in qs)/len(qs),
                       mean_fanout=sum(int(q["fanout"]) for q in qs)/len(qs),
                       mean_scan_nodes=sum(int(q["scan_nodes"]) for q in qs)/len(qs),
                       macro_recall=sum(float(q["recall"]) for q in qs)/len(qs),
                       pair_recall=sum(int(q["hit_count"]) for q in qs)/gt if gt else 1)
            row.update(stats([float(q["e2e_us"]) for q in qs], "query_us_"))
            slices.append(row)
        key = (root_of(run), float(config["target_q_u"]), run["repeat"], run["arm"])
        shared_gt = [(q["event"], q["version"], q["qidx"], q["recursive"], q["anchor"],
                      q["scope_card"], q["eligible_card"], q["gt_ids"], q["result_ids"],
                      q["distance_calls"], q["ann_hops"], q["fanout"], q["scan_nodes"])
                     for q in queries if int(q["recursive"]) == 0 or int(q["anchor"]) == 0]
        category_sequence = [tuple(u[field] for field in
            ("event", "version", "affected_items", "affected_nodes", "ann_insertions", "ann_deletions",
             *(c + "_count" for c in CATEGORIES), *(c + "_ann_insertions" for c in CATEGORIES))) for u in updates]
        pairs[key][config["topology"]] = (run, shared_gt, category_sequence)
    for key, group in pairs.items():
        if set(group) != {"native", "balanced"}:
            errors.append({"pair": list(key), "error": "missing topology pair"})
            continue
        native, balanced = group["native"], group["balanced"]
        cn, cb = native[0]["config"], balanced[0]["config"]
        if cn["trace_sha256"] != cb["trace_sha256"]:
            errors.append({"pair": list(key), "error": "paired trace changed"})
        for filename, digest in cn["data_file_sha256"].items():
            if filename != "tree.txt" and cb["data_file_sha256"].get(filename) != digest:
                errors.append({"pair": list(key), "error": "non-tree input changed", "file": filename})
        if native[1] != balanced[1]:
            errors.append({"pair": list(key), "error": "root/nonrecursive state, GT, results or ANN navigation mismatch"})
        if key[-1] == "A" and native[2] != balanced[2]:
            errors.append({"pair": list(key), "error": "local maintenance changed with topology-only mutation"})
        for field in ("initial_hnsw_node_count", "initial_index_live_count", "initial_index_dead_count",
                      "final_hnsw_node_count", "final_index_live_count", "final_index_dead_count"):
            if native[0]["summary"][field] != balanced[0]["summary"][field]:
                errors.append({"pair": list(key), "field": field, "error": "paired graph state differs"})
        sequence_checks += 1
    dump(out / "maintenance-breakdown.csv", maintenance)
    dump(out / "scope-family.csv", slices)
    audit = {"runs": len(payload["runs"]), "topology_pair_checks": sequence_checks,
             "quality_failed_runs": sum(not r["quality_pass"] for r in payload["runs"]),
             "gt_mode": gt_mode,
             "benchmark_gt_distance_calls": sum(r["summary"]["gt_distance_calls"] for r in payload["runs"]) if gt_mode == "precomputed" else None,
             "oracle_replays": len(payload.get("oracles", [])),
             "oracle_gt_distance_calls": sum(r["gt_distance_calls"] for r in payload.get("oracles", [])),
             "errors": errors,
             "scope_caveat": "Only root-recursive and nonrecursive GT identical across topology; descendant predicates differ.",
             "percentiles": "linear within repeat; plots mean and sample SD across repeats"}
    (out / "topology-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    print(json.dumps(audit))
    if errors:
        raise SystemExit(1)
    return slices


def plot(payload, slices, figures, label):
    aggregates = payload["aggregates"]
    roots = sorted({root_of(r) for r in aggregates})
    for root in roots:
        rows = [r for r in aggregates if root_of(r) == root]
        fig, axes = plt.subplots(1, 3, figsize=(14, 4.5))
        for topology in ("native", "balanced"):
            for arm in "ABC":
                rs = sorted([r for r in rows if r["arm"] == arm and r["config"]["topology"] == topology],
                            key=lambda r: r["config"]["actual_q_u"])
                for ax, metric, ylabel in zip(axes,
                        (lambda s: s["query_p95_us"], lambda s: s["update_total_us"]/max(1,s["n_updates"]),
                         lambda s: s["total_workload_us"]/max(1,s["n_queries"])),
                        ("Query p95 (us)", "Mean update / ITEM move (us)", "Measured total / query (us)")):
                    ys, deviations = [], []
                    for row in rs:
                        values=[metric(r["summary"]) for r in payload["runs"]
                                if r["config_id"] == row["config_id"] and r["arm"] == arm]
                        ys.append(statistics.mean(values))
                        deviations.append(statistics.stdev(values) if len(values)>1 else 0)
                    xs=[r["config"]["actual_q_u"] for r in rs]
                    ax.errorbar(xs,ys,yerr=deviations,color=COLORS[arm],
                                fmt="o-" if topology=="native" else "s--",capsize=3,
                                label=f"{arm} {topology}",ms=4)
                    failed=[(x,y) for x,y,r in zip(xs,ys,rs) if not r["quality_pass"]]
                    if failed: ax.scatter(*zip(*failed),color="red",marker="x",s=70,zorder=5)
                    ax.set_xscale("log"); ax.set_xlabel("Actual query / ITEM-move ratio")
                    ax.set_ylabel(ylabel); ax.grid(alpha=.25)
                    ax.legend(fontsize=7,ncol=2)
        config=rows[0]["config"]
        budgets=sorted({r["summary"]["ef"] for r in rows})
        repeats=sorted({r["repeats"] for r in rows})
        gt_mode=payload["execution"]["settings"].get("gt_mode","inline")
        fig.suptitle(f"WIKI subtree {root}, N={config['n_live']}; ef={budgets}; repeats={repeats}; GT={gt_mode}\n"
                     "serialized replay; mean +/- repeat SD; parallel processes; red x = quality failure",fontsize=9)
        fig.tight_layout()
        dest=figures/f"{label}_root{root}_frontier.png"
        fig.savefig(dest,dpi=150); plt.close(fig); print(dest)
        scope_rows=[r for r in slices if r["cohort_root"]==root and r["scope_bucket"]=="all"
                    and r["eligible_bucket"]=="all" and close(r["actual_q_u"],1)]
        if scope_rows:
            fig,axes=plt.subplots(1,2,figsize=(13,4.5))
            for ax,topology in zip(axes,("native","balanced")):
                for offset,arm in enumerate("ABC"):
                    means=[]
                    for family in FAMILIES:
                        values=[r["query_us_p95"] for r in scope_rows if r["topology"]==topology
                                and r["arm"]==arm and r["family"]==family]
                        means.append(statistics.mean(values) if values else float("nan"))
                    ax.bar([i+(offset-1)*.25 for i in range(5)],means,width=.25,
                           color=COLORS[arm],label=LABELS[arm])
                ax.set_xticks(range(5),["root rec","wide rec","narrow rec","nonrec live","nonrec empty"],rotation=20)
                ax.set_yscale("log"); ax.set_ylabel("Mean repeat query p95 (us)")
                ax.set_title(topology); ax.legend(fontsize=7); ax.grid(axis="y",alpha=.25)
            fig.suptitle(f"Source subtree {root}; Q/U=1; actual predicates differ for wide/narrow recursive",fontsize=10)
            fig.tight_layout(); dest=figures/f"{label}_root{root}_scope.png"
            fig.savefig(dest,dpi=150); plt.close(fig); print(dest)


def crossings(payload, out):
    grouped = defaultdict(dict)
    for run in payload["runs"]:
        key = (root_of(run), run["config"]["topology"], run["repeat"])
        grouped[key][float(run["config"]["target_q_u"]), run["arm"]] = run
    checks = []
    for (root, topology, repeat), points in sorted(grouped.items()):
        ratios = sorted({ratio for ratio, arm in points})
        comparisons = {}
        for other in ("B", "C"):
            states, flips = [], []
            for ratio in ratios:
                a, b = points[ratio, "A"], points[ratio, other]
                qualified = a["quality_pass"] and b["quality_pass"]
                difference = a["summary"]["total_workload_us"] - b["summary"]["total_workload_us"]
                states.append({"target_q_u": ratio, "actual_q_u": a["config"]["actual_q_u"],
                               "quality_qualified": qualified,
                               "difference_us": difference,
                               "A_lower_total": difference < 0,
                               "A_total_us": a["summary"]["total_workload_us"],
                               "other_total_us": b["summary"]["total_workload_us"]})
            for left, right in zip(states, states[1:]):
                if (left["quality_qualified"] and right["quality_qualified"] and
                        left["difference_us"] * right["difference_us"] < 0):
                    flips.append([left["actual_q_u"], right["actual_q_u"]])
            comparisons[other] = {"observed_points": states, "observed_order_flip_brackets": flips}
        checks.append({"cohort_root": root, "topology": topology, "repeat": repeat,
                       "A_vs": comparisons})
    (out / "crossing-audit.json").write_text(json.dumps(
        {"checks": checks, "interpretation": "Observed quality-qualified endpoint ordering only; no fitted crossing or continuity claim."},
        indent=2) + "\n")


def compare_gt_modes(payload, reference, out):
    if (payload["execution"]["settings"].get("gt_mode") != "precomputed" or
            reference["execution"]["settings"].get("gt_mode", "inline") != "inline"):
        raise ValueError("GT control requires precomputed measurements and an inline reference")
    key = lambda run: (run["config_id"], run["repeat"], run["arm"])
    before = {key(run): run for run in reference["runs"]}
    errors, query_rows, update_rows = [], 0, 0
    query_integers = ("event", "version", "query_index", "qidx", "recursive", "anchor", "anchor_depth",
                      "scope_card", "eligible_card", "gt_count", "hit_count", "distance_calls", "ann_hops",
                      "local_indexes", "valid_count", "invalid_count", "duplicate_count", "fanout", "scan_nodes")
    for run in payload["runs"]:
        old = before.get(key(run))
        if old is None:
            errors.append({"run": key(run), "error": "missing inline reference"})
            continue
        if any(run[field] != old[field] for field in ("trace_sha256", "data_sha256")):
            errors.append({"run": key(run), "error": "GT-mode control changed inputs"})
        for kind in ("queries", "updates"):
            now_rows = read_csv(Path(run["raw_prefix"] + "." + kind + ".csv"))
            old_rows = read_csv(Path(old["raw_prefix"] + "." + kind + ".csv"))
            if len(now_rows) != len(old_rows):
                errors.append({"run": key(run), "error": kind + " count changed"})
                continue
            for index, (now, prior) in enumerate(zip(now_rows, old_rows)):
                if kind == "queries":
                    matched = all(int(now[f]) == int(prior[f]) for f in query_integers)
                    matched = matched and all(now[f].split(";") == prior[f].split(";")
                                              for f in ("gt_ids", "result_ids"))
                    query_rows += 1
                else:
                    fields = [f for f in prior if not f.endswith("_us")]
                    matched = all(int(now[f]) == int(prior[f]) for f in fields if f != "op")
                    matched = matched and now["op"] == prior["op"]
                    update_rows += 1
                if not matched:
                    errors.append({"run": key(run), "row": index, "error": kind + " behavior changed"})
                    break
    audit = {"reference_gt_mode": "inline", "measured_gt_mode": "precomputed",
             "runs": len(payload["runs"]), "query_rows_checked": query_rows,
             "update_rows_checked": update_rows, "errors": errors,
             "interpretation": "State, ranked IDs, ANN navigation and maintenance counts identical; timings intentionally not compared."}
    (out / "gt-mode-control-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    print(json.dumps(audit))
    if errors:
        raise SystemExit(1)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results",type=Path)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--figures",type=Path,required=True)
    parser.add_argument("--reference-results",type=Path)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True); args.figures.mkdir(parents=True,exist_ok=True)
    payload=json.loads(args.results.read_text())
    slices=analyze(payload,args.out)
    crossings(payload,args.out)
    if args.reference_results:
        compare_gt_modes(payload,json.loads(args.reference_results.read_text()),args.out)
    plot(payload,slices,args.figures,args.results.parent.name)


if __name__=="__main__":
    main()
