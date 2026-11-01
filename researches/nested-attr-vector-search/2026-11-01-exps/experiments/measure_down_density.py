#!/usr/bin/env python3
"""Down-density rho with a permutation null.

rho(u) = fraction of u's exact k nearest neighbours whose owner lies in
Desc+(own(u)).  The analytic expectation depends on cone mass, and the mean is
dominated by few large cones, so we report:

  * rho, restricted to items whose cone holds < 50% of all items
  * lift = rho(real) / rho(permuted owners), same items and same neighbours

lift ~ 1 means concepts carry no geometric information under this binding;
lift >> 1 means descendants cluster around their ancestors.

No ANN structure is used: neighbours are brute force, so this measures the data,
not an index.  Output is a JSON evidence file.
"""
import argparse
import json
from collections import Counter
from pathlib import Path

import numpy as np


def closure(start, step):
    seen, stack = set(), [start]
    while stack:
        node = stack.pop()
        if node in seen:
            continue
        seen.add(node)
        stack.extend(step[node])
    return seen


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--workload', type=Path, required=True)
    parser.add_argument('--vectors', type=Path, required=True)
    parser.add_argument('--sample', type=int, default=1000)
    parser.add_argument('--k', type=int, default=10)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    edges = [tuple(map(int, line.split()))
             for line in (args.workload / 'edges.tsv').read_text().splitlines() if line.strip()]
    owners = np.array([int(line) for line in
                       (args.workload / 'objects.txt').read_text().splitlines() if line.strip()])
    n = len(owners)
    concepts = 1 + max(max(e) for e in edges)
    children = [[] for _ in range(concepts)]
    for child, parent in edges:
        children[parent].append(child)
    desc = [closure(c, children) for c in range(concepts)]
    masks = [sum(1 << d for d in desc[c]) for c in range(concepts)]
    count = Counter(int(o) for o in owners)
    cone_items = np.array([sum(count[d] for d in desc[c]) for c in range(concepts)])

    dim = 128
    raw = np.fromfile(args.vectors, dtype=np.int32, count=n * (dim + 1)).reshape(n, dim + 1)
    base = raw[:, 1:].view(np.float32).copy()

    rng = np.random.default_rng(20261101)
    sample = rng.choice(n, min(args.sample, n), replace=False)
    sq = (base ** 2).sum(1)
    neighbours = np.empty((len(sample), args.k), dtype=np.int64)
    for i, s in enumerate(sample):
        dist = sq - 2 * base @ base[s]
        idx = np.argpartition(dist, args.k)[:args.k]
        neighbours[i] = idx[np.argsort(dist[idx])]

    def rho_of(owner_array):
        out = []
        for i, s in enumerate(sample):
            mask = masks[int(owner_array[s])]
            inside = sum(1 for v in neighbours[i] if (mask >> int(owner_array[v])) & 1)
            out.append(inside / args.k)
        return np.array(out)

    real = rho_of(owners)
    permuted = owners.copy()
    rng.shuffle(permuted)
    null = rho_of(permuted)
    # Null-model sanity: two independent random permutations must give lift ~ 1.
    p1, p2 = owners.copy(), owners.copy()
    rng.shuffle(p1)
    rng.shuffle(p2)
    r1, r2 = rho_of(p1), rho_of(p2)
    sanity_lift = float(r1.mean() / r2.mean()) if r2.mean() else None
    runs = 1 + int(sum(1 for i in range(1, len(owners)) if owners[i] != owners[i - 1]))

    share = np.array([cone_items[int(owners[s])] / n for s in sample])
    small = share < 0.5                       # exclude root-like cones
    result = {
        'kind': 'down_density_measurement_not_ann_experiment',
        'vectors': str(args.vectors),
        'workload': str(args.workload),
        'items': int(n),
        'concepts': int(concepts),
        'sampled_items': int(len(sample)),
        'k': args.k,
        'rho_real_mean': round(float(real.mean()), 5),
        'rho_null_mean': round(float(null.mean()), 5),
        'lift_mean': round(float(real.mean() / null.mean()), 3) if null.mean() else None,
        'restricted_to_cone_share_below_half': {
            'items': int(small.sum()),
            'rho_real_mean': round(float(real[small].mean()), 5) if small.any() else None,
            'rho_null_mean': round(float(null[small].mean()), 5) if small.any() else None,
            'lift_mean': (round(float(real[small].mean() / null[small].mean()), 3)
                          if small.any() and null[small].mean() else None),
        },
        'cone_item_share_mean': round(float(share.mean()), 5),
        'file_order': {'contiguous_owner_runs': runs,
                       'grouped_by_concept': runs < 0.01 * n,
                       'note': 'SIFT1M is ordered by source image, so a concept-grouped '
                               'file order creates spurious local correlation; the ANN probes '
                               'shuffle object ids for exactly this reason'},
        'random_vs_random_lift': round(sanity_lift, 3) if sanity_lift else None,
        'interpretation': [
            'lift near 1 means the concept hierarchy carries no information about geometric '
            'neighbourhoods under this label-vector binding; the current binding was assigned '
            'randomly, so near-1 is expected and is not evidence about real corpora',
            'a down-edge-only graph cannot beat an unrestricted graph when lift ~ 1',
            'this quantity, not the DAG shape, decides whether cone pruning can win',
        ],
        'limits': [
            'brute-force neighbours, no ANN, no timing',
            'single binding; no real text embeddings available in this checkout',
            'mean is dominated by large cones, hence the restricted row',
        ],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
