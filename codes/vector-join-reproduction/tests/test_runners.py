#!/usr/bin/env python3
"""Exercise all standalone Python runners against tiny raw-f32 inputs."""

from __future__ import annotations

import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile


project_dir = pathlib.Path(sys.argv[1]).resolve()
module_dir = pathlib.Path(sys.argv[2]).resolve()
runners = [
    "run_independent.py",
    "run_simjoin.py",
    "run_kjoinslide.py",
    "run_fast_avj_soft.py",
    "run_fast_avj_merged.py",
    "run_fast_avj_adaptive.py",
]

with tempfile.TemporaryDirectory() as temporary_dir:
    data_dir = pathlib.Path(temporary_dir)
    base = data_dir / "base.f32"
    queries = data_dir / "queries.f32"
    base.write_bytes(struct.pack("8f", 0.0, 0.0, 0.1, 0.0, 5.0, 5.0, 5.1, 5.0))
    queries.write_bytes(struct.pack("4f", 0.05, 0.0, 5.05, 5.0))
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(module_dir) + os.pathsep + environment.get("PYTHONPATH", "")
    for runner in runners:
        command = [
                sys.executable,
                str(project_dir / "scripts" / runner),
        ]
        if runner in ("run_simjoin.py", "run_kjoinslide.py"):
            command.extend([
                "--base", str(base), "--queries", str(queries), "--dim", "2",
                "--w", "4",
                "--order", "window-order-sort", "--graph-m", "4",
                "--graph-source", "hnswlib", "--ef-construction", "20", "--seed", "100",
                "--ground-truth-limit", "2", "--repeat", "1",
                "--dataset-name", "test", "--preprocessing", "none",
                "--split-description", "fixed-test-split",
                "--output", str(data_dir / (runner + ".jsonl")),
            ])
            command.extend(["--epsilon", "0.15"] if runner == "run_simjoin.py"
                           else ["--k", "2"])
        else:
            command.extend([
                "--base", str(base), "--queries", str(queries), "--dim", "2",
                "--threshold", "0.02", "--m", "4", "--ef-construction", "20",
                "--convergence", "20", "--ground-truth-limit", "2",
                "--repeat", "1", "--dataset-name", "test", "--preprocessing", "none",
                "--split-description", "fixed-test-split", "--output", str(data_dir / (runner + ".jsonl")),
            ])
        completed = subprocess.run(
            command,
            check=True,
            capture_output=True,
            env=environment,
            text=True,
        )
        metrics = json.loads(completed.stdout)
        assert metrics["graph_backend"] == "hnswlib"
        if runner in ("run_simjoin.py", "run_kjoinslide.py"):
            assert metrics["implementation_status"] == "paper-derived-graph-backend-deviation"
            assert metrics["implementation_language"] == "C++20"
            assert metrics["distance_metric"] == "l2"
            assert metrics["average_precision"] == 1.0
        else:
            assert metrics["implementation_language"] == "C++20"
            assert metrics["distance_metric"] == "squared_l2"
            assert metrics["graph_m"] == 4
            assert metrics["ef_construction"] == 20
            assert metrics["precision"] == 1.0

print("test_runners: OK")
