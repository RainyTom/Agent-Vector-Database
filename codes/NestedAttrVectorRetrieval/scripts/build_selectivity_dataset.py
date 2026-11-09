#!/usr/bin/env python3
"""Slice an existing hierarchical-attribute dataset into fixed-selectivity tasks.

Every query gets its own attribute set U (a union of leaf attributes) whose
descendant closure covers ~sigma of the objects, so selectivity is controlled
without changing the hierarchy, the base vectors or the distance function.
Output stays in the unified flat layout that `codes/shared/dataset.h` reads:

    <out>/base.bin        symlink to the source base vectors
    <out>/edges.tsv       symlink to the source hierarchy
    <out>/objects.txt     symlink to the source owning attributes
    <out>/queries.bin     float32, m x dim, taken from the official query file
    <out>/queries.tsv     "<attribute_count> <attribute>...", all leaves
    <out>/gt_<k>.txt      int32, m x k, exact FP32 top-k inside E(U)
    <out>/meta.json       parameters, achieved selectivity, per-query eligibility

A leaf has no descendants, so E(U) is exactly the objects owned by U and the
closure step of the search adds nothing. The leaves are chosen by a seeded
greedy over a shuffled pool: take any unselected leaf that still fits in the
remaining budget, so the residual is below the smallest leaf.

Example:
  python3 codes/NestedAttrVectorRetrieval/scripts/build_selectivity_dataset.py
"""
import argparse
import collections
import json
import os
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]


def read_fvecs(path, dim):
    raw = np.fromfile(path, dtype=np.int32)
    if raw.size % (dim + 1) != 0:
        raise SystemExit(f'{path}: not an fvecs file with dim {dim}')
    matrix = raw.reshape(-1, dim + 1)
    if not (matrix[:, 0] == dim).all():
        raise SystemExit(f'{path}: inconsistent dimension field')
    # the payload is IEEE-754 float32 stored as raw bytes; reinterpret, do not convert
    return np.ascontiguousarray(matrix[:, 1:]).view(np.float32)


def load_leaves(edges_path):
    children = collections.defaultdict(list)
    nodes = set()
    with open(edges_path) as handle:
        for line in handle:
            if not line.strip():
                continue
            child, parent = (int(x) for x in line.split()[:2])
            children[parent].append(child)
            nodes.add(child)
            nodes.add(parent)
    leaves = sorted(nodes - set(children.keys()))
    return leaves, children


def group_by_attribute(owner):
    order = np.argsort(owner, kind='stable')
    sorted_owner = owner[order]
    boundaries = np.flatnonzero(np.diff(sorted_owner)) + 1
    starts = np.concatenate(([0], boundaries))
    ends = np.concatenate((boundaries, [len(sorted_owner)]))
    members = {}
    for start, end in zip(starts, ends):
        members[int(sorted_owner[start])] = order[start:end]
    return members


def pick_leaves(sizes, target, rng):
    """Greedy over a shuffled pool; residual stays below the smallest leaf.

    Returns positions into `sizes`, not attribute ids.
    """
    order = rng.permutation(sizes.size)
    candidate_sizes = sizes[order]
    taken = np.zeros(candidate_sizes.size, dtype=bool)
    total = 0
    while True:
        remaining = target - total
        fitting = np.flatnonzero(~taken & (candidate_sizes <= remaining))
        if fitting.size == 0:
            break
        index = int(fitting[0])
        taken[index] = True
        total += int(candidate_sizes[index])
    return order[taken], total


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source', default=str(ROOT / 'datasets/sift1m/ccs-attr'))
    parser.add_argument('--query-fvecs', default=str(ROOT / 'datasets/sift1m/raw/sift_query.fvecs'))
    parser.add_argument('--out-root', default=str(ROOT / 'datasets/sift1m'))
    parser.add_argument('--selectivities', default='0.01,0.10')
    parser.add_argument('--queries', type=int, default=1000)
    parser.add_argument('--dim', type=int, default=128)
    parser.add_argument('--k', type=int, default=10)
    parser.add_argument('--seed', type=int, default=20261110)
    args = parser.parse_args()

    source = Path(args.source).resolve()
    out_root = Path(args.out_root).resolve()
    base = np.fromfile(source / 'base.bin', dtype=np.float32).reshape(-1, args.dim)
    owner = np.loadtxt(source / 'objects.txt', dtype=np.int64)
    if owner.size != base.shape[0]:
        raise SystemExit(f'objects.txt has {owner.size} lines but base.bin holds {base.shape[0]} vectors')
    leaves, children = load_leaves(source / 'edges.tsv')
    if any(children.get(leaf) for leaf in leaves):
        raise SystemExit('leaf detection is inconsistent with edges.tsv')
    members = group_by_attribute(owner)
    missing = [leaf for leaf in leaves if leaf not in members]
    if missing:
        raise SystemExit(f'{len(missing)} leaves own no object')
    leaf_ids = np.asarray(leaves, dtype=np.int64)
    leaf_sizes = np.asarray([members[leaf].size for leaf in leaves], dtype=np.int64)

    query_pool = read_fvecs(args.query_fvecs, args.dim)
    if args.queries > query_pool.shape[0]:
        raise SystemExit(f'asked {args.queries} queries but the file holds {query_pool.shape[0]}')
    queries = query_pool[:args.queries]

    targets = [float(x) for x in args.selectivities.split(',') if x]
    for selectivity in targets:
        # One stream per slice: adding or removing slices never changes the others.
        slice_rng = np.random.default_rng(args.seed + int(round(selectivity * 100)))
        label = f'{selectivity * 100:g}'
        out = out_root / f'sel-{label}pct'
        out.mkdir(parents=True, exist_ok=True)
        for name, target in (('base.bin', Path('..') / 'base' / 'base.bin'),
                             ('edges.tsv', Path('..') / 'ccs-attr' / 'edges.tsv'),
                             ('objects.txt', Path('..') / 'ccs-attr' / 'objects.txt')):
            link = out / name
            if link.is_symlink() or link.exists():
                link.unlink()
            os.symlink(target, link)

        target_count = int(round(selectivity * base.shape[0]))
        query_lines = []
        ground_truth = np.full((args.queries, args.k), -1, dtype=np.int32)
        per_query = []
        for qi in range(args.queries):
            positions, count = pick_leaves(leaf_sizes, target_count, slice_rng)
            chosen = leaf_ids[positions]
            eligible = np.sort(np.concatenate([members[int(leaf)] for leaf in chosen]))
            if eligible.size != count:
                raise SystemExit(f'query {qi}: scheduled {count} objects but union holds {eligible.size}')
            if eligible.size < args.k:
                raise SystemExit(f'query {qi}: only {eligible.size} eligible objects for k={args.k}')
            vector = queries[qi]
            distances = ((base[eligible] - vector) ** 2).sum(axis=1, dtype=np.float32)
            order = np.argsort(distances, kind='stable')[:args.k]
            best_float32 = eligible[order]
            ground_truth[qi] = best_float32.astype(np.int32)
            query_lines.append(f'{chosen.size} ' + ' '.join(str(int(x)) for x in chosen))
            per_query.append(dict(qi=qi, attributes=int(chosen.size), eligible=int(eligible.size)))

        # The ranking must not hinge on float32 rounding: recompute a sample in float64.
        mismatches = 0
        for qi in range(min(args.queries, 8)):
            chosen = np.asarray([int(x) for x in query_lines[qi].split()[1:]], dtype=np.int64)
            eligible = np.sort(np.concatenate([members[int(leaf)] for leaf in chosen]))
            distances = ((base[eligible].astype(np.float64) - queries[qi].astype(np.float64)) ** 2).sum(axis=1)
            best_float64 = eligible[np.argsort(distances, kind='stable')[:args.k]]
            mismatches += int(not np.array_equal(best_float64, ground_truth[qi].astype(np.int64)))

        (out / 'queries.tsv').write_text('\n'.join(query_lines) + '\n')
        queries.astype(np.float32).tofile(out / 'queries.bin')
        ground_truth.astype(np.int32).tofile(out / f'gt_{args.k}.txt')

        eligible_counts = np.asarray([entry['eligible'] for entry in per_query])
        meta = dict(
            kind='fixed-selectivity slice of a hierarchical-attribute dataset',
            source=str(source.relative_to(ROOT)),
            dim=args.dim, objects=int(base.shape[0]), queries=int(args.queries), k=int(args.k),
            gt_depths=sorted(int(p.stem.split('_')[1]) for p in out.glob('gt_*.txt')),
            selectivity_target=selectivity,
            selectivity_achieved_mean=float(eligible_counts.mean() / base.shape[0]),
            eligible_mean=float(eligible_counts.mean()),
            eligible_min=int(eligible_counts.min()),
            eligible_max=int(eligible_counts.max()),
            attributes_per_query_mean=float(np.mean([entry['attributes'] for entry in per_query])),
            filter='union of leaf attributes; leaves have no descendants so the closure adds nothing',
            leaf_pool=int(leaf_ids.size), leaf_size_min=int(leaf_sizes.min()),
            leaf_size_median=float(np.median(leaf_sizes)), leaf_size_max=int(leaf_sizes.max()),
            seed=args.seed,
            ground_truth='exact FP32 squared L2 inside E(U), stable argsort; float64 cross-check on 8 queries: '
                         f'{mismatches} mismatches',
            queries_source=f'official fvecs rows 0..{args.queries - 1} of {args.query_fvecs}',
        )
        (out / 'meta.json').write_text(json.dumps(meta, indent=2) + '\n')
        print(f'{out}: target {selectivity:.3f} achieved {meta["selectivity_achieved_mean"]:.5f} '
              f'(eligible {eligible_counts.min()}..{eligible_counts.max()}), '
              f'{meta["attributes_per_query_mean"]:.1f} attributes/query, '
              f'float64 mismatches {mismatches}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
