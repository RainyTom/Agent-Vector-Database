#!/usr/bin/env python3
"""Generate a synthetic CCS-shaped hierarchical workload on supplied vectors.

Calibration sources (measured from the PathSeer dataset, see
researches/nested-attr-vector-search/08-baseline-adaptation.md):
  * category popularity is highly skewed (Paper metadata: 741 categories,
    Zipf slope ~ -2.4); this generator uses Zipf exponent 1.1 for leaves.
  * real filter datasets in that corpus are flat and mostly single-label
    (SIFT1M variants: 10/12/100 categories, 1 label per vector).
CCS structure is not available locally, so the tree shape and the fraction
of objects directly annotated at mid-level nodes are explicit assumptions:
  * depth 5, branching 4 (1365 nodes, 1024 leaves)
  * 25% of objects get a mid-level primary label instead of a leaf
  * 1-5 direct labels, mean ~2.4
These assumptions are parameters and must be validated on real CCS data.

Outputs (same formats as the main fixture, plus an Exact workload):
  base.f32, test.f32, tree.txt, labels.bin, labels_meta.txt, meta.txt
  workload_sub.txt, gt_sub.bin, workload_exact.txt, gt_exact.bin
  stats_ccs.json
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

N = 100_000
DIM = 128
DEPTH = 5
BRANCH = 4
NQ = 1000
K = 10
SEED = 20261021
P_MID = 0.25          # probability that the primary label is an ancestor
N_CENTROIDS = 512
MAX_LABELS = 5


def build_tree():
    parent = []
    level_start = []
    level_size = []
    off = 0
    sz = 1
    for d in range(DEPTH + 1):
        level_start.append(off)
        level_size.append(sz)
        for i in range(sz):
            parent.append(-1 if d == 0 else level_start[d - 1] + i // BRANCH)
        off += sz
        sz *= BRANCH
    return np.asarray(parent, dtype=np.int64), level_start, level_size


def descendants(parent):
    nodes = len(parent)
    children = [[] for _ in range(nodes)]
    for v in range(1, nodes):
        children[parent[v]].append(v)
    order = []
    stack = [0]
    while stack:
        v = stack.pop()
        order.append(v)
        stack.extend(children[v])
    return children, order


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True, help="dir with base.f32 and test.f32")
    ap.add_argument("--out", required=True)
    ap.add_argument("--n", type=int, default=N)
    ap.add_argument("--nq", type=int, default=NQ)
    ap.add_argument("--dim", type=int, default=DIM)
    args = ap.parse_args()
    if args.dim <= 0:
        ap.error("--dim must be positive")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    fix = Path(args.fixtures)

    rng = np.random.default_rng(SEED)
    base = np.fromfile(fix / "base.f32", dtype=np.float32).reshape(-1, args.dim)[: args.n]
    n = base.shape[0]
    test = np.fromfile(fix / "test.f32", dtype=np.float32).reshape(-1, args.dim)
    queries = test[rng.choice(len(test), args.nq, replace=False)]

    parent, level_start, level_size = build_tree()
    nodes = len(parent)
    children, order = descendants(parent)
    depth = np.zeros(nodes, dtype=np.int64)
    for v in order:
        if parent[v] >= 0:
            depth[v] = depth[parent[v]] + 1
    leaf_start = level_start[DEPTH]
    n_leaves = level_size[DEPTH]

    # ---- labels -----------------------------------------------------------
    centroids = base[rng.choice(n, N_CENTROIDS, replace=False)]
    centroids = centroids / (np.linalg.norm(centroids, axis=1, keepdims=True) + 1e-9)
    xb = base / (np.linalg.norm(base, axis=1, keepdims=True) + 1e-9)
    # nearest centroid per object, in blocks to bound memory
    nearest = np.empty(n, dtype=np.int32)
    block = 10_000
    for s in range(0, n, block):
        sims = xb[s:s + block] @ centroids.T
        nearest[s:s + block] = np.argmax(sims, axis=1)

    # leaf popularity: Zipf over leaves (permuted so popularity is not tied
    # to the semantic centroid assignment); also keep centroid -> leaves.
    leaf_for_centroid = {c: [] for c in range(N_CENTROIDS)}
    for l in range(n_leaves):
        leaf_for_centroid[l % N_CENTROIDS].append(leaf_start + l)
    leaf_perm = rng.permutation(n_leaves)
    zipf_leaf = 1.0 / np.arange(1, n_leaves + 1) ** 1.1
    zipf_leaf /= zipf_leaf.sum()
    leaf_probs = np.empty(n_leaves)
    leaf_probs[leaf_perm] = zipf_leaf

    def semantic_leaf(c):
        return int(rng.choice(leaf_for_centroid[c]))

    def popular_leaf():
        return int(leaf_start + rng.choice(n_leaves, p=leaf_probs))

    labels = np.full((n, MAX_LABELS), -1, dtype=np.int32)
    def ancestor_at(v, target_depth):
        while depth[v] > target_depth:
            v = int(parent[v])
        return v

    for i in range(n):
        c = int(nearest[i])
        leaf = popular_leaf() if rng.random() < 0.35 else semantic_leaf(c)
        if rng.random() < P_MID:
            d = int(rng.integers(1, DEPTH + 1))
            labels[i, 0] = ancestor_at(leaf, d)
        else:
            labels[i, 0] = leaf
        # cardinality: 1:30%, 2:40%, 3:22%, 4:6%, 5:2%
        u = rng.random()
        nlab = 1 if u < 0.30 else 2 if u < 0.70 else 3 if u < 0.92 else 4 if u < 0.98 else 5
        for j in range(1, nlab):
            r = rng.random()
            if r < 0.5:
                labels[i, j] = popular_leaf()
            elif r < 0.9:
                c2 = int(rng.choice(N_CENTROIDS))
                labels[i, j] = semantic_leaf(c2)
            else:
                # a relative of the primary label: ancestor or sibling leaf
                v = int(labels[i, 0])
                if v >= leaf_start:
                    labels[i, j] = int(rng.integers(leaf_start, leaf_start + n_leaves))
                else:
                    labels[i, j] = int(rng.choice(children[v])) if children[v] else v

    # ---- scope counts -----------------------------------------------------
    direct_count = np.zeros(nodes, dtype=np.int64)
    for j in range(MAX_LABELS):
        l = labels[:, j]
        ok = l >= 0
        direct_count += np.bincount(l[ok], minlength=nodes)
    subtree_count = direct_count.copy()
    for v in reversed(order):
        if parent[v] >= 0:
            subtree_count[parent[v]] += subtree_count[v]

    # ---- workload ---------------------------------------------------------
    # Subtree: 70% single node, 30% antichain of 2-3 nodes.
    level_choices = [(1, 0.18), (2, 0.30), (3, 0.28), (4, 0.20), (5, 0.04)]
    def sample_node(level=None):
        if level is None:
            r = rng.random()
            acc = 0.0
            for d, p in level_choices:
                acc += p
                if r <= acc:
                    level = d
                    break
        off = level_start[level]
        return int(off + rng.integers(0, level_size[level]))

    def mark_desc(us):
        mark = np.zeros(nodes, dtype=bool)
        stack = list(us)
        while stack:
            v = stack.pop()
            mark[v] = True
            stack.extend(children[v])
        return mark

    def feasible_mask(us):
        mark = mark_desc(us)
        idx = np.where(labels >= 0, labels, nodes)
        mark_ext = np.append(mark, False)
        return mark_ext[idx].any(axis=1)

    def exact_mask(us):
        m = np.zeros(n, dtype=bool)
        for u in us:
            for j in range(MAX_LABELS):
                m |= labels[:, j] == u
        return m

    sub_nodes, sub_sizes = [], []
    for _ in range(args.nq):
        for _attempt in range(100):
            if rng.random() < 0.7:
                us = [sample_node()]
            else:
                w = 2 if rng.random() < 0.7 else 3
                us = [sample_node() for _ in range(w)]
                # keep the set an antichain: drop ancestors of others
                drop = set()
                for a in us:
                    for b in us:
                        if a != b and b in ancestors_of(a, parent):
                            drop.add(a)
                us = [u for u in us if u not in drop]
            if not us:
                continue
            m = feasible_mask(us)
            if m.sum() >= 64:
                sub_nodes.append(us); sub_sizes.append(int(m.sum())); break

    exact_candidates = [u for u in range(nodes) if direct_count[u] >= 64]
    exact_nodes, exact_sizes = [], []
    for _ in range(args.nq):
        for _attempt in range(100):
            u = int(rng.choice(exact_candidates))
            if rng.random() < 0.15:
                u2 = int(rng.choice(exact_candidates))
                if u2 == u:
                    continue
                us = [u, u2]
            else:
                us = [u]
            m = exact_mask(us)
            if m.sum() >= 64:
                exact_nodes.append(us); exact_sizes.append(int(m.sum())); break

    def ground_truth(nodes_list, mask_fn):
        gt = np.full((len(nodes_list), K), -1, dtype=np.int32)
        for qi, us in enumerate(nodes_list):
            m = mask_fn(us)
            ids = np.nonzero(m)[0]
            d = ((base[ids] - queries[qi]) ** 2).sum(axis=1)
            order2 = np.lexsort((ids, d))
            top = ids[order2[:K]]
            gt[qi, :len(top)] = top
        return gt

    gt_sub = ground_truth(sub_nodes, feasible_mask)
    gt_exact = ground_truth(exact_nodes, exact_mask)

    # ---- write ------------------------------------------------------------
    (out / "base.f32").write_bytes(base.astype(np.float32).tobytes())
    (out / "test.f32").write_bytes(queries.astype(np.float32).tobytes())
    (out / "labels.bin").write_bytes(labels.astype(np.int32).tobytes())
    (out / "labels_meta.txt").write_text(f"{n} {MAX_LABELS}\n")
    (out / "gt_sub.bin").write_bytes(gt_sub.astype(np.int32).tobytes())
    (out / "gt_exact.bin").write_bytes(gt_exact.astype(np.int32).tobytes())
    with open(out / "tree.txt", "w") as f:
        f.write(f"{nodes}\n")
        for v in range(nodes):
            f.write(f"{v} {parent[v]}\n")
    def write_wl(path, nodes_list):
        with open(path, "w") as f:
            f.write(f"{len(nodes_list)} {K}\n")
            for us in nodes_list:
                f.write(str(len(us)) + " " + " ".join(map(str, us)) + "\n")
    write_wl(out / "workload_sub.txt", sub_nodes)
    write_wl(out / "workload_exact.txt", exact_nodes)
    words = (n + 63) // 64
    (out / "meta.txt").write_text(f"{n} {args.dim} {args.nq} {K} {nodes} {words}\n")

    # stats
    dep = np.zeros(DEPTH + 1, dtype=np.int64)
    for d in range(DEPTH + 1):
        seg = direct_count[level_start[d]:level_start[d] + level_size[d]]
        dep[d] = int(seg.sum())
    lab_hist = np.bincount((labels >= 0).sum(axis=1), minlength=MAX_LABELS + 1)
    empty_exact = int((direct_count == 0).sum())
    stats = {
        "tree_nodes": nodes, "leaves": n_leaves,
        "labels_per_object_hist": lab_hist.tolist(),
        "direct_members_by_depth": dep.tolist(),
        "empty_exact_scopes": empty_exact,
        "subtree_scope_size_p50_p95_max": [int(np.percentile(sub_sizes, 50)),
                                           int(np.percentile(sub_sizes, 95)),
                                           int(np.max(sub_sizes))],
        "exact_scope_size_p50_p95_max": [int(np.percentile(exact_sizes, 50)),
                                         int(np.percentile(exact_sizes, 95)),
                                         int(np.max(exact_sizes))],
        "sub_queries": len(sub_nodes), "exact_queries": len(exact_nodes),
        "p_mid": P_MID, "zipf_exp": 1.1,
        "calibration": "PathSeer HF meta: Paper 741 cats Zipf slope -2.4; "
                       "SIFT1M flat labels 10/12/100 cats, 1/vector",
    }
    (out / "stats_ccs.json").write_text(json.dumps(stats, indent=2, ensure_ascii=False))
    print(json.dumps(stats, indent=2, ensure_ascii=False))


def ancestors_of(v, parent):
    while v >= 0:
        yield v
        v = parent[v]


if __name__ == "__main__":
    main()
