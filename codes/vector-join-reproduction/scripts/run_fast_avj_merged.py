#!/usr/bin/env python3
"""Run Fast AVJ merged-index search through the C++ pybind11 module."""

from __future__ import annotations

import argparse
import json
import pathlib

import vector_join_py as vj
from experiment_io import provenance, validate_inputs


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=pathlib.Path, required=True)
    parser.add_argument("--queries", type=pathlib.Path, required=True)
    parser.add_argument("--dim", type=int, required=True)
    parser.add_argument("--threshold", type=float, required=True,
                        help="squared-L2 threshold")
    parser.add_argument("--base-limit", type=int, default=0)
    parser.add_argument("--query-limit", type=int, default=0)
    parser.add_argument("--ground-truth-limit", type=int, default=0)
    parser.add_argument("--m", type=int, default=16)
    parser.add_argument("--ef-construction", type=int, default=200)
    parser.add_argument("--greedy-beam", type=int, default=256)
    parser.add_argument("--hybrid-beam", type=int, default=256)
    parser.add_argument("--convergence", type=int, default=10)
    parser.add_argument("--ood-ratio", type=float, default=1.5)
    parser.add_argument("--seed", type=int, default=100)
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--ground-truth-all", action="store_true")
    parser.add_argument("--dataset-name", required=True)
    parser.add_argument("--preprocessing", required=True)
    parser.add_argument("--split-description", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--pairs-output", type=pathlib.Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.dim <= 0 or args.repeat <= 0:
        raise SystemExit("--dim and --repeat must be positive")
    validate_inputs(args, args.queries)
    manifest = provenance(args, args.queries)
    config = vj.Config()
    config.threshold = args.threshold
    config.graph_m = args.m
    config.ef_construction = args.ef_construction
    config.greedy_beam = args.greedy_beam
    config.hybrid_beam = args.hybrid_beam
    config.convergence_threshold = args.convergence
    config.ood_ratio_threshold = args.ood_ratio
    config.seed = args.seed
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.pairs_output:
        args.pairs_output.parent.mkdir(parents=True, exist_ok=True)
    pair_file = args.pairs_output.open("x", encoding="utf-8") if args.pairs_output else None
    try:
        with args.output.open("x", encoding="utf-8") as result_file:
            for repetition in range(args.repeat):
                output = vj.run_fast_avj_merged_files(
                    str(args.queries), str(args.base), args.dim, config,
                    args.query_limit, args.base_limit, args.ground_truth_limit)
                record = json.loads(output.metrics.to_json())
                record.update(manifest)
                record["repetition"] = repetition
                record["timing_policy"] = "total_online_ms=order_ms+search_and_ood_ms; excludes index, IO and ground truth"
                result_file.write(json.dumps(record, sort_keys=True) + "\n")
                result_file.flush()
                print(json.dumps(record, sort_keys=True))
                if pair_file is not None:
                    for query_id, result in enumerate(output.queries):
                        pair_file.write(json.dumps({"repetition": repetition, "query_id": query_id,
                                                    "matches": result.matches}) + "\n")
                    pair_file.flush()
    finally:
        if pair_file is not None:
            pair_file.close()


if __name__ == "__main__":
    main()
