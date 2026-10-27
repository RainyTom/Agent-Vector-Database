#!/usr/bin/env python3
"""Prepare byte-faithful full WIKI inputs and run the two baseline families serially.
Run with production/nested-attr-vector-search/work/venv/bin/python from repo root.
Build codes/directory-aware-vector-attr-search/build/repro first (build.sh).
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[4]
DAY = Path(__file__).resolve().parents[1]
CODE = ROOT / "codes/directory-aware-vector-attr-search"
FIXTURE = CODE / "data/wiki_full"


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def prepare():
    FIXTURE.mkdir(parents=True, exist_ok=True)
    (DAY / "data").mkdir(exist_ok=True)
    sources = {
        "base": ROOT / "datasets/dbpedia_dir_2m_corpus_vectors.fvecs",
        "test": ROOT / "data/real_fixtures/wiki/query_vectors.fvecs",
    }
    metadata = {"dimension": 1024, "metric": "squared L2, raw published floats; no additional normalization", "inputs": {}}
    for kind, src in sources.items():
        size = src.stat().st_size
        assert size % 4100 == 0, (src, size)
        n = size // 4100
        assert n == (1941679 if kind == "base" else 456), (kind, n)
        dst = FIXTURE / f"{kind}.f32"
        # Always regenerate: an old same-size conversion can have wrong float values.
        subprocess.run([sys.executable, str(CODE / "scripts/fvecs_to_f32.py"), str(src), str(dst), "1024"], check=True)
        original = np.memmap(src, dtype="<f4", mode="r", shape=(n, 1025))
        converted = np.memmap(dst, dtype="<f4", mode="r", shape=(n, 1024))
        min_norm, max_norm = float("inf"), 0.0
        for start in range(0, n, 8192):
            x = original[start:start + 8192, 1:]
            y = converted[start:start + 8192]
            assert np.array_equal(x.view("<u4"), y.view("<u4")), (kind, start)
            assert np.isfinite(y).all(), (kind, start, "non-finite vectors")
            norms = np.linalg.norm(y, axis=1)
            min_norm = min(min_norm, float(norms.min()))
            max_norm = max(max_norm, float(norms.max()))
        metadata["inputs"][kind] = {"source": str(src.relative_to(ROOT)), "source_bytes": size, "records": n, "source_sha256": digest(src), "output_bytes": dst.stat().st_size, "output_sha256": digest(dst), "min_norm": min_norm, "max_norm": max_norm, "all_values_finite": True, "all_float_bits_preserved": True}
        del original, converted
    old = CODE / "data/real"
    for name in ("wiki_tree.txt", "wiki_bindings.txt"):
        shutil.copyfile(old / name, FIXTURE / name)
    workload_source = DAY / "data/wiki_full_workload.txt"
    lines = workload_source.read_text().splitlines()
    rows = [list(map(int, line.split())) for line in lines[1:]]
    usable = [row for row in rows if 0 <= row[0] < 456]
    assert len(usable) == 456 and {row[0] for row in usable} == set(range(456))
    (FIXTURE / "workload_wiki.txt").write_text(f"{len(usable)} 0\n" + "".join(" ".join(map(str, row)) + "\n" for row in usable))
    metadata["workload"] = {"source": str(workload_source.relative_to(ROOT)), "original_count": len(rows), "count": len(usable), "omitted_invalid_query_ids": [r[0] for r in rows if r not in usable], "recursive": sum(r[1] == 1 for r in usable), "non_recursive": sum(r[1] == 0 for r in usable), "constraint_seed": 20261026, "constraints": "synthetic depth-mixed anchors on the full real directory tree; not published query constraints"}
    bindings = np.loadtxt(FIXTURE / "wiki_bindings.txt", dtype=np.int32)
    tree_lines = (FIXTURE / "wiki_tree.txt").read_text().splitlines()
    metadata["tree_nodes"] = int(tree_lines[0])
    assert bindings.size == 1941679 and bindings.min() >= 0 and bindings.max() < metadata["tree_nodes"]
    counts = np.bincount(bindings, minlength=metadata["tree_nodes"])
    metadata["nonempty_nodes"] = int(np.count_nonzero(counts))
    metadata["max_local_items"] = int(counts.max())
    metadata["fixture_hashes"] = {n: digest(FIXTURE / n) for n in ("wiki_tree.txt", "wiki_bindings.txt", "workload_wiki.txt")}
    (DAY / "data/wiki_full_inputs.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2), flush=True)


def run():
    env = os.environ.copy()
    env.update(OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1", MKL_NUM_THREADS="1", BLIS_NUM_THREADS="1", VECLIB_MAXIMUM_THREADS="1", LD_LIBRARY_PATH=str(ROOT / "deps/PathSeer/faiss/build/faiss"))
    configs = [
        ("wiki_full_all3", ["--executor", "all3", "--ef", "2048", "--nprobe", "256", "--node-hnsw-threshold", "0", "--node-ef", "64"]),
        ("wiki_full_node_flat", ["--executor", "node", "--node-hnsw-threshold", "2147483647"]),
        ("wiki_full_ivf_exact", ["--executor", "ivf", "--nprobe", "1024"]),
    ]
    for label, params in configs:
        cmd = [str(CODE / "build/repro"), "--data", str(FIXTURE), "--dim", "1024", "--mode", "dsq", "--csv", str(DAY / "data" / label), *params]
        record = {"command": cmd, "thread_environment": {k: env[k] for k in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "BLIS_NUM_THREADS")}, "start_unix": time.time()}
        print("START", label, flush=True)
        with (DAY / "experiments" / f"{label}.log").open("w") as log:
            proc = subprocess.run(["/usr/bin/time", "-v", "-o", str(DAY / "experiments" / f"{label}.time.txt"), *cmd], env=env, stdout=log, stderr=subprocess.STDOUT)
        record.update(end_unix=time.time(), exit_code=proc.returncode)
        (DAY / "experiments" / f"{label}.command.json").write_text(json.dumps(record, indent=2) + "\n")
        print("END", label, "exit", proc.returncode, "elapsed_s", record["end_unix"] - record["start_unix"], flush=True)
        if proc.returncode:
            raise SystemExit(proc.returncode)
        print((DAY / "experiments" / f"{label}.log").read_text(), flush=True)


if __name__ == "__main__":
    prepare()
    run()
