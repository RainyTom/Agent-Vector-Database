#!/usr/bin/env python3
"""DSM main table: three scope representations x four primitives x subtree sizes.

Representations are constructed once per (tree, representation); every cell then
runs the primitive `repeats` times and reports the distribution. Counters are
deltas measured around the primitive, so they are unaffected by the repeated runs.

  rowpath    every row stores its scope path (fixed-width bytes) + level ids
  nodebitmap every internal node stores a bit-packed row bitmap of length N
  trie       name trie + per-leaf row id lists (a move is a pointer swap)
"""
import argparse
import csv
import json
import os
import time

import numpy as np

PATH_DTYPE = "S160"
PATH_FIELD_BYTES = 160


def now_ms():
    return time.perf_counter() * 1000.0


class Hierarchy:
    def __init__(self, path):
        payload = np.load(path, allow_pickle=True)
        self.parent = payload["parent"]
        self.leaves = payload["leaves"]
        self.leaf_of_row = payload["leaf_of_row"]
        self.depth_of = payload["depth_of"]
        self.ancestors = payload["ancestors"]
        self.n_rows = int(self.leaf_of_row.size)
        self.n_nodes = int(self.parent.size)
        self.children = [[] for _ in range(self.n_nodes)]
        for node in range(1, self.n_nodes):
            self.children[int(self.parent[node])].append(node)
        self.subtree_rows = np.zeros(self.n_nodes, dtype=np.int64)
        direct = np.bincount(self.leaf_of_row, minlength=self.n_nodes)
        order = np.argsort(self.depth_of, kind="stable")[::-1]
        self.subtree_rows[:] = direct
        for node in order:
            if node:
                self.subtree_rows[int(self.parent[node])] += self.subtree_rows[node]
        self.leaf_rows = {int(leaf): np.nonzero(self.leaf_of_row == leaf)[0]
                          for leaf in self.leaves}
        self.path_cache = {}

    def path_of(self, node):
        node = int(node)
        cached = self.path_cache.get(node)
        if cached is not None:
            return cached
        chain = [node]
        while self.parent[chain[-1]] >= 0:
            chain.append(int(self.parent[chain[-1]]))
        value = "/".join(f"n{n}" for n in reversed(chain))
        self.path_cache[node] = value
        return value

    def descendants(self, scope):
        stack = [int(scope)]
        out = []
        while stack:
            node = stack.pop()
            out.append(node)
            stack.extend(self.children[node])
        return np.asarray(out, dtype=np.int64)

    def ancestor_chain(self, scope):
        chain = [int(scope)]
        while self.parent[chain[-1]] >= 0:
            chain.append(int(self.parent[chain[-1]]))
        return chain

    def pick_scope(self, target, rng):
        """Nearest subtree in log space; a fixed tree only offers its own sizes."""
        candidates = np.nonzero(self.subtree_rows > 0)[0]
        candidates = candidates[candidates != 0]
        if candidates.size == 0:
            return None
        sizes = self.subtree_rows[candidates].astype(np.float64)
        distance = np.abs(np.log(sizes) - np.log(float(target)))
        best = distance.min()
        pool = candidates[distance <= best * 1.02 + 1e-9]
        return int(rng.choice(pool))


class RowPathRep:
    name = "rowpath"

    def __init__(self, h, rng):
        self.h = h
        leaf_paths = {int(leaf): h.path_of(leaf).encode() for leaf in h.leaves}
        self.paths = np.empty(h.n_rows, dtype=PATH_DTYPE)
        self.paths[:] = np.asarray([leaf_paths[int(leaf)] for leaf in h.leaf_of_row],
                                   dtype=PATH_DTYPE)
        self.level_bytes = int(h.ancestors.shape[1] * 2)
        self.snapshot = self.paths.copy()   # restored before every repeat
        self._row_sets = {}

    def _rows_of(self, scope):
        rows = self._row_sets.get(int(scope))
        if rows is None:
            rows = np.nonzero(np.isin(self.h.leaf_of_row,
                                       self.h.descendants(scope)))[0]
            self._row_sets[int(scope)] = rows
        return rows

    def resolve_mask(self, scope):
        prefix = self.h.path_of(scope).encode()
        hit = np.char.startswith(self.paths, prefix)
        hit |= self.paths == prefix
        return hit.astype(np.uint8)

    def _rewrite(self, scope, new_parent):
        started = now_ms()
        rows = self._rows_of(scope)
        self.paths[:] = self.snapshot     # each repeat starts from the same rows
        old_prefix = self.h.path_of(scope).encode()
        new_prefix = (self.h.path_of(new_parent) + "/n" + str(int(scope))).encode()
        self.paths[rows] = np.char.replace(self.paths[rows], old_prefix, new_prefix)
        bytes_written = int(rows.size * (PATH_FIELD_BYTES + self.level_bytes))
        return dict(rows_updated=int(rows.size), bytes_written=bytes_written,
                    log_entries=1, latency_ms=now_ms() - started)

    def move(self, scope, new_parent):
        return self._rewrite(scope, new_parent)

    def merge(self, first, second, new_parent):
        one = self._rewrite(first, new_parent)
        two = self._rewrite(second, new_parent)
        return dict(rows_updated=one["rows_updated"] + two["rows_updated"],
                    bytes_written=one["bytes_written"] + two["bytes_written"],
                    log_entries=2,
                    latency_ms=one["latency_ms"] + two["latency_ms"])

    def split(self, grandchild, target_parent):
        return self._rewrite(grandchild, target_parent)

    def subtree_delete(self, scope):
        started = now_ms()
        rows = self._rows_of(scope)
        self.paths[:] = self.snapshot
        self.paths[rows] = b"\x00deleted"
        bytes_written = int(rows.size * (PATH_FIELD_BYTES + self.level_bytes))
        return dict(rows_updated=int(rows.size), bytes_written=bytes_written,
                    log_entries=1, latency_ms=now_ms() - started)


class NodeBitmapRep:
    name = "nodebitmap"

    def __init__(self, h, rng):
        self.h = h
        self.n_rows = h.n_rows
        self.packed = (h.n_rows + 7) // 8
        self.internal = [n for n in range(h.n_nodes) if h.children[n]]
        self.bitmap = {}
        for node in sorted(self.internal, key=lambda n: h.depth_of[n], reverse=True):
            bits = np.zeros(h.n_rows, dtype=np.uint8)
            for child in h.children[node]:
                if child in h.leaf_rows:
                    bits[h.leaf_rows[child]] = 1
                else:
                    bits |= np.unpackbits(self.bitmap[child], count=h.n_rows)
            self.bitmap[node] = np.packbits(bits)

    def resolve_mask(self, scope):
        scope = int(scope)
        if scope in self.h.leaf_rows:
            mask = np.zeros(self.n_rows, dtype=np.uint8)
            mask[self.h.leaf_rows[scope]] = 1
            return mask
        return np.unpackbits(self.bitmap[scope], count=self.n_rows)

    def _recompute(self, node):
        bits = np.zeros(self.n_rows, dtype=np.uint8)
        for child in self.h.children[node]:
            if child in self.h.leaf_rows:
                bits[self.h.leaf_rows[child]] = 1
            else:
                bits |= np.unpackbits(self.bitmap[child], count=self.n_rows)
        self.bitmap[node] = np.packbits(bits)

    def _rewrite_chain(self, scope):
        started = now_ms()
        rewrites = 0
        for node in self.h.ancestor_chain(scope):
            if node in self.bitmap:
                self._recompute(node)
                rewrites += 1
        return dict(rows_updated=0, bytes_written=int(rewrites * self.packed),
                    log_entries=1, latency_ms=now_ms() - started,
                    ancestor_rewrites=rewrites)

    def move(self, scope, new_parent):
        return self._rewrite_chain(scope)

    def merge(self, first, second, new_parent):
        one = self._rewrite_chain(first)
        two = self._rewrite_chain(second)
        return dict(rows_updated=0,
                    bytes_written=one["bytes_written"] + two["bytes_written"],
                    log_entries=2,
                    latency_ms=one["latency_ms"] + two["latency_ms"],
                    ancestor_rewrites=one["ancestor_rewrites"] + two["ancestor_rewrites"])

    def split(self, grandchild, target_parent):
        return self._rewrite_chain(grandchild)

    def subtree_delete(self, scope):
        return self._rewrite_chain(scope)


class TrieRep:
    name = "trie"

    def __init__(self, h, rng):
        self.h = h
        self.parent = {n: int(h.parent[n]) for n in range(h.n_nodes)}
        self.children = {n: list(h.children[n]) for n in range(h.n_nodes)}

    def resolve_mask(self, scope):
        mask = np.zeros(self.h.n_rows, dtype=np.uint8)
        stack = [int(scope)]
        while stack:
            node = stack.pop()
            rows = self.h.leaf_rows.get(node)
            if rows is not None:
                mask[rows] = 1
            else:
                stack.extend(self.children[node])
        return mask

    def _relink(self, node, parent):
        old = self.parent[node]
        if old >= 0 and node in self.children[old]:
            self.children[old].remove(node)
        self.parent[node] = parent
        self.children[parent].append(node)

    def move(self, scope, new_parent):
        started = now_ms()
        self._relink(int(scope), int(new_parent))
        return dict(rows_updated=0, bytes_written=16, log_entries=1,
                    latency_ms=now_ms() - started)

    def merge(self, first, second, new_parent):
        started = now_ms()
        self._relink(int(first), int(new_parent))
        self._relink(int(second), int(new_parent))
        return dict(rows_updated=0, bytes_written=32, log_entries=2,
                    latency_ms=now_ms() - started)

    def split(self, grandchild, target_parent):
        started = now_ms()
        self._relink(int(grandchild), int(target_parent))
        return dict(rows_updated=0, bytes_written=16, log_entries=1,
                    latency_ms=now_ms() - started)

    def subtree_delete(self, scope):
        started = now_ms()
        old = self.parent[int(scope)]
        if old >= 0 and int(scope) in self.children[old]:
            self.children[old].remove(int(scope))
        self.parent[int(scope)] = -1
        return dict(rows_updated=0, bytes_written=16, log_entries=1,
                    latency_ms=now_ms() - started)


REPRESENTATIONS = {"rowpath": RowPathRep, "nodebitmap": NodeBitmapRep, "trie": TrieRep}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hier", required=True)
    parser.add_argument("--tree", default="uniform")
    parser.add_argument("--reps", default="rowpath,nodebitmap,trie")
    parser.add_argument("--primitives", default="move,merge,split,delete")
    parser.add_argument("--sizes", default="1000,5000,10000,50000,100000,300000")
    parser.add_argument("--repeats", type=int, default=1, help="single shot by design")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    hierarchy = Hierarchy(os.path.join(args.hier, f"{args.tree}.npz"))
    sizes = [int(v) for v in args.sizes.split(",")]
    primitives = [v.strip() for v in args.primitives.split(",") if v.strip()]
    reps = [v.strip() for v in args.reps.split(",") if v.strip()]
    rng = np.random.default_rng(args.seed)

    fields = ["tree", "representation", "primitive", "subtree_size", "actual_subtree_size", "rows_updated",
              "bytes_written", "log_entries", "latency_p50_ms", "latency_p99_ms",
              "theoretical_rows", "theoretical_bytes", "repeats", "measured_over_theoretical"]
    writer_rows = []

    for name in reps:
        rep = REPRESENTATIONS[name](hierarchy, rng)
        for size in sizes:
            scope = hierarchy.pick_scope(size, rng)
            if scope is None:
                for primitive in primitives:
                    writer_rows.append(dict(
                        tree=args.tree, representation=name, primitive=primitive,
                        subtree_size=size, actual_subtree_size="NOT_MEASURED",
                        rows_updated="NOT_MEASURED",
                        bytes_written="NOT_MEASURED", log_entries="NOT_MEASURED",
                        latency_p50_ms="NOT_MEASURED", latency_p99_ms="NOT_MEASURED",
                        theoretical_rows=size, theoretical_bytes="NOT_MEASURED",
                        repeats=0, measured_over_theoretical="NOT_MEASURED"))
                continue
            new_parent = hierarchy.pick_scope(max(size, 400000), rng) or 0
            actual_rows = int(hierarchy.subtree_rows[scope])
            chain = hierarchy.ancestor_chain(scope)
            for primitive in primitives:
                latencies, rows, byts, logs = [], [], [], []
                for _ in range(args.repeats):
                    if primitive == "move":
                        stats = rep.move(scope, new_parent)
                    elif primitive == "merge":
                        second = hierarchy.pick_scope(size, rng)
                        stats = rep.merge(scope, second if second is not None else scope,
                                          new_parent)
                    elif primitive == "split":
                        # detach S from its parent and hang it under its grandparent,
                        # i.e. T becomes a sibling of its former parent
                        grandparent = int(hierarchy.parent[int(hierarchy.parent[scope])]) \
                            if hierarchy.parent[scope] >= 0 and hierarchy.parent[int(hierarchy.parent[scope])] >= 0 else 0
                        stats = rep.split(scope, grandparent)
                    elif primitive == "delete":
                        stats = rep.subtree_delete(scope)
                    else:
                        raise SystemExit("unknown primitive " + primitive)
                    latencies.append(stats["latency_ms"])
                    rows.append(stats["rows_updated"])
                    byts.append(stats["bytes_written"])
                    logs.append(stats["log_entries"])
                if name == "rowpath":
                    theoretical_rows = actual_rows
                    theoretical_bytes = int(actual_rows * (PATH_FIELD_BYTES + hierarchy.ancestors.shape[1] * 2))
                elif name == "nodebitmap":
                    theoretical_rows = 0
                    theoretical_bytes = int(len(chain) * ((hierarchy.n_rows + 7) // 8))
                else:
                    theoretical_rows = 0
                    theoretical_bytes = 16
                measured = int(np.median(byts))
                ratio = round(float(measured) / theoretical_bytes, 4) if theoretical_bytes else "NA"
                writer_rows.append(dict(
                    tree=args.tree, representation=name, primitive=primitive,
                    subtree_size=size, actual_subtree_size=actual_rows,
                    rows_updated=int(np.median(rows)),
                    bytes_written=measured, log_entries=int(np.median(logs)),
                    latency_p50_ms=round(float(np.median(latencies)), 3),
                    latency_p99_ms=round(float(np.max(latencies)), 3),
                    theoretical_rows=theoretical_rows,
                    theoretical_bytes=theoretical_bytes, repeats=args.repeats,
                    measured_over_theoretical=ratio))
                print(json.dumps(dict(tree=args.tree, repr=name, prim=primitive, size=size,
                                      p50=round(float(np.percentile(latencies, 50)), 3),
                                      bytes=measured)), flush=True)

    with open(args.out, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in writer_rows:
            writer.writerow(row)
    print(f"wrote {args.out} ({len(writer_rows)} rows)")


if __name__ == "__main__":
    main()
