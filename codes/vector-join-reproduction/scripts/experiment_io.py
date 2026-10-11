"""Data provenance utilities; contains no experiment scheduling or dispatch."""

from __future__ import annotations

import hashlib
import math
import os
import pathlib
import platform
import sys


def validate_inputs(args, query_path: pathlib.Path) -> int:
    for name in ("base_limit", "query_limit", "ground_truth_limit"):
        if getattr(args, name) < 0:
            raise SystemExit(f"--{name.replace('_', '-')} must be nonnegative")
    if not 0 <= args.seed <= 2**32 - 1:
        raise SystemExit("--seed must fit uint32")
    if hasattr(args, "epsilon") and not math.isfinite(args.epsilon):
        raise SystemExit("--epsilon must be finite")
    if getattr(args, "self_join", False) and args.query_limit not in (0, args.base_limit):
        raise SystemExit("self join requires matching base/query limits (or query limit 0)")
    if getattr(args, "order_search_w", 1) <= 0 or getattr(args, "order_repeat", 1) <= 0:
        raise SystemExit("--order-search-w and --order-repeat must be positive")
    explicit_order_width = any(arg.split("=", 1)[0] == "--order-search-w" for arg in sys.argv)
    if getattr(args, "order", "") == "window-order-epsilon" and not explicit_order_width:
        raise SystemExit("WindowOrder-epsilon requires an explicit --order-search-w")
    row_bytes = args.dim * 4
    for path in (args.base, query_path):
        if path.stat().st_size == 0 or path.stat().st_size % row_bytes:
            raise SystemExit(f"invalid raw-f32 shape: {path}")
    query_rows = query_path.stat().st_size // row_bytes
    limit = args.base_limit if getattr(args, "self_join", False) else args.query_limit
    if limit:
        query_rows = min(query_rows, limit)
    if args.ground_truth_all:
        if args.ground_truth_limit:
            raise SystemExit("choose --ground-truth-all or --ground-truth-limit")
        args.ground_truth_limit = query_rows
    input_paths = {path.resolve() for path in (args.base, query_path)}
    for name in ("base_graph", "query_graph"):
        path = getattr(args, name, None)
        if path:
            input_paths.add(path.resolve())
    outputs = [path.resolve() for path in (args.output, args.pairs_output) if path]
    if len(set(outputs)) != len(outputs) or any(path in input_paths for path in outputs):
        raise SystemExit("output paths must be distinct from each other and all inputs")
    if any(path.exists() for path in outputs):
        raise SystemExit("output already exists; choose a new run filename to preserve raw results")
    return query_rows


def provenance(args, query_path: pathlib.Path) -> dict:
    records = {}
    for path in (args.base, query_path, getattr(args, "base_graph", None), getattr(args, "query_graph", None)):
        if path is None:
            continue
        resolved = str(path.resolve())
        if resolved in records:
            continue
        digest = hashlib.sha256()
        with path.open("rb") as source:
            while block := source.read(1024 * 1024):
                digest.update(block)
        records[resolved] = {"sha256": digest.hexdigest(), "bytes": path.stat().st_size}
    cpu = "unknown"
    cpu_info = pathlib.Path("/proc/cpuinfo")
    if cpu_info.exists():
        cpu = next((line.split(":", 1)[1].strip() for line in cpu_info.read_text().splitlines()
                    if line.startswith("model name")), cpu)
    return {
        "schema_version": 2,
        "argv": sys.argv,
        "arguments": {key: str(value.resolve()) if isinstance(value, pathlib.Path) else value
                      for key, value in vars(args).items()},
        "input_files": records,
        "host": {"platform": platform.platform(), "cpu": cpu,
                 "logical_cpus": os.cpu_count(), "python": platform.python_version()},
        "distance_precision": "float32 accumulation; no quantization",
        "timing_policy": "total_online_ms=mean_online_order_ms+search_ms; excludes index, offline MST, IO and ground truth",
    }
