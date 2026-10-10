#!/usr/bin/env python3
"""Run reproducible vector-join method/threshold sweeps using only stdlib."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--base", type=pathlib.Path, required=True)
    parser.add_argument("--queries", type=pathlib.Path, required=True)
    parser.add_argument("--dim", type=int, required=True)
    parser.add_argument("--thresholds", type=float, nargs="+", required=True)
    parser.add_argument(
        "--methods",
        nargs="+",
        default=["independent", "hard", "soft", "mi", "mi-adapt"],
    )
    parser.add_argument("--base-limit", type=int, default=0)
    parser.add_argument("--query-limit", type=int, default=0)
    parser.add_argument("--ground-truth-limit", type=int, default=100)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("experiment-results/vector_join_sweep.jsonl"),
    )
    return parser.parse_args()


def run_once(args: argparse.Namespace, method: str, threshold: float) -> dict[str, Any]:
    command = [
        str(args.binary),
        "--base",
        str(args.base),
        "--queries",
        str(args.queries),
        "--dim",
        str(args.dim),
        "--threshold",
        str(threshold),
        "--method",
        method,
        "--ground-truth-limit",
        str(args.ground_truth_limit),
    ]
    if args.base_limit:
        command.extend(["--base-limit", str(args.base_limit)])
    if args.query_limit:
        command.extend(["--query-limit", str(args.query_limit)])
    completed = subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(completed.stdout)


def main() -> None:
    args = parse_args()
    if args.repeat < 1:
        raise SystemExit("--repeat must be positive")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as output:
        for threshold in args.thresholds:
            for method in args.methods:
                for repetition in range(args.repeat):
                    record = run_once(args, method, threshold)
                    record["repetition"] = repetition
                    output.write(json.dumps(record, sort_keys=True) + "\n")
                    print(
                        f"threshold={threshold:g} method={method} repeat={repetition} "
                        f"join_ms={record['join_ms']:.3f} "
                        f"pair_recall={record['pair_recall']:.6f}"
                    )


if __name__ == "__main__":
    main()
