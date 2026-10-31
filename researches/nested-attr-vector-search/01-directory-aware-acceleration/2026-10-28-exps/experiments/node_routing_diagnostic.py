#!/usr/bin/env python3
"""Investigate node selection, not an end-to-end ANN speed benchmark.
Build summaries from corpus only. GT is used solely for evaluation/oracle limits.
"""
import csv
import heapq
import importlib.util
import json
import os
from pathlib import Path
import time

for key in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "BLIS_NUM_THREADS"):
    os.environ[key] = "1"
import numpy as np

ROOT = Path(__file__).resolve().parents[5]
DAY = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "codes/directory-aware-vector-attr-search/data/wiki_full"
BUDGETS = (1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4096, 16384)


def save_csv(path, rows):
    with path.open("w") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    spec = importlib.util.spec_from_file_location("frozen_eval", ROOT / "codes/directory-aware-vector-attr-search/scripts/autoresearch_benchmark.py")
    frozen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(frozen)
    frozen.check_inputs()
    bindings, tin, tout, _ = frozen.load_scopes()
    reference = json.loads(frozen.REFERENCE.read_text())["queries"]
    n, dim = frozen.N, frozen.DIM
    base = np.memmap(FIXTURE / "base.f32", dtype="<f4", mode="r", shape=(n, dim))
    test = np.memmap(FIXTURE / "test.f32", dtype="<f4", mode="r", shape=(456, dim))
    started = time.perf_counter()
    counts = np.bincount(bindings, minlength=len(tin))
    nodes = np.flatnonzero(counts)
    order = np.argsort(bindings, kind="stable")
    starts = np.r_[0, np.cumsum(counts[nodes])[:-1]]
    node_to_group = np.full(len(tin), -1, dtype=np.int32)
    node_to_group[nodes] = np.arange(len(nodes))
    # The summaries contain no query or GT information.
    ordered = base[order]
    centers = np.add.reduceat(ordered, starts, axis=0, dtype=np.float64)
    centers /= counts[nodes, None]
    point_radii = np.empty(n, dtype=np.float64)
    for begin in range(0, n, 8192):
        end = min(begin + 8192, n)
        groups = node_to_group[bindings[order[begin:end]]]
        delta = ordered[begin:end].astype(np.float64) - centers[groups]
        point_radii[begin:end] = np.sqrt(np.einsum("ij,ij->i", delta, delta))
    radii = np.nextafter(np.maximum.reduceat(point_radii, starts), np.inf) + 1e-10
    del ordered, point_radii
    build_seconds = time.perf_counter() - started
    print(f"Built corpus-only summaries for {len(nodes)} nonempty nodes in {build_seconds:.3f}s", flush=True)
    perquery, routing_rows = [], []
    for ref in reference:
        if not ref["recursive"]:
            continue
        qi, qid, anchor = ref["query_index"], ref["query_id"], ref["anchor"]
        eligible = np.flatnonzero((tin[nodes] >= tin[anchor]) & (tin[nodes] < tout[anchor]))
        assert int(counts[nodes[eligible]].sum()) == ref["scope_card"]
        q = test[qid].astype(np.float64)
        delta = centers[eligible] - q
        center_d2 = np.einsum("ij,ij->i", delta, delta)
        lower = np.maximum(0.0, np.sqrt(center_d2) - radii[eligible] - 1e-10)
        centroid_order = np.lexsort((nodes[eligible], center_d2))
        owner_rank = np.empty(len(nodes), dtype=np.int64)
        owner_rank.fill(len(nodes) + 1)
        owner_rank[eligible[centroid_order]] = np.arange(1, len(eligible) + 1)
        gt = ref["gt_ids"]
        gt_groups = node_to_group[bindings[gt]]
        ranks = owner_rank[gt_groups]
        distinct_gt_nodes = len(set(bindings[gt].tolist()))
        for budget in BUDGETS:
            selected = eligible[centroid_order[:budget]]
            hits = int(np.count_nonzero(ranks <= budget))
            routing_rows.append({"query_index": qi, "candidate_nodes": len(eligible), "budget": budget,
                                 "selected_nodes": len(selected), "selected_points": int(counts[nodes[selected]].sum()),
                                 "scope_card": ref["scope_card"], "gt_count": len(gt), "hit_count": hits,
                                 "query_complete": int(hits == len(gt))})
        gt_delta = base[gt].astype(np.float64) - q
        gt_d2 = np.einsum("ij,ij->i", gt_delta, gt_delta)
        # Optimistic oracle: true kth distance is unavailable before search.
        oracle_tau = float(np.sqrt(gt_d2.max())) if len(gt) == 10 else float("inf")
        oracle_prunable = int(np.count_nonzero(lower > oracle_tau + 1e-10))
        bound_order = np.lexsort((nodes[eligible], lower))
        heap, visited_nodes, visited_points = [], 0, 0
        for at in bound_order:
            if len(heap) == 10 and lower[at] > np.sqrt(-heap[0][0]) + 1e-10:
                break
            group = int(eligible[at])
            ids = order[starts[group]:starts[group] + counts[nodes[group]]]
            local_delta = base[ids].astype(np.float64) - q
            distances = np.einsum("ij,ij->i", local_delta, local_delta)
            local_order = np.lexsort((ids, distances))[:10]
            for p in local_order:
                item = (-float(distances[p]), -int(ids[p]))
                if len(heap) < 10:
                    heapq.heappush(heap, item)
                elif item > heap[0]:
                    heapq.heapreplace(heap, item)
            visited_nodes += 1
            visited_points += len(ids)
        returned = {-item[1] for item in heap}
        exact_hits = len(returned.intersection(gt))
        perquery.append({"query_index": qi, "scope_card": ref["scope_card"], "candidate_nodes": len(eligible),
                         "gt_count": len(gt), "distinct_gt_nodes": distinct_gt_nodes,
                         "worst_gt_centroid_rank": int(ranks.max()),
                         "oracle_ball_prunable_nodes": oracle_prunable,
                         "best_first_visited_nodes": visited_nodes, "best_first_visited_points": visited_points,
                         "best_first_gt_hits": exact_hits})
    aggregate = []
    bounds = []
    for group in ("all_recursive", "candidate_nodes_ge16"):
        qq = [r for r in perquery if group == "all_recursive" or r["candidate_nodes"] >= 16]
        selected_qids = {r["query_index"] for r in qq}
        for budget in BUDGETS:
            rr = [r for r in routing_rows if r["query_index"] in selected_qids and r["budget"] == budget]
            aggregate.append({"group": group, "queries": len(rr), "budget": budget,
                              "macro_gt_pair_coverage": float(np.mean([r["hit_count"] / r["gt_count"] for r in rr])),
                              "global_gt_pair_coverage": sum(r["hit_count"] for r in rr) / sum(r["gt_count"] for r in rr),
                              "query_complete_fraction": float(np.mean([r["query_complete"] for r in rr])),
                              "selected_node_fraction_global": sum(r["selected_nodes"] for r in rr) / sum(r["candidate_nodes"] for r in rr),
                              "selected_point_fraction_global": sum(r["selected_points"] for r in rr) / sum(r["scope_card"] for r in rr)})
        total_nodes, total_points = sum(r["candidate_nodes"] for r in qq), sum(r["scope_card"] for r in qq)
        bounds.append({"group": group, "queries": len(qq), "candidate_nodes_total": total_nodes,
                       "median_candidate_nodes": float(np.median([r["candidate_nodes"] for r in qq])),
                       "max_candidate_nodes": max(r["candidate_nodes"] for r in qq),
                       "median_distinct_gt_nodes": float(np.median([r["distinct_gt_nodes"] for r in qq])),
                       "fraction_all_gt_in_one_node": float(np.mean([r["distinct_gt_nodes"] == 1 for r in qq])),
                       "oracle_ball_node_prune_fraction": sum(r["oracle_ball_prunable_nodes"] for r in qq) / total_nodes,
                       "best_first_node_prune_fraction": 1 - sum(r["best_first_visited_nodes"] for r in qq) / total_nodes,
                       "best_first_point_prune_fraction": 1 - sum(r["best_first_visited_points"] for r in qq) / total_points,
                       "best_first_macro_recall": float(np.mean([r["best_first_gt_hits"] / r["gt_count"] for r in qq])),
                       "best_first_global_recall": sum(r["best_first_gt_hits"] for r in qq) / sum(r["gt_count"] for r in qq)})
    save_csv(DAY / "data/node_routing_queries.csv", perquery)
    save_csv(DAY / "data/node_routing_centroid_budgets.csv", aggregate)
    summary = {"nodes": len(nodes), "centroid_payload_bytes": centers.nbytes, "radius_payload_bytes": radii.nbytes,
               "summary_build_seconds_python": build_seconds,
               "classification": "routing diagnostic; centroid GT coverage upper-bounds ANN recall, not end-to-end performance",
               "arithmetic": "float64 summaries/local distances on raw float32 vectors; radius inflation1e-10; not formal floating-point certification",
               "groups": bounds}
    (DAY / "data/node_routing_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    for r in aggregate:
        if r["group"] == "candidate_nodes_ge16" and r["budget"] in (1,4,16,64,256,1024):
            print(json.dumps(r), flush=True)


if __name__ == "__main__":
    main()
