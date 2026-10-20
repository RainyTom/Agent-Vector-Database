#!/usr/bin/env python3
"""Run fixed Subtree comparisons on five existing L2 vector subsets.

All hierarchy labels are synthetic (prep_ccs, seed 20261021). SIFT25k,
SIFT50k and SIFT100k are nested prefixes of the SAME SIFT corpus, not
three independent datasets. Fixed operating points are not matched-recall
or equal-memory comparisons; serialized FAISS, PSG, and LES indexes have
materially different representations. Query times exclude fixture creation,
bitmap construction, graph loading and PFS posting construction. The optional
native IndexPathSeerFlat configuration is not the full PathSeer recipe, and
virtual-overhead effects are not independently profiled.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SRC = Path(__file__).resolve().parent
PRODUCTION = SRC.parent
EXPS = ROOT / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"
NAMES = {"sift25k": (25_000, 128), "sift50k": (50_000, 128),
         "sift100k": (100_000, 128), "mnist25k": (25_000, 784),
         "fashion-mnist25k": (25_000, 784)}
REPEATS = 2
K = 10
THREAD_ENV = {"OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1"}
COLUMNS = ("dataset", "method", "status", "n", "dim", "nq", "k", "recall_at_10",
           "violations", "feasibility_violations", "duplicate_queries", "duplicate_ids",
           "invalid_ids", "unique_ids_total", "unique_ids_global",
           "unique_ids_mean_per_query", "full_k_queries",
           "filled_slots", "empty_queries", "query_seconds", "qps", "p95_ms",
           "base_graph_build_seconds", "les_dedup_seconds", "metadata_build_seconds",
           "metadata_validation_seconds", "index_load_seconds", "index_reused",
           "build_seconds", "metadata_bytes", "index_bytes", "index_bytes_definition",
           "config_json", "serialized_graph_path",
           "serialized_graph_sha256", "raw_metrics_path", "raw_metrics_sha256",
           "ids_path", "ids_sha256", "timings_path", "timings_sha256", "error")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def size_on_disk(path: Path) -> int:
    return sum(p.stat().st_size for p in path.rglob("*") if p.is_file())


def persist(run: dict, json_path: Path, csv_path: Path) -> None:
    for target_json, target_csv in ((json_path, csv_path),
                                    (Path(run["raw_logs_root"]) / "run.json",
                                     Path(run["raw_logs_root"]) / "run.csv")):
        draft = target_json.with_suffix(".json.tmp")
        draft.write_text(json.dumps(run, indent=2, ensure_ascii=False) + "\n")
        draft.replace(target_json)
        draft_csv = target_csv.with_suffix(".csv.tmp")
        with draft_csv.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=COLUMNS)
            writer.writeheader()
            for item in run["results"]:
                row = {key: item.get(key) for key in COLUMNS}
                row["config_json"] = json.dumps(item.get("config", {}), sort_keys=True)
                writer.writerow(row)
        draft_csv.replace(target_csv)


def invoke(cmd: list[str], log: Path, commands: list[dict], env: dict | None = None) -> float:
    changes = {**THREAD_ENV, **(env or {})}
    record = {"argv": cmd, "shell": shlex.join(cmd), "environment_overrides": changes,
              "log": str(log)}
    commands.append(record)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as stream:
        stream.write(json.dumps(record, indent=2) + "\n--- process output ---\n")
        stream.flush()
        start = time.perf_counter()
        proc = subprocess.run(cmd, cwd=ROOT, env={**os.environ, **changes},
                              stdout=stream, stderr=subprocess.STDOUT, check=False)
        elapsed = time.perf_counter() - start
        stream.write(f"\n--- exit={proc.returncode} wall_seconds={elapsed:.9f} ---\n")
    record.update(returncode=proc.returncode, wall_seconds=elapsed, log_sha256=sha256(log))
    if proc.returncode:
        raise RuntimeError(f"{shlex.join(cmd)} exited {proc.returncode}; see {log}")
    return elapsed


def reported_seconds(log: Path) -> float:
    values = re.findall(r"^QUERY_SECONDS\s+(\S+)", log.read_text(), re.MULTILINE)
    if len(values) != 1 or (seconds := float(values[0])) <= 0:
        raise ValueError(f"expected one positive QUERY_SECONDS in {log}")
    return seconds


def score(ids_path: Path, timings_path: Path, golden: np.ndarray,
          bitmap: np.memmap, n: int, nq: int, k: int) -> dict:
    if ids_path.stat().st_size != nq * k * 4 or timings_path.stat().st_size != nq * 8:
        raise ValueError(f"incorrect ids/timings sizes: {ids_path}, {timings_path}")
    ids = np.fromfile(ids_path, dtype=np.int32).reshape(nq, k)
    timings = np.fromfile(timings_path, dtype=np.float64)
    if not np.isfinite(timings).all() or (timings < 0).any():
        raise ValueError(f"invalid timing values: {timings_path}")
    hits = violations = feasibility_violations = duplicate_queries = duplicate_ids = invalid = 0
    unique_total = filled_slots = full_k = empty = 0
    global_ids = set()
    for qi, row in enumerate(ids):
        seen = set()
        row_dup = False
        for value in row:
            x = int(value)
            if x == -1:
                continue
            filled_slots += 1
            if x in seen:
                duplicate_ids += 1
                violations += 1
                row_dup = True
            seen.add(x)
            if x < 0 or x >= n:
                invalid += 1
                violations += 1
                feasibility_violations += 1
            elif not (int(bitmap[qi, x // 8]) & (1 << (x % 8))):
                violations += 1
                feasibility_violations += 1
            else:
                global_ids.add(x)
        duplicate_queries += int(row_dup)
        unique_total += len(seen)
        full_k += int(len(seen) == k and all(0 <= x < n for x in seen))
        empty += int(not seen)
        hits += len(seen.intersection(int(x) for x in golden[qi] if x >= 0))
    return {"recall_at_10": hits / (nq * k), "violations": violations,
            "feasibility_violations": feasibility_violations,
            "duplicate_queries": duplicate_queries, "duplicate_ids": duplicate_ids,
            "invalid_ids": invalid, "unique_ids_total": unique_total,
            "unique_ids_global": len(global_ids),
            "unique_ids_mean_per_query": unique_total / nq, "full_k_queries": full_k,
            "filled_slots": filled_slots, "empty_queries": empty,
            "p95_ms": float(np.percentile(timings, 95)),
            "ids_path": str(ids_path), "ids_sha256": sha256(ids_path),
            "timings_path": str(timings_path), "timings_sha256": sha256(timings_path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs-root", type=Path, default=PRODUCTION / "work/five_l2/inputs")
    parser.add_argument("--work-root", type=Path, default=PRODUCTION / "work/five_l2")
    parser.add_argument("--data-root", type=Path, default=EXPS / "data")
    parser.add_argument("--only", choices=NAMES)
    parser.add_argument("--nq", type=int, default=1000)
    parser.add_argument("--with-pathseer", action="store_true",
                        help="also run native IndexPathSeerFlat (requires enhanced faiss_filtered)")
    parser.add_argument("--with-triehi", action="store_true",
                        help="also run static Subtree TRIEHI-style directory on the same serialized BF-HNSW index")
    args = parser.parse_args()
    if args.nq < 1 or args.nq > 10_000:
        parser.error("--nq must be between 1 and 10000 (available test vectors)")
    inputs_root, work_root, data_root = (p.resolve() for p in
                                          (args.inputs_root, args.work_root, args.data_root))
    data_root.mkdir(parents=True, exist_ok=True)
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    log_root = EXPS / "experiments/five_l2" / run_id
    log_root.mkdir(parents=True, exist_ok=False)
    json_path, csv_path = (data_root / f"five_l2_{run_id}.{ext}" for ext in ("json", "csv"))
    bin_dir = PRODUCTION / "work/bin"
    faiss_lib = ROOT / "deps/PathSeer/faiss/build/faiss"
    ld_path = str(faiss_lib) + (":" + os.environ["LD_LIBRARY_PATH"]
                                if os.environ.get("LD_LIBRARY_PATH") else "")
    run = {"run_id": run_id, "status": "running", "input_root": str(inputs_root),
           "work_root": str(work_root), "data_root": str(data_root), "raw_logs_root": str(log_root),
           "dataset_order": [args.only] if args.only else list(NAMES),
           "nq_requested": args.nq, "k": K, "repeats": REPEATS,
           "with_pathseer": args.with_pathseer, "with_triehi": args.with_triehi,
           "thread_environment": THREAD_ENV,
           "query_time_policy": "best of two timed query repetitions; fixture generation excluded. BF-HNSW/PathSeer use precomputed query bitmaps, while triehi-hnsw resolves its candidate bitmap online inside query timing",
           "hierarchy": "synthetic prep_ccs 4-ary depth-5, 1365 nodes, seed 20261021; not an original hierarchy dataset",
           "provenance": "sift25k/sift50k/sift100k are overlapping nested prefixes of one SIFT HDF5 corpus; MNIST and Fashion-MNIST use distinct HDF5 corpora",
           "metric": "squared L2 on integer-valued [0,255] vectors (u8 and float32 preserve distance)",
           "comparability": "Fixed search breadth, not matched recall, build threads or memory. PSG directory bytes exclude fixture vectors and labels; LES directory includes base_u8 and upper levels; FAISS serialized index includes float32 vectors. TRIEHI scope metadata bytes are in-memory only and NOT included in serialized graph disk bytes. FAISS bitmap construction and PFS posting construction are separate from timed search. Native IndexPathSeerFlat with a bitmap selector does not reproduce the full PathSeer recipe; virtual-overhead effects are not isolated. No same-recall superiority is implied.",
           "datasets": {}, "results": []}
    tools = [SRC / "run_five_l2.py", SRC / "prep_ccs.py", SRC / "to_pathseer.py",
             *(bin_dir / name for name in ("candidate", "les_build", "les_query", "faiss_filtered")),
             faiss_lib / "libfaiss.so"]
    run["tool_artifacts"] = {str(p): sha256(p) if p.is_file() else None for p in tools}
    persist(run, json_path, csv_path)
    try:
        for name in run["dataset_order"]:
            n, dim = NAMES[name]
            source = inputs_root / name
            fixture = work_root / "fixtures" / name
            ps = work_root / "pathseer" / name
            index = work_root / "runs" / run_id / name
            psg, les = index / "psg", index / "les"
            raw = log_root / name
            raw.mkdir(parents=True)
            commands: list[dict] = []
            info = {"name": name, "source": str(source), "fixture": str(fixture),
                    "pathseer_dataset": str(ps), "index_dir": str(index),
                    "commands": commands, "status": "running"}
            run["datasets"][name] = info
            stage = "validate_inputs"
            try:
                input_paths = [source / "base.f32", source / "test.f32"]
                if not all(p.is_file() for p in input_paths):
                    raise FileNotFoundError(f"missing exported vectors in {source}; export existing HDF5 inputs first")
                if input_paths[0].stat().st_size != n * dim * 4:
                    raise ValueError(f"base.f32 size mismatch for {name}")
                if input_paths[1].stat().st_size < args.nq * dim * 4 or input_paths[1].stat().st_size % (dim * 4):
                    raise ValueError(f"test.f32 size mismatch for {name}")
                info["input_files"] = {p.name: {"path": str(p), "sha256": sha256(p)} for p in input_paths}
                metadata = source / "meta.json"
                if metadata.is_file():
                    info["input_metadata"] = json.loads(metadata.read_text())
                    info["input_files"]["meta.json"] = {"path": str(metadata), "sha256": sha256(metadata)}
                fixture.mkdir(parents=True, exist_ok=True)
                ps.mkdir(parents=True, exist_ok=True)
                psg.mkdir(parents=True, exist_ok=False)
                les.mkdir(parents=True, exist_ok=False)

                stage = "prepare_fixture"
                invoke([sys.executable, str(SRC / "prep_ccs.py"), "--fixtures", str(source),
                        "--dim", str(dim), "--n", str(n), "--nq", str(args.nq),
                        "--out", str(fixture)], raw / "prep.log", commands)
                meta = tuple(map(int, (fixture / "meta.txt").read_text().split()))
                if meta[:5] != (n, dim, args.nq, K, 1365):
                    raise ValueError(f"unexpected fixture dimensions/tree for {name}: {meta}")
                info["fixture_files"] = {fn: {"path": str(fixture / fn),
                                               "sha256": sha256(fixture / fn)}
                                         for fn in ("meta.txt", "tree.txt", "labels.bin",
                                                    "workload_sub.txt", "gt_sub.bin", "test.f32")}
                stage = "convert_pathseer"
                invoke([sys.executable, str(SRC / "to_pathseer.py"), "--fixtures", str(fixture),
                        "--out", str(ps)], raw / "pathseer_conversion.log", commands)
                checks = json.loads((ps / "check.json").read_text())
                if checks["failures"] or not checks["sub"]["golden_feasible"] or not checks["sub"]["popcount_matches"]:
                    raise ValueError(f"PathSeer converter checks failed: {ps / 'check.json'}")
                info["pathseer_checks"] = checks
                golden = np.fromfile(fixture / "gt_sub.bin", dtype=np.int32)
                if golden.size != args.nq * K:
                    raise ValueError("wrong Subtree ground-truth size")
                golden = golden.reshape(args.nq, K)
                bitmap_path = ps / "query/query_sub.bv"
                width = (n + 7) // 8
                if bitmap_path.stat().st_size != args.nq * width:
                    raise ValueError("wrong Subtree bitmap size")
                bitmap = np.memmap(bitmap_path, dtype=np.uint8, mode="r", shape=(args.nq, width))
                info["subtree_bitmap_sha256"] = sha256(bitmap_path)
                persist(run, json_path, csv_path)

                stage = "pfs_query"
                ids, timings, log = raw / "pfs_ids.bin", raw / "pfs_timings.f64", raw / "pfs_query.log"
                invoke([str(bin_dir / "candidate"), "query", "--mode", "subtree_scan",
                        "--base", str(fixture / "base.f32"), "--tree", str(fixture / "tree.txt"),
                        "--labels", str(fixture / "labels.bin"), "--meta", str(fixture / "meta.txt"),
                        "--queries", str(fixture / "test.f32"),
                        "--workload", str(fixture / "workload_sub.txt"),
                        "--out", str(ids), "--timings", str(timings),
                        "--repeats", str(REPEATS)], log, commands)
                seconds = reported_seconds(log)
                row = {"dataset": name, "method": "pfs_subtree_scan", "status": "ok",
                       "n": n, "dim": dim, "nq": args.nq, "k": K,
                       "config": {"mode": "subtree_scan", "repeats": REPEATS,
                                  "distance": "u8 squared L2"},
                       "query_seconds": seconds, "qps": args.nq / seconds,
                       "index_bytes_definition": "No persisted ANN index; direct-label postings and u8 base built in process outside timed query loop"}
                row.update(score(ids, timings, golden, bitmap, n, args.nq, K))
                run["results"].append(row)
                persist(run, json_path, csv_path)

                stage = "psg_build"
                base_build = invoke([str(bin_dir / "candidate"), "build", "--base", str(fixture / "base.f32"),
                                     "--tree", str(fixture / "tree.txt"), "--labels", str(fixture / "labels.bin"),
                                     "--meta", str(fixture / "meta.txt"), "--index-dir", str(psg)],
                                    raw / "psg_build.log", commands,
                                    {"GRAPH_M": "24", "GRAPH_EFC": "200"})
                psg_bytes = size_on_disk(psg)
                if not psg_bytes:
                    raise ValueError("PSG build emitted no index files")

                stage = "psg_query"
                ids, timings, log = (raw / "psg_ids.bin", raw / "psg_timings.f64", raw / "psg_query.log")
                invoke([str(bin_dir / "candidate"), "query", "--base", str(fixture / "base.f32"),
                        "--tree", str(fixture / "tree.txt"), "--meta", str(fixture / "meta.txt"),
                        "--index-dir", str(psg), "--queries", str(fixture / "test.f32"),
                        "--workload", str(fixture / "workload_sub.txt"), "--mode", "subtree",
                        "--out", str(ids), "--timings", str(timings), "--repeats", str(REPEATS)],
                       log, commands, {"GRAPH_EF": "21"})
                seconds = reported_seconds(log)
                row = {"dataset": name, "method": "psg", "status": "ok", "n": n, "dim": dim,
                       "nq": args.nq, "k": K, "config": {"graph_m": 24, "graph_efc": 200,
                                                        "graph_ef": 21, "repeats": REPEATS},
                       "query_seconds": seconds, "qps": args.nq / seconds,
                       "base_graph_build_seconds": base_build, "build_seconds": base_build,
                       "index_bytes": psg_bytes, "index_bytes_definition": "PSG c_*.bin disk bytes incl local u8 vectors, excluding separate fixture labels/base"}
                row.update(score(ids, timings, golden, bitmap, n, args.nq, K))
                run["results"].append(row)
                persist(run, json_path, csv_path)

                stage = "les_build"
                dedup_build = invoke([str(bin_dir / "les_build"), "--fixtures", str(fixture),
                                      "--index-dir", str(psg), "--out", str(les)],
                                     raw / "les_build.log", commands)
                les_bytes = size_on_disk(les)
                if not les_bytes:
                    raise ValueError("LES build emitted no index files")
                stage = "les_query"
                ids, timings, log = (raw / "les32_ids.bin", raw / "les32_timings.f64", raw / "les32_query.log")
                invoke([str(bin_dir / "les_query"), "--out-dir", str(les),
                        "--queries", str(ps / "query/query.bin"),
                        "--workload", str(fixture / "workload_sub.txt"), "--mode", "subtree",
                        "--k", str(K), "--ef", "32", "--repeats", str(REPEATS),
                        "--out", str(ids), "--timings", str(timings)], log, commands)
                seconds = reported_seconds(log)
                row = {"dataset": name, "method": "les_ef32", "status": "ok", "n": n, "dim": dim,
                       "nq": args.nq, "k": K,
                       "config": {"ef": 32, "source_graph_m": 24,
                                  "source_graph_efc": 200, "repeats": REPEATS},
                       "query_seconds": seconds, "qps": args.nq / seconds,
                       "base_graph_build_seconds": base_build, "les_dedup_seconds": dedup_build,
                       "build_seconds": base_build + dedup_build, "index_bytes": les_bytes,
                       "index_bytes_definition": "LES output directory disk bytes, includes u8 base and per-scope upper levels; source PSG graph omitted"}
                row.update(score(ids, timings, golden, bitmap, n, args.nq, K))
                run["results"].append(row)
                persist(run, json_path, csv_path)

                faiss_methods = ["hnsw"]
                if args.with_triehi:
                    faiss_methods.append("triehi-hnsw")
                if args.with_pathseer:
                    faiss_methods.append("pathseer")
                shared_hnsw = index / "bf_hnsw_index.faiss"
                hnsw_build_seconds = None
                for method in faiss_methods:
                    stage = f"faiss_{method}"
                    faiss_json, log = raw / f"faiss_{method}.json", raw / f"faiss_{method}.log"
                    faiss_ids = raw / f"faiss_{method}_ids.bin"
                    faiss_times = raw / f"faiss_{method}_timings.f64"
                    cmd = [str(bin_dir / "faiss_filtered"), "--dataset", str(ps),
                           "--method", method, "--mode", "sub", "--ef", "384",
                           "--M", "32", "--efc", "200", "--k", str(K),
                           "--repeats", str(REPEATS)]
                    if method == "pathseer":
                        cmd += ["--M-exp", "64", "--M-exp-search", "16"]
                    if args.with_triehi and method == "hnsw":
                        cmd += ["--index-out", str(shared_hnsw)]
                    elif method == "triehi-hnsw":
                        cmd += ["--index-in", str(shared_hnsw), "--scope-fixtures", str(fixture)]
                    cmd += ["--out", str(faiss_json), "--ids", str(faiss_ids),
                            "--timings", str(faiss_times)]
                    wall = invoke(cmd, log, commands, {"LD_LIBRARY_PATH": ld_path})
                    result = json.loads(faiss_json.read_text())
                    if result.get("method") != method or result.get("mode") != "sub":
                        raise ValueError(f"FAISS JSON method/mode mismatch: {faiss_json}")
                    if int(result["index_bytes"]) <= 0 or float(result["qps"]) <= 0:
                        raise ValueError(f"FAISS index bytes/qps not measured: {faiss_json}")
                    seconds = float(result["query_seconds"])
                    if seconds <= 0:
                        raise ValueError(f"FAISS query seconds not measured: {faiss_json}")
                    row = {"dataset": name, "method": {"hnsw": "bf_hnsw", "pathseer": "native_pathseer",
                                                        "triehi-hnsw": "triehi_hnsw"}[method],
                           "status": "ok", "n": n, "dim": dim, "nq": args.nq, "k": K,
                           "config": {"M": 32, "efc": 200, "ef": 384, "repeats": REPEATS,
                                      **({"M_exp": 64, "M_exp_search": 16} if method == "pathseer" else {})},
                           "recall_at_10": result["recall"], "violations": result["violations"],
                           "empty_queries": result["empty"], "query_seconds": seconds,
                           "qps": result["qps"], "p95_ms": result["p95_ms"],
                           "build_seconds": result["build_s"], "process_wall_seconds": wall,
                           "index_bytes": result["index_bytes"],
                           "index_bytes_definition": "FAISS serialized float32 index, excludes supplied bitmap",
                           "raw_metrics_path": str(faiss_json), "raw_metrics_sha256": sha256(faiss_json),
                           "measurement_limitation": {
                               "hnsw": "Precomputed query bitmaps supplied to BF-HNSW; bitmap construction excluded from timed search",
                               "triehi-hnsw": "Only a static Subtree directory resolver; no path-string lookup, DSM, or full directory-aware system",
                               "pathseer": "Native IndexPathSeerFlat with bitmap selector is not the full PathSeer recipe; virtual-overhead effects are not isolated",
                           }[method]}
                    if method == "hnsw" and args.with_triehi:
                        if not shared_hnsw.is_file() or shared_hnsw.stat().st_size != int(result["index_bytes"]):
                            raise ValueError(f"BF-HNSW shared graph missing/size differs from serialized index: {shared_hnsw}")
                        hnsw_build_seconds = float(result["build_s"])
                        row["base_graph_build_seconds"] = hnsw_build_seconds
                        row["serialized_graph_path"] = str(shared_hnsw)
                        row["serialized_graph_sha256"] = sha256(shared_hnsw)
                    elif method == "triehi-hnsw":
                        if hnsw_build_seconds is None:
                            raise ValueError("TRIEHI needs a completed BF-HNSW build")
                        metadata_seconds = float(result["metadata_build_seconds"])
                        metadata_bytes = int(result["metadata_bytes"])
                        if metadata_seconds < 0 or metadata_bytes <= 0 or not result["index_reused"]:
                            raise ValueError(f"TRIEHI shared graph/metadata not measured: {faiss_json}")
                        row["base_graph_build_seconds"] = hnsw_build_seconds
                        row["build_seconds"] = hnsw_build_seconds + metadata_seconds
                        row["metadata_build_seconds"] = metadata_seconds
                        row["metadata_bytes"] = metadata_bytes
                        row["index_load_seconds"] = float(result["index_load_seconds"])
                        row["serialized_graph_path"] = str(shared_hnsw)
                        row["metadata_validation_seconds"] = float(result["metadata_validation_seconds"])
                        row["index_reused"] = True
                        row["serialized_graph_sha256"] = sha256(shared_hnsw)
                        row["index_bytes_definition"] = "Shared FAISS graph serialized disk bytes; separately reported metadata_bytes is an in-memory directory, not serialized disk bytes"
                        row["scope_resolution"] = "inclusive per-node bitmap directory; U resolved in timed Subtree search and compared to oracle bitmaps"
                    row.update(score(faiss_ids, faiss_times, golden, bitmap, n, args.nq, K))
                    if (row["feasibility_violations"] != result["violations"]
                            or abs(row["recall_at_10"] - result["recall"]) > 1e-9):
                        raise ValueError(f"FAISS aggregate metrics disagree with scored IDs: {faiss_json}")
                    run["results"].append(row)
                    persist(run, json_path, csv_path)
                info["status"] = "ok"
                persist(run, json_path, csv_path)
            except Exception as exc:
                error = f"{type(exc).__name__}: {exc}"
                info.update(status="failed", failed_stage=stage, error=error)
                run["results"].append({"dataset": name, "method": stage, "status": "failed",
                                       "n": n, "dim": dim, "nq": args.nq, "k": K, "error": error})
                run.update(status="failed", error=error, failed_dataset=name, failed_stage=stage)
                persist(run, json_path, csv_path)
                print(f"[{name}/{stage}] {error}; logs: {raw}", file=sys.stderr)
                return 1
        run["status"] = "ok"
        persist(run, json_path, csv_path)
        print(f"results: {csv_path}\nmanifest: {json_path}\nraw logs: {log_root}")
        return 0
    except Exception as exc:
        run.update(status="failed", error=f"{type(exc).__name__}: {exc}")
        persist(run, json_path, csv_path)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
