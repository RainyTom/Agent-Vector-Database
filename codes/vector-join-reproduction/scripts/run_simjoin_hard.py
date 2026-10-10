#!/usr/bin/env python3
"""Run SimJoin hard-window sharing through the C++ pybind11 module."""

from __future__ import annotations

import argparse
import pathlib

import vector_join_py as vj


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
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    config = vj.Config()
    config.threshold = args.threshold
    config.graph_m = args.m
    config.ef_construction = args.ef_construction
    config.greedy_beam = args.greedy_beam
    config.hybrid_beam = args.hybrid_beam
    config.convergence_threshold = args.convergence
    config.ood_ratio_threshold = args.ood_ratio
    config.seed = args.seed
    output = vj.run_simjoin_hard_files(
        str(args.queries), str(args.base), args.dim, config,
        args.query_limit, args.base_limit, args.ground_truth_limit)
    print(output.metrics.to_json())


if __name__ == "__main__":
    main()
