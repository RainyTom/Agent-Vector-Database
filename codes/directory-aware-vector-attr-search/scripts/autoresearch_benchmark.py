#!/usr/bin/env python3
"""Fixed full-WIKI evaluator. Research iterations must not modify this file."""
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
CODE = ROOT / "codes/directory-aware-vector-attr-search"
FIXTURE = CODE / "data/wiki_full"
DATA = ROOT / "researches/nested-attr-vector-search/2026-10-28-exps/data"
REFERENCE = DATA / "autoresearch_reference.json"
MANIFEST = ROOT / "researches/nested-attr-vector-search/2026-10-27-exps/data/wiki_full_inputs.json"
WORK = ROOT / "production/nested-attr-vector-search/work/autoresearch_trie"
N, DIM, K, REPEATS = 1941679, 1024, 10, 3
MIN_RECALL = 0.999
MAX_RSS_KIB = 24 * 1024 * 1024
MAX_BUILD_SECONDS = 180
MAX_NONRECURSIVE_MS = 0.025


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def check_inputs():
    manifest = json.loads(MANIFEST.read_text())
    expected = {f"{name}.f32": info["output_sha256"] for name, info in manifest["inputs"].items()}
    expected.update(manifest["fixture_hashes"])
    for name, fingerprint in expected.items():
        require(sha256(FIXTURE / name) == fingerprint, f"input changed: {name}")
    return expected


def load_scopes():
    tree = np.loadtxt(FIXTURE / "wiki_tree.txt", dtype=np.int32, skiprows=1)
    bindings = np.loadtxt(FIXTURE / "wiki_bindings.txt", dtype=np.int32)
    children = [[] for _ in range(len(tree))]
    for node, parent in tree[1:]:
        children[int(parent)].append(int(node))
    tin, tout = np.empty(len(tree), dtype=np.int32), np.empty(len(tree), dtype=np.int32)
    stack, clock = [(0, False)], 0
    while stack:
        node, exiting = stack.pop()
        if exiting:
            tout[node] = clock
        else:
            tin[node] = clock
            clock += 1
            stack.append((node, True))
            stack.extend((child, False) for child in reversed(children[node]))
    require(clock == len(tree) and len(bindings) == N, "invalid tree/binding size")
    workload = np.loadtxt(FIXTURE / "workload_wiki.txt", dtype=np.int32, skiprows=1)
    require(workload.shape == (456, 3), "workload shape changed")
    return bindings, tin, tout, workload


def scope_mask(bindings, tin, tout, query):
    _, recursive, anchor = query
    if recursive:
        positions = tin[bindings]
        return (positions >= tin[anchor]) & (positions < tout[anchor])
    return bindings == anchor


def prepare_reference():
    """Setup only: independent NumPy squared-L2 GT, not the C++ implementation."""
    fingerprints = check_inputs()
    bindings, tin, tout, workload = load_scopes()
    base = np.memmap(FIXTURE / "base.f32", mode="r", dtype="<f4", shape=(N, DIM))
    test = np.memmap(FIXTURE / "test.f32", mode="r", dtype="<f4", shape=(456, DIM))
    records = []
    for qi, query in enumerate(workload):
        ids = np.flatnonzero(scope_mask(bindings, tin, tout, query))
        best_ids, best_distances = np.empty(0, dtype=np.int64), np.empty(0, dtype=np.float32)
        for start in range(0, len(ids), 8192):
            block_ids = ids[start:start + 8192]
            delta = base[block_ids] - test[query[0]]
            distances = np.einsum("ij,ij->i", delta, delta, dtype=np.float32)
            merged_ids = np.concatenate((best_ids, block_ids))
            merged_distances = np.concatenate((best_distances, distances))
            order = np.lexsort((merged_ids, merged_distances))[:K]
            best_ids, best_distances = merged_ids[order], merged_distances[order]
        records.append({"query_index": qi, "query_id": int(query[0]), "recursive": bool(query[1]), "anchor": int(query[2]), "scope_card": len(ids), "gt_ids": best_ids.tolist()})
    require(sum(len(r["gt_ids"]) for r in records) == 2349, "unexpected GT pair count")
    DATA.mkdir(parents=True, exist_ok=True)
    REFERENCE.write_text(json.dumps({"inputs": fingerprints, "distance": "NumPy float32 squared L2; ties by ascending global ID", "queries": records}, indent=2) + "\n")
    print(f"Prepared {len(records)} independent GT records: {REFERENCE}", flush=True)


def quantile(values, p):
    values = sorted(values)
    x = (len(values) - 1) * p
    lo = int(x)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (x - lo)


def validate_rows(rows, reference, scopes):
    bindings, tin, tout, workload = scopes
    require(len(rows) == len(reference), "missing or extra query rows")
    indexed = {int(r["query_index"]): r for r in rows}
    require(set(indexed) == set(range(len(reference))), "duplicate/missing query indices")
    groups = {True: [], False: []}
    for ref in reference:
        qi = ref["query_index"]
        row = indexed[qi]
        require(row["design"] == "NODE" and row["dataset"] == "wiki", "unexpected method/workload")
        require(row["query_type"] == ("recursive" if ref["recursive"] else "non-recursive"), "query type changed")
        require(int(row["scope_card"]) == ref["scope_card"], "scope count changed")
        ids = list(map(int, row["result_ids"].split()))
        require(len(ids) == K and all(i == -1 or 0 <= i < N for i in ids), "invalid returned ID")
        valid = [i for i in ids if i >= 0]
        require(len(valid) == len(set(valid)), "duplicate returned ID")
        seen_sentinel = False
        for i in ids:
            if i == -1:
                seen_sentinel = True
            else:
                require(not seen_sentinel, "valid result after missing-result sentinel")
        for i in valid:
            node = bindings[i]
            eligible = tin[ref["anchor"]] <= tin[node] < tout[ref["anchor"]] if ref["recursive"] else node == ref["anchor"]
            require(eligible, f"out-of-scope result for query {qi}: {i}")
        hits = len(set(valid).intersection(ref["gt_ids"]))
        gt_count = len(ref["gt_ids"])
        require(int(row["gt_count"]) == gt_count and int(row["hit_count"]) == hits, "reported quality differs from independent GT")
        require(int(row["valid_return_count"]) == len(valid), "reported valid count differs from returned IDs")
        elapsed, directory, search = (float(row[k]) for k in ("e2e_ms", "dir_us", "search_us"))
        require(all(math.isfinite(v) and v >= 0 for v in (elapsed, directory, search)) and elapsed > 0, "invalid query time")
        require(abs(elapsed - (directory + search) / 1000) <= max(0.0001, elapsed * 0.0001), "inconsistent timing decomposition")
        groups[ref["recursive"]].append((hits, gt_count, elapsed))
    result = {}
    for recursive, values in groups.items():
        prefix = "recursive" if recursive else "nonrecursive"
        nonempty = [(h, g) for h, g, _ in values if g]
        result[f"{prefix}_macro_recall"] = statistics.mean(h / g for h, g in nonempty)
        result[f"{prefix}_pair_recall"] = sum(h for h, _, _ in values) / sum(g for _, g, _ in values)
        result[f"{prefix}_e2e_ms"] = statistics.mean(t for _, _, t in values)
        result[f"{prefix}_p95_ms"] = quantile([t for _, _, t in values], 0.95)
    require(result["recursive_macro_recall"] >= MIN_RECALL and result["recursive_pair_recall"] >= MIN_RECALL, "recursive recall below 0.999")
    require(result["nonrecursive_macro_recall"] == 1 and result["nonrecursive_pair_recall"] == 1, "nonrecursive recall regression")
    require(result["nonrecursive_e2e_ms"] <= MAX_NONRECURSIVE_MS, "nonrecursive latency guard exceeded")
    return result


def main():
    fingerprints = check_inputs()
    reference = json.loads(REFERENCE.read_text())
    require(reference["inputs"] == fingerprints, "GT/input fingerprint mismatch")
    scopes = load_scopes()
    env = os.environ.copy()
    env.update({k: "1" for k in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "BLIS_NUM_THREADS", "VECLIB_MAXIMUM_THREADS")})
    env["LD_LIBRARY_PATH"] = str(ROOT / "deps/PathSeer/faiss/build/faiss")
    WORK.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=WORK))
    print(f"ARTIFACTS {out}", flush=True)
    with (out / "build.log").open("w") as log:
        subprocess.run(["bash", str(CODE / "build.sh")], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    runs = []
    for repeat in range(REPEATS):
        prefix = out / f"repeat_{repeat}"
        cmd = [str(CODE / "build/repro"), "--data", str(FIXTURE), "--dim", str(DIM), "--mode", "dsq", "--executor", "node", "--node-hnsw-threshold", "0", "--node-ef", "64", "--csv", str(prefix)]
        timed = ["/usr/bin/time", "-v", "-o", str(prefix) + ".time.txt", *cmd]
        with Path(str(prefix) + ".log").open("w") as log:
            subprocess.run(timed, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        text = Path(str(prefix) + ".log").read_text()
        require("RESULT_SCOPE_WARNING" not in text, "scope/duplicate warning emitted")
        with Path(str(prefix) + "_dsq_perquery.csv").open() as f:
            metrics = validate_rows(list(csv.DictReader(f)), reference["queries"], scopes)
        match = re.search(r"executor NODE[^\n]* build_s=([0-9.eE+-]+)", text)
        require(match is not None, "missing index build time")
        build = float(match.group(1))
        rss_match = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", Path(str(prefix) + ".time.txt").read_text())
        require(rss_match is not None, "missing process RSS")
        rss = int(rss_match.group(1))
        require(math.isfinite(build) and 0 < build <= MAX_BUILD_SECONDS, "index construction exceeds 180 s")
        require(0 < rss <= MAX_RSS_KIB, "process peak RSS exceeds 24 GiB")
        metrics.update(build_s=build, peak_rss_mib=rss / 1024)
        runs.append(metrics)
        print(f"REPEAT {repeat} {json.dumps(metrics, sort_keys=True)}", flush=True)
    med = lambda key: statistics.median(r[key] for r in runs)
    result = {"recursive_e2e_ms": med("recursive_e2e_ms"), "recursive_p95_ms": med("recursive_p95_ms"), "recursive_macro_recall": min(r["recursive_macro_recall"] for r in runs), "recursive_pair_recall": min(r["recursive_pair_recall"] for r in runs), "nonrecursive_e2e_ms": med("nonrecursive_e2e_ms"), "nonrecursive_macro_recall": min(r["nonrecursive_macro_recall"] for r in runs), "build_s": med("build_s"), "peak_rss_mib": max(r["peak_rss_mib"] for r in runs)}
    result["recursive_qps"] = 1000 / result["recursive_e2e_ms"]
    result["recursive_e2e_mad_ms"] = statistics.median(abs(r["recursive_e2e_ms"] - result["recursive_e2e_ms"]) for r in runs)
    (out / "summary.json").write_text(json.dumps({"metrics": result, "repeats": runs, "command": cmd, "input_hashes": fingerprints}, indent=2) + "\n")
    for name, value in result.items():
        print(f"METRIC {name}={value:.12g}", flush=True)


if __name__ == "__main__":
    try:
        if sys.argv[1:] == ["--prepare-reference"]:
            prepare_reference()
        elif sys.argv[1:]:
            raise ValueError("unsupported benchmark arguments")
        else:
            main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"BENCHMARK_FAILED {error}", file=sys.stderr)
        sys.exit(1)
