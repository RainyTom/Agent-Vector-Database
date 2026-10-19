#!/usr/bin/env python3
"""Benchmark harness: hierarchical (nested attribute) filtered vector top-k.

Runs the candidate in production/nested-attr-vector-search/{src,work}: builds
an index from the fixtures, runs a fixed query workload, then scores returned
ids against a precomputed exact oracle and prints METRIC lines.

Primary metric: qps_at_r95 = query throughput (queries/second, query loop only)
when Recall@10 >= 0.95 and the returned ids satisfy the hierarchy filter with
no duplicates; otherwise 0.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
BASEDIR = ROOT / "production" / "nested-attr-vector-search"
SRC = BASEDIR / "src"
WORK = BASEDIR / "work"
FIX = WORK / "fixtures"
BIN = WORK / "bin"
INDEX = WORK / "index"

N_BASE = 100_000
N_QUERIES = 1_000
K = 10
REPEATS = 3
TARGET_RECALL = 0.95
SEED = 20261020
FIXTURE_VERSION = "nested-v2"

SOURCE_HDF5 = ROOT / "datasets" / "sift-128-euclidean.hdf5"
DIFFICULTY = "sift100k, 85-node arity-4 depth-3 hierarchy, 1000 mixed-scope queries"


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, text=True, **kw)


def compile_binaries() -> None:
    BIN.mkdir(parents=True, exist_ok=True)
    common = ["g++", "-O3", "-march=native", "-ffast-math", "-DNDEBUG", "-std=c++17",
              f"-I{ROOT / 'deps' / 'hnswlib'}"]
    jobs = [
        (SRC / "prep.cpp", BIN / "prep"),
        (SRC / "baseline.cpp", BIN / "candidate"),
    ]
    for src, out in jobs:
        if out.exists() and out.stat().st_mtime >= src.stat().st_mtime:
            continue
        print(f"[harness] compiling {src.name}", file=sys.stderr)
        run(common + [str(src), "-o", str(out)])


def export_vectors() -> None:
    base_f32 = WORK / "base_all.f32"
    test_f32 = WORK / "test.f32"
    if base_f32.exists() and base_f32.stat().st_size == N_BASE * 128 * 4 and test_f32.exists():
        return
    import h5py  # noqa: PLC0415

    print("[harness] exporting vectors from HDF5", file=sys.stderr)
    with h5py.File(SOURCE_HDF5, "r") as f:
        train = f["train"][:N_BASE].astype(np.float32)
        test = f["test"][:].astype(np.float32)
    train.tofile(base_f32)
    test.tofile(test_f32)


def prepare_fixtures() -> None:
    FIX.mkdir(parents=True, exist_ok=True)
    version_file = FIX / ".version"
    if version_file.exists() and version_file.read_text().strip() == FIXTURE_VERSION:
        return
    print("[harness] generating workload fixtures", file=sys.stderr)
    for p in FIX.iterdir():
        if p.is_file():
            p.unlink()
    run([
        str(BIN / "prep"),
        "--base", str(WORK / "base_all.f32"),
        "--test", str(WORK / "test.f32"),
        "--out", str(FIX),
        "--base-n", str(N_BASE),
        "--dim", "128",
        "--test-n", "10000",
        "--nq", str(N_QUERIES),
        "--k", str(K),
        "--seed", str(SEED),
    ])
    version_file.write_text(FIXTURE_VERSION + "\n")


def build_index() -> float:
    if INDEX.exists():
        shutil.rmtree(INDEX)
    INDEX.mkdir(parents=True)
    t0 = time.perf_counter()
    run([
        str(BIN / "candidate"), "build",
        "--base", str(FIX / "base.f32"),
        "--tree", str(FIX / "tree.txt"),
        "--labels", str(FIX / "labels.bin"),
        "--meta", str(FIX / "meta.txt"),
        "--index-dir", str(INDEX),
    ], capture_output=True)
    return time.perf_counter() - t0


def run_queries() -> tuple[float, int]:
    ids_path = WORK / "ids.bin"
    timings_path = WORK / "timings.f64"
    t0 = time.perf_counter()
    proc = run([
        str(BIN / "candidate"), "query",
        "--base", str(FIX / "base.f32"),
        "--tree", str(FIX / "tree.txt"),
        "--meta", str(FIX / "meta.txt"),
        "--index-dir", str(INDEX),
        "--queries", str(FIX / "test.f32"),
        "--workload", str(FIX / "workload.txt"),
        "--out", str(ids_path),
        "--timings", str(timings_path),
        "--repeats", str(REPEATS),
    ], capture_output=True)
    wall = time.perf_counter() - t0
    qs = None
    for line in proc.stdout.splitlines():
        if line.startswith("QUERY_SECONDS "):
            qs = float(line.split()[1])
    if qs is None or qs <= 0:
        raise SystemExit("candidate did not report QUERY_SECONDS")
    if ids_path.stat().st_size != N_QUERIES * K * 4:
        raise SystemExit("candidate ids.bin has wrong size")
    if timings_path.stat().st_size != N_QUERIES * 8:
        raise SystemExit("candidate timings.f64 has wrong size")
    print(f"[harness] query wall={wall:.3f}s inner={qs:.3f}s", file=sys.stderr)
    return qs, len(proc.stdout)


def load_tree():
    lines = (FIX / "tree.txt").read_text().split()
    nodes = int(lines[0])
    parent = [-1] * nodes
    children = [[] for _ in range(nodes)]
    it = iter(lines[1:])
    for _ in range(nodes):
        v = int(next(it)); p = int(next(it))
        parent[v] = p
        if p >= 0:
            children[p].append(v)
    return parent, children


def evaluate(query_seconds: float) -> dict:
    parent, children = load_tree()
    labels = np.fromfile(FIX / "labels.bin", dtype=np.int32).reshape(N_BASE, 3)
    gt = np.fromfile(FIX / "gt.bin", dtype=np.int32).reshape(N_QUERIES, K)
    pred = np.fromfile(WORK / "ids.bin", dtype=np.int32).reshape(N_QUERIES, K)
    timings = np.fromfile(WORK / "timings.f64", dtype=np.float64)

    wl = (FIX / "workload.txt").read_text().split()
    nq, k = int(wl[0]), int(wl[1])
    pos = 2
    qnodes = []
    for _ in range(nq):
        w = int(wl[pos]); pos += 1
        qnodes.append([int(x) for x in wl[pos:pos + w]])
        pos += w

    # Per-node feasible masks, propagated from direct labels to ancestors.
    n_nodes = len(children)
    node_mask = np.zeros((n_nodes, N_BASE), dtype=bool)
    for j in range(3):
        l = labels[:, j]
        ok = l >= 0
        node_mask[l[ok], np.nonzero(ok)[0]] = True
    order = []
    stack = [0]
    while stack:
        v = stack.pop()
        order.append(v)
        stack.extend(children[v])
    for v in reversed(order):
        p = parent[v]
        if p >= 0:
            node_mask[p] |= node_mask[v]

    hits = 0
    violations = 0
    empty = 0
    for qi in range(N_QUERIES):
        feasible = node_mask[qnodes[qi][0]].copy()
        for u in qnodes[qi][1:]:
            feasible |= node_mask[u]
        preds = [int(x) for x in pred[qi]]
        valid = [x for x in preds if x >= 0]
        if len(set(valid)) != len(valid):
            violations += 1
        for x in valid:
            if x >= N_BASE or not feasible[x]:
                violations += 1
        if len(valid) == 0:
            empty += 1
        hits += len(set(valid) & set(int(x) for x in gt[qi]))

    recall = hits / (N_QUERIES * K)
    qps = N_QUERIES / query_seconds
    p95 = float(np.percentile(timings, 95))
    index_bytes = sum(p.stat().st_size for p in INDEX.rglob("*") if p.is_file())
    return {
        "recall_at_10": recall,
        "qps_raw": qps,
        "p95_ms": p95,
        "index_mb": index_bytes / (1024 * 1024),
        "violations": violations,
        "empty_queries": empty,
        "query_seconds": query_seconds,
    }


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    compile_binaries()
    export_vectors()
    prepare_fixtures()
    build_s = build_index()
    query_seconds, _ = run_queries()

    m = evaluate(query_seconds)
    gated = m["qps_raw"] if (m["recall_at_10"] >= TARGET_RECALL and m["violations"] == 0) else 0.0

    print(f"METRIC qps_at_r95={gated:.3f}")
    print(f"METRIC recall_at_10={m['recall_at_10']:.4f}")
    print(f"METRIC qps_raw={m['qps_raw']:.3f}")
    print(f"METRIC p95_ms={m['p95_ms']:.4f}")
    print(f"METRIC build_s={build_s:.3f}")
    print(f"METRIC index_mb={m['index_mb']:.3f}")
    print(f"METRIC violations={m['violations']}")
    print(f"ASI difficulty={json.dumps(DIFFICULTY)}")
    print(f"ASI target_recall={TARGET_RECALL}")
    print(f"ASI n_base={N_BASE} n_queries={N_QUERIES} k={K} repeats={REPEATS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
