#!/usr/bin/env python3
"""Build the hierarchical-attribute dataset on SIFT1M from the CCS hierarchy.

Layout produced under datasets/sift1m:
  base/base.bin             1M x 128 float32 (converted from raw/sift_base.fvecs)
  ccs-attr/edges.tsv        CCS DAG, lines "child parent"
  ccs-attr/nodes.tsv        attribute id, depth (longest path to a leaf), #descendant leaves
  ccs-attr/objects.txt      one leaf attribute per SIFT base object, 1M lines
  ccs-attr/queries.tsv      200 queries, line = "<|U|> <attr>..."
  ccs-attr/queries.bin      200 x 128 float32 query vectors
  ccs-attr/gt_10.txt        200 x 10 int32 exact FP32 neighbours inside the closure
  ccs-attr/meta.json        counts, seeds, distribution and per-query depth/eligibility

Objects are assigned to leaves with a Zipf(alpha) weight, so attribute sizes are
skewed as in real hierarchies. Queries are stratified by the recursion depth of
the chosen attribute, i.e. the longest path from it down to a leaf.

Usage:
  python3 build_ccs_sift_dataset.py --edges <edges.tsv> --alpha 0.8 --seed 20261109
"""
import argparse
import collections
import json
import sys
from pathlib import Path

import numpy as np

DIM = 128
ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'datasets/sift1m'


def read_fvecs(path, dim=DIM):
    raw = np.fromfile(path, dtype=np.int32)
    assert raw.size % (dim + 1) == 0, path
    mat = raw.reshape(-1, dim + 1)
    assert (mat[:, 0] == dim).all(), path
    return mat[:, 1:].astype(np.float32)


def build_tree(edges):
    children = collections.defaultdict(list)
    nodes = set()
    for child, parent in edges:
        children[parent].append(child)
        nodes.add(child)
        nodes.add(parent)
    return children, nodes


def depths(children, nodes):
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


def descendant_leaves(children, leaves):
    memo = {}
    for leaf in leaves:
        memo[leaf] = (leaf,)

    def walk(node):
        if node in memo:
            return memo[node]
        acc = set()
        for child in children.get(node, ()):
            acc.update(walk(child))
        memo[node] = tuple(sorted(acc))
        return memo[node]

    return {node: walk(node) for node in set(children) | set(leaves)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--edges', required=True, help='CCS hierarchy, lines "child parent"')
    ap.add_argument('--alpha', type=float, default=0.8, help='Zipf exponent for leaf weights (0 = uniform)')
    ap.add_argument('--seed', type=int, default=20261109)
    ap.add_argument('--queries', type=int, default=200)
    ap.add_argument('--k', type=int, default=10)
    args = ap.parse_args()

    out = DATA / 'ccs-attr'
    out.mkdir(parents=True, exist_ok=True)
    (DATA / 'base').mkdir(parents=True, exist_ok=True)

    # 1. base vectors
    base_bin = DATA / 'base/base.bin'
    if not base_bin.exists():
        base = read_fvecs(DATA / 'raw/sift_base.fvecs')
        assert base.shape == (1_000_000, DIM), base.shape
        base.astype(np.float32).tofile(base_bin)
    base = np.fromfile(base_bin, dtype=np.float32).reshape(-1, DIM)
    n = len(base)

    # 2. hierarchy
    edges = []
    with open(args.edges) as fh:
        for line in fh:
            if line.strip():
                child, parent = (int(x) for x in line.split()[:2])
                edges.append((child, parent))
    edges = sorted(set(edges))
    children, nodes = build_tree(edges)
    node_list = sorted(nodes)
    depth = depths(children, nodes)
    leaves = np.array(sorted(n for n in nodes if not children.get(n)), dtype=np.int64)
    closure = descendant_leaves(children, set(leaves.tolist()))

    # 3. assign objects to leaves with Zipf weights (deterministic)
    rng = np.random.default_rng(args.seed)
    rank = np.arange(1, len(leaves) + 1)
    weight = rank.astype(np.float64) ** (-args.alpha) if args.alpha > 0 else np.ones(len(leaves))
    weight = weight / weight.sum()
    leaf_of_object = leaves[rng.choice(len(leaves), size=n, p=weight)]
    np.savetxt(out / 'objects.txt', leaf_of_object, fmt='%d')

    leaf_members = collections.defaultdict(list)
    for obj, leaf in enumerate(leaf_of_object):
        leaf_members[int(leaf)].append(obj)

    with open(out / 'edges.tsv', 'w') as fh:
        for child, parent in edges:
            fh.write(f'{child}\t{parent}\n')
    with open(out / 'nodes.tsv', 'w') as fh:
        fh.write('attribute\tdepth\tdescendant_leaves\n')
        for node in node_list:
            fh.write(f'{node}\t{depth[node]}\t{len(closure[node])}\n')

    # 4. queries: stratified by recursion depth of the chosen attribute
    node_depth = np.array([depth[n] for n in node_list])
    levels = sorted(set(node_depth.tolist()))
    by_level = {lvl: [n for n, d in zip(node_list, node_depth) if d == lvl] for lvl in levels}
    # allocate across recursion depths: aim for an even split, but cap each level
    # at 4x the number of attributes available there so a single deep attribute is
    # not repeated dozens of times; the remainder goes back to levels with room
    avail = np.array([len(by_level[lvl]) for lvl in levels], dtype=np.int64)
    share = args.queries // len(levels)
    cap = np.minimum(4 * avail, args.queries)
    quota = np.minimum(cap, share)
    i = 0
    while int(quota.sum()) < args.queries and i < 100 * args.queries:
        lvl = i % len(levels)
        if quota[lvl] < cap[lvl]:
            quota[lvl] += 1
        i += 1

    test = read_fvecs(DATA / 'raw/sift_query.fvecs')
    picks = rng.choice(len(test), size=args.queries, replace=False)

    rows, qvecs, gts, info = [], [], [], []
    qpos = 0
    for lvl, count in zip(levels, quota):
        pool = sorted(by_level[lvl])
        chosen = rng.choice(len(pool), size=int(count), replace=int(count) > len(pool))
        for idx in chosen:
            attr = int(pool[idx])
            cand_leaves = closure[attr]
            ids = np.unique(np.concatenate([np.asarray(leaf_members[l], dtype=np.int64)
                                             for l in cand_leaves])) \
                if cand_leaves else np.empty(0, dtype=np.int64)
            vec = test[picks[qpos]]
            d = ((base[ids] - vec) ** 2).sum(axis=1)
            order = np.argsort(d, kind='stable')[:args.k]
            top = ids[order]
            rows.append('1 ' + str(attr))
            qvecs.append(vec)
            gt = np.full(args.k, -1, dtype=np.int32)
            gt[:len(top)] = top
            gts.append(gt)
            info.append(dict(qi=qpos, attribute=attr, depth=int(lvl), eligible=int(len(ids)),
                             source_query=int(picks[qpos])))
            qpos += 1

    (out / 'queries.tsv').write_text('\n'.join(rows) + '\n')
    np.stack(qvecs).astype(np.float32).tofile(out / 'queries.bin')
    np.stack(gts).astype(np.int32).tofile(out / 'gt_10.txt')

    base_sizes = np.array([len(v) for v in leaf_members.values()])
    elig = np.array([r['eligible'] for r in info])
    meta = dict(kind='CCS hierarchy applied to SIFT1M',
                objects=int(n), attributes=len(nodes), edges=len(edges), leaves=int(len(leaves)),
                max_depth=int(max(levels)), alpha=args.alpha, seed=args.seed,
                assignment='objects sampled uniformly by object id onto leaves with Zipf(alpha) weights',
                base_bin='../base/base.bin', queries=int(args.queries), k=args.k,
                query_source='raw/sift_query.fvecs, sampled without replacement',
                leaf_size=dict(min=int(base_sizes.min()), median=int(np.median(base_sizes)),
                              mean=float(base_sizes.mean()), max=int(base_sizes.max())),
                eligible=dict(min=int(elig.min()), median=int(np.median(elig)),
                              mean=float(elig.mean()), max=int(elig.max())),
                depth_hist={str(l): int(c) for l, c in zip(levels, quota)},
                per_query=info)
    (out / 'meta.json').write_text(json.dumps(meta, ensure_ascii=False, indent=1) + '\n')
    print(json.dumps({k: meta[k] for k in ['objects', 'attributes', 'edges', 'leaves', 'max_depth',
                                           'leaf_size', 'eligible', 'depth_hist']}, ensure_ascii=False))


if __name__ == '__main__':
    main()