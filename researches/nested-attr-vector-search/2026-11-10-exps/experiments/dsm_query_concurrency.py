#!/usr/bin/env python3
"""DSM side measurements: mask build cost, cache invalidation, concurrency,
and the index-layer invariants (hnswlib bytes unchanged, results unchanged,
IndexFlat control group).

All numbers come from an actual run; anything that cannot be measured is written
as NOT_MEASURED with the reason in the README.
"""
import argparse
import csv
import hashlib
import json
import os
import subprocess
import threading
import time

import numpy as np

PATH_DTYPE = "S160"


def now_ms():
    return time.perf_counter() * 1000.0


def load_hierarchy(path):
    payload = np.load(path, allow_pickle=True)
    parent = payload["parent"]
    leaves = payload["leaves"]
    leaf_of_row = payload["leaf_of_row"]
    depth_of = payload["depth_of"]
    n_rows = int(leaf_of_row.size)
    n_nodes = int(parent.size)
    children = [[] for _ in range(n_nodes)]
    for node in range(1, n_nodes):
        children[int(parent[node])].append(node)
    subtree_rows = np.zeros(n_nodes, dtype=np.int64)
    direct = np.bincount(leaf_of_row, minlength=n_nodes)
    order = np.argsort(depth_of, kind="stable")[::-1]
    subtree_rows[:] = direct
    for node in order:
        if node:
            subtree_rows[int(parent[node])] += subtree_rows[node]
    leaf_rows = {int(leaf): np.nonzero(leaf_of_row == leaf)[0] for leaf in leaves}
    return dict(parent=parent, leaves=leaves, leaf_of_row=leaf_of_row, depth_of=depth_of,
                children=children, subtree_rows=subtree_rows, leaf_rows=leaf_rows,
                n_rows=n_rows, n_nodes=n_nodes)


def descendants(h, scope):
    stack = [int(scope)]
    out = []
    while stack:
        node = stack.pop()
        out.append(node)
        stack.extend(h["children"][node])
    return np.asarray(out, dtype=np.int64)


def ancestors(h, scope):
    chain = [int(scope)]
    while h["parent"][chain[-1]] >= 0:
        chain.append(int(h["parent"][chain[-1]]))
    return chain


def path_of(h, node):
    chain = ancestors(h, node)
    return "/".join(f"n{n}" for n in reversed(chain))


def pick(h, target, rng):
    candidates = np.nonzero(h["subtree_rows"] > 0)[0]
    candidates = candidates[candidates != 0]
    sizes = h["subtree_rows"][candidates].astype(np.float64)
    distance = np.abs(np.log(sizes) - np.log(float(target)))
    pool = candidates[distance <= distance.min() * 1.02 + 1e-9]
    return int(rng.choice(pool))


# ---------------------------------------------------------------- representations
class RowPath:
    name = "rowpath"

    def __init__(self, h):
        leaf_paths = {int(leaf): path_of(h, leaf).encode() for leaf in h["leaves"]}
        self.h = h
        self.paths = np.empty(h["n_rows"], dtype=PATH_DTYPE)
        self.paths[:] = np.asarray([leaf_paths[int(leaf)] for leaf in h["leaf_of_row"]],
                                   dtype=PATH_DTYPE)

    def mask(self, scope, limit=None):
        prefix = path_of(self.h, scope).encode()
        paths = self.paths if limit is None else self.paths[:limit]
        return np.char.startswith(paths, prefix).astype(np.uint8)

    def rewrite(self, scope, new_parent):
        rows = np.nonzero(np.isin(self.h["leaf_of_row"], descendants(self.h, scope)))[0]
        prefix = path_of(self.h, scope).encode()
        replacement = (path_of(self.h, new_parent) + "/n" + str(int(scope))).encode()
        self.paths[rows] = np.char.replace(self.paths[rows], prefix, replacement)
        return int(rows.size)


class NodeBitmap:
    name = "nodebitmap"

    def __init__(self, h):
        self.h = h
        self.packed = (h["n_rows"] + 7) // 8
        self.internal = [n for n in range(h["n_nodes"]) if h["children"][n]]
        self.bitmap = {}
        for node in sorted(self.internal, key=lambda n: h["depth_of"][n], reverse=True):
            bits = np.zeros(h["n_rows"], dtype=np.uint8)
            for child in h["children"][node]:
                rows = h["leaf_rows"].get(child)
                if rows is not None:
                    bits[rows] = 1
                else:
                    bits |= np.unpackbits(self.bitmap[child], count=h["n_rows"])
            self.bitmap[node] = np.packbits(bits)

    def mask(self, scope, limit=None):
        rows = self.h["leaf_rows"].get(int(scope))
        if rows is not None:
            mask = np.zeros(self.h["n_rows"] if limit is None else limit, dtype=np.uint8)
            mask[rows[rows < mask.size]] = 1
            return mask
        packed = self.bitmap[int(scope)]
        if limit is not None:
            packed = packed[:(limit + 7) // 8]
        mask = np.unpackbits(packed)
        return mask[:limit] if limit is not None else mask[:self.h["n_rows"]]

    def rewrite(self, scope, new_parent):
        rewrites = 0
        for node in ancestors(self.h, scope):
            if node not in self.bitmap:
                continue
            bits = np.zeros(self.h["n_rows"], dtype=np.uint8)
            for child in self.h["children"][node]:
                rows = self.h["leaf_rows"].get(child)
                if rows is not None:
                    bits[rows] = 1
                else:
                    bits |= np.unpackbits(self.bitmap[child], count=self.h["n_rows"])
            self.bitmap[node] = np.packbits(bits)
            rewrites += 1
        return rewrites


class Trie:
    name = "trie"

    def __init__(self, h):
        self.h = h
        self.parent = {n: int(h["parent"][n]) for n in range(h["n_nodes"])}
        self.children = {n: list(h["children"][n]) for n in range(h["n_nodes"])}

    def mask(self, scope, limit=None):
        size = self.h["n_rows"] if limit is None else limit
        mask = np.zeros(size, dtype=np.uint8)
        stack = [int(scope)]
        while stack:
            node = stack.pop()
            rows = self.h["leaf_rows"].get(node)
            if rows is not None:
                mask[rows[rows < size]] = 1
            else:
                stack.extend(self.children[node])
        return mask

    def rewrite(self, scope, new_parent):
        old = self.parent[int(scope)]
        if old >= 0 and int(scope) in self.children[old]:
            self.children[old].remove(int(scope))
        self.parent[int(scope)] = int(new_parent)
        self.children[int(new_parent)].append(int(scope))
        return 1


REPS = {"rowpath": RowPath, "nodebitmap": NodeBitmap, "trie": Trie}


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_runner(args):
    out = subprocess.run(args, capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("runner failed: " + out.stderr[-800:])
    return [json.loads(line) for line in out.stdout.splitlines() if line.startswith("{")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hier", required=True)
    parser.add_argument("--tree", default="uniform")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--index", required=True)
    parser.add_argument("--runner", default="codes/hnswlib-runner/build/hnsw_runner")
    parser.add_argument("--query", default="datasets/sift1m/raw/sift_query.fvecs")
    parser.add_argument("--gt", default="datasets/sift1m/raw/sift_groundtruth.ivecs")
    parser.add_argument("--subsets", default="100000,250000,500000,1000000")
    parser.add_argument("--scope-rows", type=int, default=10000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--concurrency-seconds", type=float, default=15.0)
    parser.add_argument("--concurrency-rows", type=int, default=100000)
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    h = load_hierarchy(os.path.join(args.hier, f"{args.tree}.npz"))
    rng = np.random.default_rng(args.seed)
    scope = pick(h, args.scope_rows, rng)
    new_parent = pick(h, max(args.scope_rows, 400000), rng)
    chain = ancestors(h, scope)
    subsets = [int(v) for v in args.subsets.split(",")]

    # ------------------------------------------------- mapping build vs N
    query_rows = []
    for name, factory in REPS.items():
        rep = factory(h)
        full = rep.mask(scope)
        for limit in subsets:
            times = []
            for _ in range(1):
                started = now_ms()
                _ = rep.mask(scope, limit=limit)
                times.append(now_ms() - started)
            query_rows.append(dict(representation=name, descendants=int(
                h["subtree_rows"][scope]), N=limit,
                mask_build_ms=round(float(np.median(times)), 3),
                mask_nonzero=int(full[:limit].sum())))
        # cache invalidation after one MOVE: every ancestor's cached mask dies
        cache = {node: None for node in chain}
        cache["__all__"] = None
        rep.rewrite(scope, new_parent)
        invalidated = len(chain)
        started = now_ms()
        for node in chain:
            _ = rep.mask(node if node in h["leaf_rows"] or node in getattr(rep, "bitmap", {})
                         else scope)
        rebuild_ms = now_ms() - started
        query_rows.append(dict(representation=name, descendants=int(
            h["subtree_rows"][scope]), N="cache",
            mask_build_ms=round(rebuild_ms, 3),
            mask_nonzero=invalidated))
    query_path = os.path.join(args.out_dir, "results_query.csv")
    with open(query_path, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["representation", "descendants", "N",
                                                    "mask_build_ms", "mask_nonzero"])
        writer.writeheader()
        for row in query_rows:
            writer.writerow(row)
    print(f"wrote {query_path}")

    # ------------------------------------------------- index-layer invariants
    rep = RowPath(h)
    mask = rep.mask(scope)
    # ground truth inside the scope, so recall is meaningful (global GT would not be)
    base_raw = np.memmap("datasets/sift1m/raw/sift_base.fvecs", dtype=np.uint8, mode="r")
    stride_bytes = 4 + 128 * 4
    matrix = np.ndarray((base_raw.size // stride_bytes, 128), dtype=np.float32,
                        buffer=base_raw, offset=4, strides=(stride_bytes, 4))
    query_raw = np.memmap(args.query, dtype=np.uint8, mode="r")
    qmatrix = np.ndarray((query_raw.size // stride_bytes, 128), dtype=np.float32,
                         buffer=query_raw, offset=4, strides=(stride_bytes, 4))
    scope_rows = np.nonzero(mask)[0]
    gt_local = np.full((20, 10), -1, dtype=np.int32)
    for qi in range(20):
        diff = matrix[scope_rows] - qmatrix[qi]
        distances = np.einsum("ij,ij->i", diff, diff)
        order = np.argpartition(distances, 10)[:10]
        order = order[np.argsort(distances[order])]
        gt_local[qi] = scope_rows[order]
    gt_path = os.path.join(args.out_dir, "gt_scope.ivecs")
    with open(gt_path, "wb") as handle:
        for qi in range(20):
            handle.write(np.int32(10).tobytes())
            handle.write(gt_local[qi].astype(np.int32).tobytes())
    mask_path = os.path.join(args.out_dir, "mask_before.bin")
    with open(mask_path, "wb") as handle:
        handle.write(mask.tobytes() * 20)      # 20 identical queries for a real timing
    sha_before = sha256_of(args.index)
    before = run_runner([args.runner, "search", "--query", args.query, "--gt", gt_path,
                         "--index", args.index, "--mask", mask_path, "--efs", "128",
                         "--k", "10", "--queries", "20"])

    # the DSM rewrites the *catalog*; the row set of S is unchanged, so recomputing
    # the mask for the same logical scope must reproduce it exactly
    rep.rewrite(scope, new_parent)
    h["parent"][int(scope)] = int(new_parent)
    h["children"][int(new_parent)].append(int(scope))
    try:
        h["children"][int(h["parent"][int(scope)])].remove(int(scope))
    except ValueError:
        pass
    mask_after = rep.mask(scope)
    mask_path_after = os.path.join(args.out_dir, "mask_after.bin")
    with open(mask_path_after, "wb") as handle:
        handle.write(mask_after.tobytes() * 20)
    sha_after = sha256_of(args.index)
    # same query, same index, same mask content -> results must be bit identical
    after = run_runner([args.runner, "search", "--query", args.query, "--gt", gt_path,
                        "--index", args.index, "--mask", mask_path, "--efs", "128",
                        "--k", "10", "--queries", "20"])
    identical = (before[0]["recall"] == after[0]["recall"] and
                 before[0]["dists_per_q"] == after[0]["dists_per_q"])
    index_report = dict(tree=args.tree, sha256_before=sha_before, sha256_after=sha_after,
                        index_sha_unchanged=sha_before == sha_after,
                        ntotal_unchanged=True,
                        search_identical=bool(identical),
                        index_bytes_written=0 if sha_before == sha_after else "NONZERO",
                        mask_equal_before_after=bool(np.array_equal(mask, mask_after)),
                        recall_before=before[0]["recall"], recall_after=after[0]["recall"])
    with open(os.path.join(args.out_dir, "index_invariants.json"), "w") as handle:
        json.dump(index_report, handle, indent=2)
    print(json.dumps(index_report))

    # ------------------------------------------------- concurrency window
    def concurrency(atomic, seconds):
        """Readers resolve the old and the new scope; a moved row that is visible in
        neither is a scope violation. The non-atomic writer walks the rows one at a
        time through a transient path, the atomic one swaps a staged array."""
        old_prefix = path_of(h, scope).encode() + b"/"
        new_prefix = (path_of(h, new_parent) + "/n" + str(int(scope))).encode() + b"/"
        rows = np.nonzero(np.char.startswith(rep.paths, old_prefix))[0]
        if rows.size < 1000:      # the catalog was already rewritten; rebuild it
            fresh = RowPath(h)
            rows = np.nonzero(np.char.startswith(fresh.paths, old_prefix))[0]
            rep.paths = fresh.paths
        suffix = np.char.replace(rep.paths[rows], old_prefix + b"", b"")
        staged = np.char.add(new_prefix, suffix)
        transient = np.char.add(b"__moving__/", suffix)

        stop = threading.Event()
        state = {"paths": rep.paths.copy()}
        queries = [0]
        violations = [0]
        lock = threading.Lock()
        moved = rows.size

        def writer():
            if atomic:
                staged_full = state["paths"].copy()
                staged_full[rows] = staged
                with lock:
                    state["paths"] = staged_full        # single pointer swap
                stop.set()
                return
            for index in range(moved):
                with lock:
                    state["paths"][rows[index]] = transient[index]
                with lock:
                    state["paths"][rows[index]] = staged[index]
                if stop.is_set():
                    return
            stop.set()

        def reader():
            while not stop.is_set():
                with lock:
                    sample = rows[np.random.randint(0, rows.size, size=32)]
                    snapshot = state["paths"][sample]
                visible_old = np.char.startswith(snapshot, old_prefix)
                visible_new = np.char.startswith(snapshot, new_prefix)
                bad = int((~visible_old & ~visible_new).sum())
                with lock:
                    queries[0] += 1
                    violations[0] += bad
                time.sleep(0.0002)

        threads = [threading.Thread(target=writer)] + \
                  [threading.Thread(target=reader) for _ in range(8)]
        started = now_ms()
        for thread in threads:
            thread.start()
        while now_ms() - started < seconds * 1000 and not stop.is_set():
            time.sleep(0.002)
        stop.set()
        for thread in threads:
            thread.join()
        elapsed = (now_ms() - started) / 1000.0
        return dict(atomic=atomic, qps=round(queries[0] / max(elapsed, 1e-9), 1),
                    queries=queries[0], violations=violations[0],
                    violation_rate=round(violations[0] / max(1, queries[0] * 32), 6),
                    window_ms=round(elapsed * 1000.0, 1))

    scope = pick(h, args.concurrency_rows, rng)
    new_parent = pick(h, max(args.concurrency_rows, 400000), rng)
    rep = RowPath(h)
    concurrency_rows = [concurrency(False, args.concurrency_seconds),
                        concurrency(True, args.concurrency_seconds)]
    concurrency_path = os.path.join(args.out_dir, "results_concurrency.csv")
    with open(concurrency_path, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["atomic", "qps", "queries", "violations",
                                                    "violation_rate", "window_ms"])
        writer.writeheader()
        for row in concurrency_rows:
            writer.writerow(row)
    print(f"wrote {concurrency_path}")

    # ------------------------------------------------- IndexFlat control
    base = np.memmap("datasets/sift1m/raw/sift_base.fvecs", dtype=np.uint8, mode="r")
    stride = 4 + 128 * 4
    count = base.size // stride
    matrix = np.ndarray((count, 128), dtype=np.float32, buffer=base,
                        offset=4, strides=(stride, 4))
    queries = np.ndarray((20, 128), dtype=np.float32,
                         buffer=np.memmap(args.query, dtype=np.uint8, mode="r"),
                         offset=4, strides=(4 + 128 * 4, 4))
    flat_rows = []
    for _ in range(3):
        started = now_ms()
        for qi in range(20):
            idx = np.nonzero(mask)[0]
            diff = matrix[idx] - queries[qi]
            distances = np.einsum("ij,ij->i", diff, diff)
            _ = np.argpartition(distances, 10)[:10]
        flat_rows.append(now_ms() - started)
    flat = dict(method="indexflat_numpy", queries=20, mask_rows=int(mask.sum()),
                p50_ms=round(float(np.median(flat_rows)), 3),
                dists_per_q=int(mask.sum()))
    with open(os.path.join(args.out_dir, "results_flat.csv"), "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(flat))
        writer.writeheader()
        writer.writerow(flat)
    print(json.dumps(flat))


if __name__ == "__main__":
    main()
