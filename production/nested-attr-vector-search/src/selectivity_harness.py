#!/usr/bin/env python3
"""Compile and run the attribute-filter selectivity sweep.

Primary metric: gmean_qps_r95 = geometric mean over the seven controlled
selectivity buckets (s=0.001..1.0) of the best query throughput that reaches
Recall@10 >= 0.95. Exact feasible-set scan is included as the low-selectivity
frontier; BF-HNSW and post-filter are the filtered-index candidates.

The C++ driver prints one row per (mode, bucket, method, variant) and a
summary JSON. This wrapper only handles build/run and METRIC/ASI formatting.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / "production" / "nested-attr-vector-search"
SRC = BASE / "src"
WORK = BASE / "work"
FIX = WORK / "fixtures_ccs"
BIN = WORK / "bin" / "selectivity_bench"
FAISS = ROOT / "deps" / "PathSeer" / "faiss"
FAISS_LIB = FAISS / "build" / "faiss"

TARGET_RECALL = 0.95


def compile_binary() -> None:
    src = SRC / "selectivity_bench.cpp"
    if not src.exists():
        raise SystemExit(f"missing source: {src}")
    lib = FAISS_LIB / "libfaiss.so"
    if not lib.exists():
        raise SystemExit(
            f"missing {lib}; build the PathSeer FAISS fork first "
            "(see researches/nested-attr-vector-search/07-laminar-edge-sharing/"
            "2026-10-20-exps/experiments/faiss-build.md)"
        )
    if BIN.exists() and BIN.stat().st_mtime >= src.stat().st_mtime:
        return
    BIN.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "g++", "-O3", "-march=native", "-ffast-math", "-DNDEBUG", "-std=c++17",
        f"-I{FAISS}", str(src), "-o", str(BIN),
        f"-L{FAISS_LIB}", "-lfaiss", f"-Wl,-rpath,{FAISS_LIB}",
    ]
    print("[harness] compiling selectivity_bench", file=sys.stderr)
    subprocess.run(cmd, check=True)


def parse_paths(argv: list[str]) -> tuple[Path, Path, list[str]]:
    fixtures, out_dir, rest = FIX, WORK / "selectivity", []
    i = 0
    while i < len(argv):
        if argv[i] == "--fixtures":
            fixtures = Path(argv[i + 1])
            i += 2
        elif argv[i] == "--out-dir":
            out_dir = Path(argv[i + 1])
            i += 2
        else:
            rest.append(argv[i])
            i += 1
    return fixtures, out_dir, rest


def run_benchmark(argv: list[str], fixtures: Path, out_dir: Path) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(BIN),
        "--fixtures", str(fixtures),
        "--out-dir", str(out_dir),
        *argv,
    ]
    env = dict(os.environ, OMP_NUM_THREADS="1")
    print("[harness] " + " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True, env=env)
    with open(out_dir / "summary.json", encoding="utf-8") as f:
        return json.load(f)


def main() -> int:
    fixtures, out_dir, rest = parse_paths(sys.argv[1:])
    if not fixtures.exists():
        raise SystemExit(
            f"missing fixtures: {fixtures}; generate with prep_ccs.py or "
            "export base.f32/test.f32/meta.txt"
        )
    compile_binary()
    summary = run_benchmark(rest, fixtures, out_dir)
    valid = bool(summary.get("valid", False))
    print(f"METRIC gmean_qps_r95={summary['gmean_qps_r95']:.3f}")
    print(f"METRIC gmean_policy_qps_r95={summary['gmean_policy_qps_r95']:.3f}")
    print(f"METRIC crossover_s={summary['crossover_s']:.6f}")
    print(f"METRIC build_s={summary['build_s']:.3f}")
    print(f"METRIC index_mb={summary['index_mb']:.3f}")
    print(f"METRIC tree_meta_mb={summary['tree_meta_mb']:.3f}")
    print(f"METRIC valid={1 if valid else 0}")
    print(f"METRIC target_recall={TARGET_RECALL}")
    print(f"ASI frontier={json.dumps(summary['frontier'])}")
    print(
        "ASI design=random+CCS-hierarchy predicates; exact/PASS/FAISS-BF-HNSW/post-filter; "
        "single-thread query"
    )
    print(
        f"ASI n={summary['n']} d={summary['d']} k={summary['k']} "
        f"nq_per_bucket={summary['nq_per_bucket']} "
        f"repeats={summary.get('repeats', 1)} seed={summary['seed']}"
    )
    if not valid:
        print("[harness] validation failed", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
