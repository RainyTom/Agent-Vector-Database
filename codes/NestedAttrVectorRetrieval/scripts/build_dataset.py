#!/usr/bin/env python3
"""Build a hierarchical-attribute dataset in the unified flat layout.

Input: any base-vector file, a hierarchy (child parent edges) and a query-vector
file. Output: one directory that every scheme can read through
`codes/shared/dataset.h`:

    <out>/base.bin        float32, n x dim
    <out>/edges.tsv       "child parent"
    <out>/objects.txt     one owning leaf attribute per object
    <out>/queries.bin     float32, m x dim
    <out>/queries.tsv     "<attribute_count> <attribute>..."
    <out>/gt_<k>.txt      int32, m x k, -1 padding
    <out>/meta.json       parameters and statistics

Objects are assigned to leaves with a Zipf weight, queries are stratified by the
recursion depth of the chosen attribute, and ground truth is the exact FP32
top-k inside each query's descendant closure.

Example:
  python3 codes/NestedAttrVectorRetrieval/scripts/build_dataset.py \
      --base-fvecs datasets/sift1m/raw/sift_base.fvecs \
      --query-fvecs datasets/sift1m/raw/sift_query.fvecs \
      --edges datasets/sift1m/ccs-attr/edges.tsv \
      --out datasets/sift1m/ccs-attr --queries 200 --k 10
"""
import argparse
import collections
import json
import sys
from pathlib import Path

import numpy as np


def read_fvecs(path, dim):
    raw = np.fromfile(path, dtype=np.int32)
    if raw.size % (dim + 1) != 0:
        raise SystemExit(f'{path}: not an fvecs file with dim {dim}')
    matrix = raw.reshape(-1, dim + 1)
    if not (matrix[:, 0] == dim).all():
        raise SystemExit(f'{path}: inconsistent dimension field')
    # the payload is IEEE-754 float32 stored as raw bytes; reinterpret, do not convert
    return np.ascontiguousarray(matrix[:, 1:]).view(np.float32)


def load_hierarchy(path):
    children = collections.defaultdict(list)
    nodes = set()
    with open(path) as handle:
        for line in handle:
            if not line.strip():
                continue
            child, parent = (int(x) for x in line.split()[:2])
            children[parent].append(child)
            nodes.add(child)
            nodes.add(parent)
    return children, nodes


def depth_to_leaf(children, nodes):
    memo = {}
    sys.setrecursionlimit(100000)

    def walk(node):
        if node in memo:
            return memo[node]
        memo[node] = 0
        best = 0
        for child in children.get(node, ()):
            best = max(best, 1 + walk(child))
        memo[node] = best
        return best

    for node in nodes:
        walk(node)
    return memo


def descendant_attributes(children, nodes):
    """Every attribute in node's descendant closure, including internal ones."""
    memo = {}

    def walk(node):
        if node in memo:
            return memo[node]
        found = {node}
        for child in children.get(node, ()):
            found.update(walk(child))
        memo[node] = tuple(sorted(found))
        return memo[node]

    return {node: walk(node) for node in nodes}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base-fvecs', required=True, help='object vectors, fvecs')
    parser.add_argument('--query-fvecs', required=True, help='candidate query vectors, fvecs')
    parser.add_argument('--edges', required=True, help='hierarchy, lines "child parent"')
    parser.add_argument('--out', required=True, help='output dataset directory')
    parser.add_argument('--dim', type=int, default=128)
    parser.add_argument('--queries', type=int, default=200)
    parser.add_argument('--k', type=int, default=10)
    parser.add_argument('--alpha', type=float, default=0.8, help='Zipf exponent of leaf weights (0 = uniform)')
    parser.add_argument('--seed', type=int, default=20261109)
    parser.add_argument('--depth-repetition-cap', type=int, default=4,
                        help='max queries per available attribute at one recursion depth')
    parser.add_argument('--internal-share', type=float, default=0.2,
                        help='fraction of objects attached to non-leaf attributes, so that '
                             'parent attributes own objects and cross-attribute edges can exist')
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    base = read_fvecs(args.base_fvecs, args.dim)
    base.astype(np.float32).tofile(out / 'base.bin')

    children, nodes = load_hierarchy(args.edges)
    depth = depth_to_leaf(children, nodes)
    leaves = np.array(sorted(n for n in nodes if not children.get(n)), dtype=np.int64)
    closure = descendant_attributes(children, nodes)

    generator = np.random.default_rng(args.seed)
    weights = (np.arange(1, len(leaves) + 1, dtype=np.float64) ** (-args.alpha)
               if args.alpha > 0 else np.ones(len(leaves)))
    weights /= weights.sum()
    owning_attribute = leaves[generator.choice(len(leaves), size=len(base), p=weights)]

    # a share of objects sits directly on internal attributes, as in real tag data;
    # without that no parent attribute owns any object and no cross-attribute edge exists
    internal_attributes = np.array(sorted(n for n in nodes if children.get(n)), dtype=np.int64)
    if args.internal_share > 0 and len(internal_attributes) > 0:
        internal_count = int(round(args.internal_share * len(base)))
        chosen = generator.choice(len(base), size=internal_count, replace=False)
        owning_attribute[chosen] = generator.choice(internal_attributes, size=internal_count)
    np.savetxt(out / 'objects.txt', owning_attribute, fmt='%d')

    attribute_members = collections.defaultdict(list)
    for object_id, attribute in enumerate(owning_attribute.tolist()):
        attribute_members[attribute].append(object_id)

    with open(out / 'edges.tsv', 'w') as handle:
        for parent, kids in sorted(children.items()):
            for child in kids:
                handle.write(f'{child}\t{parent}\n')

    # queries stratified by the recursion depth of the chosen attribute
    attribute_depth = {}
    for node in nodes:
        attribute_depth[node] = depth[node]
    levels = sorted(set(attribute_depth.values()))
    by_level = {level: [node for node, d in attribute_depth.items() if d == level] for level in levels}
    share = args.queries // len(levels)
    cap = np.array([min(args.depth_repetition_cap * len(by_level[level]), args.queries) for level in levels])
    quota = np.minimum(cap, share)
    cursor = 0
    while int(quota.sum()) < args.queries and cursor < 100 * args.queries:
        level_index = cursor % len(levels)
        if quota[level_index] < cap[level_index]:
            quota[level_index] += 1
        cursor += 1

    query_pool = read_fvecs(args.query_fvecs, args.dim)
    chosen_vectors = generator.choice(len(query_pool), size=args.queries, replace=False)

    query_lines, query_vectors, ground_truth, per_query = [], [], [], []
    position = 0
    for level, count in zip(levels, quota):
        pool = sorted(by_level[level])
        picks = generator.choice(len(pool), size=int(count), replace=int(count) > len(pool))
        for pick in picks:
            attribute = int(pool[pick])
            member_lists = [np.asarray(attribute_members[node], dtype=np.int64)
                            for node in closure[attribute] if attribute_members[node]]
            eligible_ids = np.unique(np.concatenate(member_lists)) if member_lists else np.empty(0, dtype=np.int64)
            vector = query_pool[chosen_vectors[position]]
            distances = ((base[eligible_ids] - vector) ** 2).sum(axis=1)
            best = eligible_ids[np.argsort(distances, kind='stable')[:args.k]]
            query_lines.append(f'1 {attribute}')
            query_vectors.append(vector)
            row = np.full(args.k, -1, dtype=np.int32)
            row[:len(best)] = best
            ground_truth.append(row)
            per_query.append(dict(qi=position, attribute=attribute, depth=int(level),
                                  eligible=int(len(eligible_ids)),
                                  source_query=int(chosen_vectors[position])))
            position += 1

    (out / 'queries.tsv').write_text('\n'.join(query_lines) + '\n')
    np.stack(query_vectors).astype(np.float32).tofile(out / 'queries.bin')
    np.stack(ground_truth).astype(np.int32).tofile(out / f'gt_{args.k}.txt')

    leaf_sizes = np.array([len(attribute_members[leaf]) for leaf in leaves.tolist()])
    eligibility = np.array([entry['eligible'] for entry in per_query])
    meta = dict(
        kind='hierarchical attributes over a base vector set',
        dim=args.dim, objects=int(len(base)), attributes=len(nodes), leaves=int(len(leaves)),
        max_depth=int(max(levels)), alpha=args.alpha, seed=args.seed,
        assignment=f'objects onto leaves with Zipf(alpha) weights, plus {args.internal_share:.0%} '
                   f'uniformly onto internal attributes so parents own objects',
        internal_share=args.internal_share,
        queries=args.queries, k=args.k,
        query_source=f'{args.query_fvecs}, sampled without replacement',
        depth_hist={str(level): int(count) for level, count in zip(levels, quota)},
        leaf_size=dict(min=int(leaf_sizes.min()), median=int(np.median(leaf_sizes)),
                       mean=float(leaf_sizes.mean()), max=int(leaf_sizes.max())),
        eligible=dict(min=int(eligibility.min()), median=int(np.median(eligibility)),
                      mean=float(eligibility.mean()), max=int(eligibility.max())),
        layout='flat: base.bin, edges.tsv, objects.txt, queries.tsv, queries.bin, gt_<k>.txt',
        per_query=per_query,
    )
    (out / 'meta.json').write_text(json.dumps(meta, ensure_ascii=False, indent=1) + '\n')
    print(json.dumps({key: meta[key] for key in
                      ['objects', 'attributes', 'leaves', 'max_depth', 'leaf_size', 'eligible', 'depth_hist']},
                     ensure_ascii=False))


if __name__ == '__main__':
    main()
