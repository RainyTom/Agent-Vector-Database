#!/usr/bin/env python3
"""Degree retention inside the eligible set: the premise of the theory note.

For a query with down-set D and visitable cone E, measure, for nodes u whose
owner lies in D:

    retention(u) = |N(u) ∩ E| / M

  * unrestricted graph: edges point anywhere, so retention ~ |E|/n = delta;
  * down-closed graph: every edge of u stays in Desc+(own(u)) subset D subset E,
    so retention = 1 by construction.

This is a structural measurement: no beam search, no HNSW, no timing.  It tests
the premise, not the end-to-end benefit.

Real-data arm: exact M-NN of sampled items over the SciHTC/SIFT binding.
Synthetic arm: clustered concepts, same DAG shape, to show retention under
rho ~ 1 and to check the construction claim.
"""
import argparse
import json
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


def knn_of(vectors, ids, m):
    """Exact M-NN of the given row ids over all rows."""
    sq = (vectors ** 2).sum(1)
    out = np.empty((len(ids), m), dtype=np.int64)
    for i, node in enumerate(ids):
        d = sq + sq[node] - 2 * (vectors @ vectors[node])
        d[node] = np.inf
        idx = np.argpartition(d, m)[:m]
        out[i] = idx[np.argsort(d[idx])]
    return out


def retention(nodes, neighbours, owners, d_owners, e_owners, m):
    vals = []
    for i, node in enumerate(nodes):
        if int(owners[node]) not in d_owners:
            continue
        inside = sum(1 for v in neighbours[i] if int(owners[v]) in e_owners)
        vals.append(inside / m)
    return np.array(vals)


def real_arm(workload, vectors_path, sample_items, sample_queries, m, rng):
    edges = [tuple(map(int, l.split()))
             for l in (workload / 'edges.tsv').read_text().splitlines() if l.strip()]
    owners = np.array([int(l) for l in
                       (workload / 'objects.txt').read_text().splitlines() if l.strip()])
    n = len(owners)
    concepts = 1 + max(max(e) for e in edges)
    children = [[] for _ in range(concepts)]
    for child, parent in edges:
        children[parent].append(child)
    desc = [closure(c, children) for c in range(concepts)]

    queries = []
    for line in (workload / 'queries.tsv').read_text().splitlines():
        if line.strip():
            parts = list(map(int, line.split()))
            queries.append(parts[1:1 + parts[0]])
    picked = [queries[int(i)] for i in rng.choice(len(queries), sample_queries, replace=False)]

    dim = 128
    raw = np.fromfile(vectors_path, dtype=np.int32, count=n * (dim + 1)).reshape(n, dim + 1)
    vectors = raw[:, 1:].view(np.float32).copy()

    nodes = rng.choice(n, sample_items, replace=False)
    neighbours = knn_of(vectors, nodes, m)

    rows = []
    for scope in picked:
        d_owners = set().union(*(desc[u] for u in scope))
        e_owners = set().union(*(desc[c] for c in d_owners)) | d_owners
        vals = retention(nodes, neighbours, owners, d_owners, e_owners, m)
        if len(vals) == 0:
            continue
        rows.append({'density_E': len(np.flatnonzero(np.isin(owners, list(e_owners)))) / n,
                     'retention_mean': float(vals.mean())})
    dens = np.array([r['density_E'] for r in rows])
    ret = np.array([r['retention_mean'] for r in rows])
    return {
        'sampled_items': int(sample_items),
        'sampled_queries': len(rows),
        'density_E_mean': round(float(dens.mean()), 5),
        'density_E_p50': round(float(np.percentile(dens, 50)), 5),
        'density_E_p95': round(float(np.percentile(dens, 95)), 5),
        'retention_mean': round(float(ret.mean()), 4),
        'retention_p50': round(float(np.percentile(ret, 50)), 4),
        'retention_p95': round(float(np.percentile(ret, 95)), 4),
        'retention_over_density': round(float(np.mean(ret / dens)), 3),
        'prediction': 'unrestricted retention ~ density_E; ratio ~ 1 means the data agrees',
    }


def synthetic_arm(items_per_leaf, mids, leaves_per_mid, dim, m, queries, rng):
    concepts = ['R']
    children = {'R': []}
    for i in range(mids):
        mid = f'M{i}'
        concepts.append(mid)
        children['R'].append(mid)
        children[mid] = []
        for j in range(leaves_per_mid):
            leaf = f'L{i}_{j}'
            concepts.append(leaf)
            children[mid].append(leaf)
            children[leaf] = []
    cid = {c: i for i, c in enumerate(concepts)}
    desc = {c: closure(c, children) for c in concepts}

    vectors, owners = [], []
    centers = {c: rng.normal(0, 1, dim) * 10.0 for c in concepts}
    leaves = [c for c in concepts if c.startswith('L')]
    for c in leaves:
        vectors.append(centers[c] + rng.normal(0, 0.35, (items_per_leaf, dim)))
        owners.extend([cid[c]] * items_per_leaf)
    for c in concepts:
        if not c.startswith('L'):
            vectors.append(centers[c] + rng.normal(0, 0.35, (50, dim)))
            owners.extend([cid[c]] * 50)
    vectors = np.vstack(vectors).astype(np.float32)
    owners = np.array(owners)
    n = len(vectors)

    nodes = rng.choice(n, min(400, n), replace=False)
    neighbours = knn_of(vectors, nodes, m)
    cid_desc = {cid[c]: {cid[x] for x in desc[c]} for c in concepts}

    rows = []
    for _ in range(queries):
        leaf = leaves[int(rng.integers(len(leaves)))]
        d_owners = cid_desc[cid[leaf]]
        e_owners = set().union(*(cid_desc[x] for x in d_owners))
        vals = retention(nodes, neighbours, owners, d_owners, e_owners, m)
        if len(vals) == 0:
            continue
        rows.append((len(np.flatnonzero(np.isin(owners, list(e_owners)))) / n, float(vals.mean())))
    dens = np.array([r[0] for r in rows])
    ret = np.array([r[1] for r in rows])

    # rho on the same synthetic data
    rho = []
    for i, node in enumerate(nodes):
        allowed = cid_desc[int(owners[node])]
        rho.append(sum(1 for v in neighbours[i] if int(owners[v]) in allowed) / m)

    return {
        'items': int(n),
        'concepts': len(concepts),
        'density_E_mean': round(float(dens.mean()), 5),
        'retention_mean': round(float(ret.mean()), 4),
        'rho_mean': round(float(np.mean(rho)), 4),
        'down_closed_retention': 1.0,
        'note': 'down-closed arms retain M/M by construction because every edge of an '
                'eligible node targets a descendant, and descendants of an eligible concept '
                'are eligible',
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--workload', type=Path)
    parser.add_argument('--vectors', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--m', type=int, default=16)
    parser.add_argument('--sample-items', type=int, default=400)
    parser.add_argument('--sample-queries', type=int, default=40)
    args = parser.parse_args()

    rng = np.random.default_rng(20261102)
    out = {'kind': 'degree_retention_structural_measurement_not_ann',
           'm': args.m,
           'definition': 'retention(u) = |N(u) cap E| / M for nodes u with own(u) in D',
           'synthetic_clustered': synthetic_arm(400, 6, 10, 24, args.m, 40, rng)}
    if args.workload and args.vectors:
        out['real_scihtc_sift'] = real_arm(args.workload, args.vectors, args.sample_items,
                                           args.sample_queries, args.m, rng)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(out, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
