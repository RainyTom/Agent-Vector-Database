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
    "run_simjoin_hard.py",
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
        completed = subprocess.run(
            [
                sys.executable,
                str(project_dir / "scripts" / runner),
                "--base", str(base),
                "--queries", str(queries),
                "--dim", "2",
                "--threshold", "0.02",
                "--m", "4",
                "--ef-construction", "20",
                "--convergence", "20",
                "--ground-truth-limit", "2",
            ],
            check=True,
            capture_output=True,
            env=environment,
            text=True,
        )
        metrics = json.loads(completed.stdout)
        assert metrics["graph_backend"] == "hnswlib"
        assert metrics["implementation_language"] == "C++20"
        assert metrics["distance_metric"] == "squared_l2"
        assert metrics["graph_m"] == 4
        assert metrics["ef_construction"] == 20
        assert metrics["precision"] == 1.0

print("test_runners: OK")
