#!/usr/bin/env python3
"""C3 sizing (revised): measurable quantities only.

The real WIKI fixture stores node ids without names, so "same relative name"
conflict detection is not measurable --- conflict *frequency* must be treated as
a workload parameter. What is measurable and decides the cost model:

  MOVE(u): number of nodes/items in subtree(u)  -> renumbering scope;
           local ANN re-parents as a whole (cost 0), Euler/interval codes do not.
  Conflict(a, b): |L(a)| + |L(b)|  -> rebuild-merged vs lazy dual index cost;
           sampled by the same (u weighted by subtree items, w uniform) rule as
           the DSM stream.

All values come from tree + bindings; no index build.
"""
import argparse
import json
import statistics as st
from pathlib import Path

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--ops", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=20261026)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    toks = Path(a.data, "tree.txt").read_text().split()
    m = int(toks[0])
    parent = np.full(m, -1, dtype=np.int64)
    vals = np.asarray(toks[1:], dtype=np.int64).reshape(m, 2)
    parent[vals[:, 0]] = vals[:, 1]
    children = [[] for _ in range(m)]
    for v in range(1, m):
        children[int(parent[v])].append(v)
    bindings = np.loadtxt(Path(a.data, "bindings.txt"), dtype=np.int64)
    counts = np.bincount(bindings, minlength=m).astype(np.int64)

    # subtree item counts (bottom-up over reverse DFS of preorder)
    order, stack = [], [0]
    while stack:
        v = stack.pop()
        order.append(v)
        stack.extend(children[v])
    sub_items = counts.copy()
    sub_nodes = np.ones(m, dtype=np.int64)
    for v in reversed(order):
        for c in children[v]:
            sub_items[v] += sub_items[c]
            sub_nodes[v] += sub_nodes[c]

    rng = np.random.default_rng(a.seed)
    wgt = sub_items.astype(np.float64).copy()
    wgt[0] = 0
    wgt /= wgt.sum()

    def is_ancestor(x, y):
        while y >= 0:
            if y == x:
                return True
            y = parent[y]
        return False

    moves, conflicts = [], []
    while len(moves) < a.ops or len(conflicts) < a.ops:
        u = int(rng.choice(m, p=wgt))
        w = int(rng.integers(1, m))
        if u == w or u == 0 or is_ancestor(u, w):
            continue
        if len(moves) < a.ops:
            moves.append({"nodes": int(sub_nodes[u]), "items": int(sub_items[u]),
                          "local_items": int(counts[u])})
        if len(conflicts) < a.ops:
            # conflict at node level: |L(a)| + |L(b)|
            conflicts.append({"a_local": int(counts[u]), "b_local": int(counts[w]),
                              "merged_items": int(counts[u] + counts[w])})

    def summ(rows, keys):
        out = {"n": len(rows)}
        for k in keys:
            v = np.array([r[k] for r in rows], dtype=np.float64)
            out[k] = {"median": float(np.median(v)), "mean": float(v.mean()),
                      "p90": float(np.percentile(v, 90)), "max": float(v.max()),
                      "sum": float(v.sum())}
        return out

    res = {"tree_nodes": m, "nonempty_nodes": int((counts > 0).sum()),
           "items_total": int(bindings.size), "seed": a.seed,
           "measurement_limits": [
               "real WIKI fixture stores node ids without names: MERGE "
               "'same relative name' conflicts cannot be detected; conflict "
               "frequency is a workload parameter, conflict *size* is measured here",
               "local ANN re-parent cost for MOVE is 0 by construction (index "
               "object moves with the node); renumbering of Euler/interval "
               "codes is O(subtree nodes)"],
           "move": summ(moves, ["nodes", "items", "local_items"]),
           "conflict_size": summ(conflicts, ["a_local", "b_local", "merged_items"])}
    Path(a.out).write_text(json.dumps(res, indent=2) + "\n")
    print(json.dumps(res, indent=2))


if __name__ == "__main__":
    main()
