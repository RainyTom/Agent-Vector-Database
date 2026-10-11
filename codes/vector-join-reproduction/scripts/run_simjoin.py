#!/usr/bin/env python3
"""Run the paper-derived epsilon-SimJoin C++ implementation."""

from __future__ import annotations

import argparse
import json
import pathlib
import resource

import vector_join_py as vj
from experiment_io import provenance, validate_inputs


ORDER = {
    "base-mst": vj.SimJoinOrder.BASE_MST,
    "window-order-sort": vj.SimJoinOrder.WINDOW_ORDER_SORT,
    "window-order-epsilon": vj.SimJoinOrder.WINDOW_ORDER_EPSILON,
}
SERVER_LIMIT_BYTES = vj.MEMORY_LIMIT_BYTES


def enforce_process_memory_limit() -> None:
    """Apply the compiled memory policy as an OS address-space ceiling."""
    current_soft, current_hard = resource.getrlimit(resource.RLIMIT_AS)
    ceiling = SERVER_LIMIT_BYTES
    if current_hard != resource.RLIM_INFINITY:
        ceiling = min(ceiling, current_hard)
    resource.setrlimit(resource.RLIMIT_AS, (min(current_soft, ceiling) if current_soft != resource.RLIM_INFINITY else ceiling, ceiling))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=pathlib.Path, required=True,
                        help="row-major float32 Y file")
    parser.add_argument("--queries", type=pathlib.Path,
                        help="row-major float32 X file; omit only with --self-join")
    parser.add_argument("--dim", type=int, required=True)
    parser.add_argument("--self-join", action="store_true")
    parser.add_argument("--epsilon", type=float, required=True,
                        help="positive Euclidean L2 threshold")
    parser.add_argument("--w", type=int, required=True,
                        help="paper sliding-window bound; no undisclosed default")
    parser.add_argument("--order", choices=ORDER, required=True)
    parser.add_argument("--order-search-w", type=int, default=32,
                        help="explicit local maxT search width; required for WindowOrder-epsilon")
    parser.add_argument("--order-repeat", type=int, default=1,
                        help="online order repetitions; use 100 for Table 3 pre-ratio timing")
    parser.add_argument("--ground-truth-all", action="store_true")
    parser.add_argument("--graph-source", choices=("hnswlib", "external"), required=True)
    parser.add_argument("--graph-m", type=int, required=True,
                        help="HNSW M or declared external max degree; paper does not report it")
    parser.add_argument("--ef-construction", type=int, required=True,
                        help="HNSW build ef; use 0 for an external graph")
    parser.add_argument("--base-graph", type=pathlib.Path,
                        help="external adjacency text: source followed by neighbors")
    parser.add_argument("--query-graph", type=pathlib.Path,
                        help="external X graph; omit for self join")
    parser.add_argument("--base-entry", type=int, default=0)
    parser.add_argument("--query-entry", type=int, default=0)
    parser.add_argument("--graph-label",
                        help="descriptive source name, e.g. NSG; never treated as verified alone")
    parser.add_argument("--graph-provenance",
                        help="repository URL, immutable commit, build flags and graph parameters")
    parser.add_argument("--seed", type=int, required=True,
                        help="split/build seed; paper does not report this value")
    parser.add_argument("--base-limit", type=int, default=0)
    parser.add_argument("--query-limit", type=int, default=0)
    parser.add_argument("--ground-truth-limit", type=int, default=0,
                        help="0 disables quality evaluation; nonzero is recorded")
    parser.add_argument("--repeat", type=int, default=5,
                        help="paper averages five runs")
    parser.add_argument("--dataset-name", required=True,
                        help="free-form label; datasets are not allow-listed")
    parser.add_argument("--preprocessing", required=True,
                        help="free-form, e.g. none or l2-normalized")
    parser.add_argument("--split-description", required=True,
                        help="records how X/Y were selected or split")
    parser.add_argument("--output", type=pathlib.Path, required=True,
                        help="raw JSONL results; existing files are rejected")
    parser.add_argument("--pairs-output", type=pathlib.Path,
                        help="optional raw match lists as JSONL; existing files are rejected")
    return parser.parse_args()


def validate(args: argparse.Namespace) -> pathlib.Path:
    if args.dim <= 0 or args.w <= 0 or args.repeat <= 0:
        raise SystemExit("--dim, --w and --repeat must be positive")
    if args.graph_m <= 0:
        raise SystemExit("--graph-m must be positive")
    if args.self_join:
        if args.queries is not None and args.queries.resolve() != args.base.resolve():
            raise SystemExit("--self-join cannot use a different --queries file")
        query_path = args.base
    elif args.queries is None:
        raise SystemExit("--queries is required unless --self-join is used")
    else:
        query_path = args.queries
    if args.graph_source == "external":
        if args.base_graph is None or not args.graph_label or not args.graph_provenance:
            raise SystemExit(
                "external graphs require --base-graph, --graph-label and --graph-provenance")
        if not args.self_join and args.query_graph is None:
            raise SystemExit("ordinary join with external graphs requires --query-graph")
        if args.ef_construction != 0:
            raise SystemExit("use --ef-construction 0 for external graphs")
    elif args.base_graph is not None or args.query_graph is not None:
        raise SystemExit("graph files are only valid with --graph-source external")
    elif args.ef_construction <= 0:
        raise SystemExit("hnswlib requires --ef-construction > 0")
    if args.epsilon <= 0:
        raise SystemExit("--epsilon must be positive")
    return query_path


def main() -> None:
    enforce_process_memory_limit()
    args = parse_args()
    query_path = validate(args)
    validate_inputs(args, query_path)
    manifest = provenance(args, query_path)
    config = vj.SimJoinConfig()
    config.epsilon = args.epsilon
    config.window_size = args.w
    config.k = 0
    config.order = ORDER[args.order]
    config.order_search_window = args.order_search_w
    config.order_repeats = args.order_repeat
    config.self_join = args.self_join
    config.graph_m = args.graph_m
    config.ef_construction = args.ef_construction
    config.seed = args.seed

    args.output.parent.mkdir(parents=True, exist_ok=True)
    pair_file = None
    if args.pairs_output:
        args.pairs_output.parent.mkdir(parents=True, exist_ok=True)
        pair_file = args.pairs_output.open("x", encoding="utf-8")
    try:
        with args.output.open("x", encoding="utf-8") as output_file:
            for repetition in range(args.repeat):
                if args.graph_source == "hnswlib":
                    result = vj.run_simjoin_files(
                        str(query_path), str(args.base), args.dim, config,
                        args.query_limit, args.base_limit, args.ground_truth_limit)
                else:
                    query_graph = args.base_graph if args.self_join else args.query_graph
                    result = vj.run_simjoin_graph_files(
                        str(query_path), str(args.base), args.dim,
                        str(query_graph), str(args.base_graph),
                        args.query_entry, args.base_entry,
                        args.graph_label, args.graph_provenance, config,
                        args.query_limit, args.base_limit, args.ground_truth_limit)
                record = json.loads(result.metrics.to_json())
                record.update(manifest)
                record.update({
                    "repetition": repetition,
                    "dataset_name": args.dataset_name,
                    "base_path": str(args.base.resolve()),
                    "query_path": str(query_path.resolve()),
                    "input_format": "raw-f32-row-major",
                    "graph_source": args.graph_source,
                    "process_memory_limit_bytes": SERVER_LIMIT_BYTES,
                    "preprocessing": args.preprocessing,
                    "split_description": args.split_description,
                    "paper_parameter_status": {
                        "w": "user-supplied; sweep values unreported by paper",
                        "graph_m": "user-supplied; unreported by paper",
                        "ef_construction": "user-supplied; unreported by paper",
                        "seed": "user-supplied; unreported by paper",
                    },
                })
                output_file.write(json.dumps(record, sort_keys=True) + "\n")
                output_file.flush()
                print(json.dumps(record, sort_keys=True))
                if pair_file is not None:
                    for query_id, query_result in enumerate(result.queries):
                        pair_file.write(json.dumps({
                            "repetition": repetition,
                            "query_id": query_id,
                            "matches": query_result.matches,
                            "distance_computations": query_result.distance_computations,
                        }) + "\n")
                    pair_file.flush()
    finally:
        if pair_file is not None:
            pair_file.close()


if __name__ == "__main__":
    main()
