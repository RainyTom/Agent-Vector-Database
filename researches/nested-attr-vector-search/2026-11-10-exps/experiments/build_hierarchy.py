#!/usr/bin/env python3
"""Synthetic hierarchies over SIFT1M for the directory-scope maintenance (DSM) study.

  uniform : branching 10, depth 4          -> 10,000 leaves
  skewed  : branching 6..40 random, depth 5, leaf weights Zipf(alpha=1.2)

The skewed tree keeps its branching inside the requested 6..40 band but is budget
aware, so depth 5 does not explode into millions of leaves. 1,000,000 base vectors
are distributed over the leaves with seed 42 and a per-leaf cap of 50,000 rows.

Outputs <out>/<tree>.npz and <out>/trees.json.
"""
import argparse
import json
import os

import numpy as np


def build_uniform_fast(branching, depth):
    """BFS numbering; every node keeps exactly `branching` children below `depth`."""
    parent = [-1]
    frontier = [0]
    for level in range(depth):
        next_frontier = []
        for node in frontier:
            for _ in range(branching):
                parent.append(node)
                next_frontier.append(len(parent) - 1)
        frontier = next_frontier
    return np.asarray(parent, dtype=np.int64), np.asarray(frontier, dtype=np.int64)


def build_skewed(rng, depth, min_branch, max_branch, target_leaves):
    """Random branching in [min_branch, max_branch], budgeted to ~target_leaves."""
    parent = [-1]
    frontier = [0]
    for level in range(depth):
        levels_left = depth - level
        produced = len(parent) - 1          # leaves so far at this frontier
        budget = max(1.0, (target_leaves / max(1, produced)) ** (1.0 / levels_left))
        next_frontier = []
        for node in frontier:
            branching = int(round(budget * rng.uniform(0.6, 1.4)))
            branching = int(np.clip(branching, min_branch, max_branch))
            for _ in range(branching):
                parent.append(node)
                next_frontier.append(len(parent) - 1)
        frontier = next_frontier
    return np.asarray(parent, dtype=np.int64), np.asarray(frontier, dtype=np.int64)


def distribute(rows, leaf_count, weights, rng, cap):
    """Row -> leaf composition, vectorised, with a hard per-leaf cap."""
    draw = rng.choice(leaf_count, size=rows, p=weights)
    sizes = np.bincount(draw, minlength=leaf_count)
    kept = np.minimum(sizes, cap)
    excess = rows - int(kept.sum())
    if excess > 0:
        capacity = cap - kept
        pool = capacity.astype(np.float64)
        if pool.sum() <= 0:
            raise SystemExit("cap too small for this row count")
        extra = rng.multinomial(excess, pool / pool.sum())
        kept = kept + extra
    perm = rng.permutation(rows)
    leaf_of_row = np.empty(rows, dtype=np.int64)
    leaf_of_row[perm] = np.repeat(np.arange(leaf_count, dtype=np.int64), kept)
    return leaf_of_row, kept


def depth_of_all(parent):
    depth = np.zeros(parent.size, dtype=np.int64)
    for node in range(1, parent.size):
        depth[node] = depth[parent[node]] + 1
    return depth


def ancestor_matrix(parent, depth):
    """ancestors[node, k] = k-th ancestor (node itself at k=0), -1 when exhausted."""
    width = int(depth.max()) + 1
    chain = np.full((parent.size, width), -1, dtype=np.int64)
    order = np.argsort(depth, kind="stable")
    for node in order:
        if parent[node] < 0:
            chain[node, 0] = node
            continue
        chain[node, 0] = node
        chain[node, 1:] = chain[parent[node], :-1]
    return chain


def summarise(name, parent, leaves, leaf_of_row, rows):
    depth = depth_of_all(parent)
    chain = ancestor_matrix(parent, depth)
    chain_lengths = (chain >= 0).sum(1)
    sizes = np.bincount(leaf_of_row, minlength=parent.size)[leaves]
    nonempty = sizes[sizes > 0]
    return dict(
        tree=name,
        nodes=int(parent.size),
        leaves=int(leaves.size),
        leaves_nonempty=int(nonempty.size),
        max_depth=int(depth.max()),
        rows=int(rows),
        leaf_size_p50=float(np.percentile(nonempty, 50)),
        leaf_size_p99=float(np.percentile(nonempty, 99)),
        leaf_size_max=int(sizes.max()),
        leaf_size_min=int(nonempty.min()),
        leaf_std_over_mean=float(nonempty.std() / max(1e-9, nonempty.mean())),
        ancestor_chain_p50=float(np.percentile(chain_lengths, 50)),
        ancestor_chain_p99=float(np.percentile(chain_lengths, 99)),
        ancestor_chain_max=int(chain_lengths.max()),
    ), chain


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--rows", type=int, default=1000000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--cap", type=int, default=50000)
    parser.add_argument("--skewed-leaves", type=int, default=100000)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    summary = {}
    rng = np.random.default_rng(args.seed)

    parent, leaves = build_uniform_fast(branching=10, depth=4)
    weights = np.full(leaves.size, 1.0 / leaves.size)
    leaf_of_row, _ = distribute(args.rows, leaves.size, weights, rng, args.cap)
    leaf_of_row = leaves[leaf_of_row]
    entry, chain = summarise("uniform", parent, leaves, leaf_of_row, args.rows)
    summary["uniform"] = entry
    np.savez(os.path.join(args.out, "uniform.npz"), parent=parent, leaves=leaves,
             leaf_of_row=leaf_of_row, ancestors=chain,
             depth_of=depth_of_all(parent))

    parent, leaves = build_skewed(rng, depth=5, min_branch=6, max_branch=40,
                                  target_leaves=args.skewed_leaves)
    weights = 1.0 / np.power(np.arange(1, leaves.size + 1), 1.2)
    weights /= weights.sum()
    leaf_of_row, _ = distribute(args.rows, leaves.size, weights, rng, args.cap)
    leaf_of_row = leaves[leaf_of_row]
    entry, chain = summarise("skewed", parent, leaves, leaf_of_row, args.rows)
    summary["skewed"] = entry
    np.savez(os.path.join(args.out, "skewed.npz"), parent=parent, leaves=leaves,
             leaf_of_row=leaf_of_row, ancestors=chain,
             depth_of=depth_of_all(parent))

    with open(os.path.join(args.out, "trees.json"), "w") as handle:
        json.dump(summary, handle, indent=2)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
