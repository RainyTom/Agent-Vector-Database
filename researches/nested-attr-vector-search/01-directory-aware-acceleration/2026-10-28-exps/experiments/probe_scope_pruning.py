#!/usr/bin/env python3
"""Measure how many vectors a summary layer can skip inside a directory scope.

Offline analysis only: reads the existing corpus, tree/bindings and the frozen
ground truth. Builds no index, changes no harness, and reports no benchmark.

For each requested anchor it computes the exact top-k threshold tau inside the
scope, then asks: given a partition of the scope into blocks with a bounding
ball per block, what fraction of BLOCKS and of VECTORS can be skipped when a
block is dropped iff  ||q-c|| - r > tau * (1 + slack)?

Two partitions are compared:
  * contiguous: 256 adjacent positions in DFS order (what the current layout gives)
  * geometric:  256 k-means cells over a sample, assigned to all scope members

Usage:
  probe_scope_pruning.py --data DIR --gt reference.json --queries 397,3,91 \
      --blocks 256 --clusters 256 --sample 50000 --out out.json
"""
import argparse
import json
import sys

import numpy as np


def load_tree(path):
    with open(path) as f:
        m = int(f.readline().split()[0])
        parent = np.full(m, -1, dtype=np.int64)
        for line in f:
            parts = line.split()
            if len(parts) < 2:
                continue
            parent[int(parts[0])] = int(parts[1])
    return parent


def load_bindings(path):
    with open(path) as f:
        return np.array([int(x) for x in f.read().split()], dtype=np.int64)


def layout(parent, node_of_entry):
    m = parent.shape[0]
    ch = [[] for _ in range(m)]
    for v in range(1, m):
        if parent[v] >= 0:
            ch[parent[v]].append(v)
    nloc = np.bincount(node_of_entry, minlength=m).astype(np.int64)
    lo = np.zeros(m, dtype=np.int64)
    hi = np.zeros(m, dtype=np.int64)
    cursor = 0
    lo[0] = 0
    cursor += nloc[0]
    stack = [(0, 0)]
    while stack:
        u, ci = stack[-1]
        if ci < len(ch[u]):
            c = ch[u][ci]
            stack[-1] = (u, ci + 1)
            lo[c] = cursor
            cursor += nloc[c]
            stack.append((c, 0))
        else:
            hi[u] = cursor
            stack.pop()
    return lo, hi, nloc


def kmeans(x, k, iters, rng):
    cent = x[rng.choice(x.shape[0], size=k, replace=False)].copy()
    xx = (x * x).sum(1)[:, None]
    for _ in range(iters):
        cc = (cent * cent).sum(1)[None, :]
        assign = (xx - 2.0 * x @ cent.T + cc).argmin(1)
        for j in range(k):
            sel = assign == j
            if sel.any():
                cent[j] = x[sel].mean(0)
    return cent


def ball_stats(x, labels, n_blocks, dim):
    """Centroid, radius and size per block over the given rows."""
    cent = np.zeros((n_blocks, dim), dtype=np.float64)
    cnt = np.bincount(labels, minlength=n_blocks).astype(np.int64)
    np.add.at(cent, labels, x)
    nz = cnt > 0
    cent[nz] /= cnt[nz][:, None]
    r2 = np.zeros(n_blocks, dtype=np.float64)
    step = 200_000
    for s in range(0, x.shape[0], step):
        e = min(s + step, x.shape[0])
        d = x[s:e].astype(np.float64) - cent[labels[s:e]]
        np.maximum.at(r2, labels[s:e], (d * d).sum(1))
    return cent, np.sqrt(r2), cnt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--gt", required=True)
    ap.add_argument("--ns", default="wiki")
    ap.add_argument("--dim", type=int, default=1024)
    ap.add_argument("--queries", required=True,
                    help="comma-separated query_index values from the frozen GT")
    ap.add_argument("--k", type=int, default=10)
    ap.add_argument("--blocks", type=int, default=256)
    ap.add_argument("--clusters", type=int, default=256)
    ap.add_argument("--sample", type=int, default=50000)
    ap.add_argument("--iters", type=int, default=6)
    ap.add_argument("--seed", type=int, default=20261028)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    dim = args.dim
    parent = load_tree(f"{args.data}/{args.ns}_tree.txt")
    node_of_entry = load_bindings(f"{args.data}/{args.ns}_bindings.txt")
    n_entries = node_of_entry.shape[0]
    lo, hi, nloc = layout(parent, node_of_entry)

    orig = np.full(n_entries, -1, dtype=np.int64)
    slot = lo.copy()
    for i in range(n_entries):
        orig[slot[node_of_entry[i]]] = i
        slot[node_of_entry[i]] += 1

    base = np.memmap(f"{args.data}/base.f32", dtype=np.float32, mode="r").reshape(n_entries, dim)
    ref = {q["query_index"]: q for q in json.load(open(args.gt))["queries"]}
    slacks = [0.0, 0.01, 0.02, 0.05, 0.10]
    results = []

    for qi in [int(a) for a in args.queries.split(",")]:
        entry = ref[qi]
        anchor = int(entry["anchor"])
        rows = orig[lo[anchor]:hi[anchor]]
        n = rows.shape[0]
        qidx = entry.get("query_id", entry["query_index"])
        q = np.fromfile(f"{args.data}/test.f32", dtype=np.float32,
                        count=dim, offset=dim * 4 * qidx)
        x = np.ascontiguousarray(base[rows])
        d2 = ((x - q) ** 2).sum(1)
        kk = min(args.k, n)
        tau = float(np.partition(d2, kk - 1)[kk - 1])
        gt = set(entry["gt_ids"])
        gt_pos = np.isin(rows, list(gt))

        rec = {"anchor": anchor, "query_index": entry["query_index"],
               "scope_card": int(n), "tau": tau, "k": kk,
               "partitions": {}}
        topk_rows = set(rows[np.argpartition(d2, kk - 1)[:kk]].tolist())
        rec["gt_selfcheck"] = {"topk_matches_gt": topk_rows == gt,
                               "overlap": len(topk_rows & gt), "k": kk}

        # (a) contiguous blocks in DFS order
        lab_c = (np.arange(n) // args.blocks).astype(np.int64)
        cent, rad, cnt = ball_stats(x, lab_c, lab_c.max() + 1, dim)
        rec["partitions"]["contiguous"] = _sweep(q, cent, rad, cnt, tau, slacks, lab_c, gt_pos)

        # (b) geometric k-means cells
        if args.clusters > 0 and n > args.clusters:
            s = min(args.sample, n)
            samp = x[rng.choice(n, size=s, replace=False)]
            cent = kmeans(samp, args.clusters, args.iters, rng)
            cc = (cent * cent).sum(1)[None, :]
            lab_g = np.empty(n, dtype=np.int64)
            for a in range(0, n, 100_000):
                b = min(a + 100_000, n)
                lab_g[a:b] = ((x[a:b] * x[a:b]).sum(1)[:, None]
                              - 2.0 * x[a:b] @ cent.T + cc).argmin(1)
            cent_g, rad_g, cnt_g = ball_stats(x, lab_g, args.clusters, dim)
            rec["partitions"]["geometric"] = _sweep(q, cent_g, rad_g, cnt_g, tau, slacks, lab_g, gt_pos)

        results.append(rec)
        print(json.dumps(rec, indent=2), flush=True)

    with open(args.out, "w") as f:
        json.dump({"kind": "offline_scope_prune_probe_not_benchmark",
                   "inputs": {"blocks": args.blocks, "clusters": args.clusters,
                              "sample": args.sample, "iters": args.iters,
                              "seed": args.seed},
                   "anchors": results}, f, indent=2)
        f.write("\n")


def _sweep(q, cent, rad, cnt, tau, slacks, labels, gt_pos):
    dc = np.sqrt(((cent - q) ** 2).sum(1)) if cent.shape[0] else np.zeros(0)
    lb = np.maximum(0.0, dc - rad)
    out = []
    for slack in slacks:
        pruned = lb > tau * (1.0 + slack)
        vec = int(cnt[pruned].sum())
        gt_lost = int(gt_pos[pruned[labels]].sum())
        out.append({"slack": slack,
                    "pruned_blocks_frac": float(pruned.mean()),
                    "pruned_vectors_frac": vec / max(1, int(cnt.sum())),
                    "gt_among_pruned": gt_lost})
    return {"blocks": int(cent.shape[0]), "sweep": out}


if __name__ == "__main__":
    main()
