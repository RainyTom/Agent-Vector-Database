#!/usr/bin/env python3
"""ε 标定：每个查询的候选规模，以及不同 ε 分位下的输出对数（向量化实现）。

用法：
  work/venv-embed/bin/python codes/sift-attr-join/scripts/calibrate.py --data datasets/sift-attr
"""
import argparse
import json
import os
from collections import defaultdict

import h5py
import numpy as np

UNKNOWN = -1


def load_tree(path):
    lines = open(path).read().split('\n')
    parent, children = {}, defaultdict(list)
    for line in lines[1:]:
        if not line.strip():
            continue
        c, p = (int(x) for x in line.split()[:2])
        parent[c] = p
        if p >= 0:
            children[p].append(c)
    roots = [c for c, p in parent.items() if p < 0]
    depth = {r: 0 for r in roots}
    frontier = list(roots)
    while frontier:
        nxt = []
        for v in frontier:
            for c in children[v]:
                depth[c] = depth[v] + 1
                nxt.append(c)
        frontier = nxt
    anc = {}
    for v in parent:
        chain, cur = [], v
        while cur >= 0:
            chain.append(cur)
            cur = parent.get(cur, -1)
        anc[v] = chain
    desc = defaultdict(list)
    for v in anc:
        for a in anc[v]:
            desc[a].append(v)
    return parent, children, anc, depth, desc


def load_objects(path, nf):
    vecs, cats, fac = [], [], []
    for line in open(path):
        p = line.split()
        if not p:
            continue
        vecs.append(int(p[1]))
        cats.append(int(p[2]))
        fac.append([int(x) for x in p[3:3 + nf]])
    return np.array(vecs), np.array(cats), np.array(fac, dtype=np.int32)


def load_queries(path):
    out = []
    for line in open(path):
        p = [int(x) for x in line.split()]
        if not p:
            continue
        out.append((p[0], p[1], [(p[3 + 2 * i], p[4 + 2 * i]) for i in range(p[2])]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default='datasets/sift-attr')
    ap.add_argument('--tree', default='datasets/scihtc/scihtc_tree.txt')
    ap.add_argument('--sift', default='datasets/sift-128-euclidean.hdf5')
    ap.add_argument('--nf', type=int, default=3)
    ap.add_argument('--eps-q', default='0.001,0.005,0.01,0.02,0.05')
    ap.add_argument('--rebuild-D', action='store_true')
    args = ap.parse_args()
    out = os.path.join(args.data, 'eps-calibration.json')

    parent, children, anc, depth, desc = load_tree(args.tree)
    vecA, catA, facA = load_objects(os.path.join(args.data, 'objects_A.tsv'), args.nf)
    vecB, catB, facB = load_objects(os.path.join(args.data, 'objects_B.tsv'), args.nf)
    nA, nB = len(catA), len(catB)

    used = np.unique(np.concatenate([vecA, vecB]))
    with h5py.File(args.sift, 'r') as f:
        allv = f['train'][used]
    pos = {int(v): i for i, v in enumerate(used)}
    VA = allv[[pos[int(v)] for v in vecA]]
    VB = allv[[pos[int(v)] for v in vecB]]

    cache = os.path.join(args.data, 'cache')
    os.makedirs(cache, exist_ok=True)
    dpath = os.path.join(cache, f'D_{nA}x{nB}.npy')
    if args.rebuild_D or not os.path.exists(dpath) or os.path.getsize(dpath) != nA * nB * 4:
        D = np.memmap(dpath, dtype=np.float32, mode='w+', shape=(nA, nB))
        a2, b2 = (VA * VA).sum(1), (VB * VB).sum(1)
        for s in range(0, nA, 1024):
            e = min(s + 1024, nA)
            D[s:e] = a2[s:e, None] + b2[None, :] - 2.0 * (VA[s:e] @ VB.T)
        D.flush()
        del D
    D = np.memmap(dpath, dtype=np.float32, mode='r', shape=(nA, nB))

    locA, locB = defaultdict(list), defaultdict(list)
    for i, c in enumerate(catA):
        locA[int(c)].append(i)
    for i, c in enumerate(catB):
        locB[int(c)].append(i)
    locA = {k: np.array(v) for k, v in locA.items()}
    locB = {k: np.array(v) for k, v in locB.items()}

    def attr_mask(ai, bj):
        ok = np.ones((len(ai), len(bj)), dtype=bool)
        for d in range(args.nf):
            x, y = facA[ai, d][:, None], facB[bj, d][None, :]
            ok &= ~((x != UNKNOWN) & (y != UNKNOWN) & (x != y))
        return ok

    # 全量校验
    tot_cat = tot_attr = 0
    for cb, bj in locB.items():
        for ca in anc[cb]:
            ai = locA.get(ca)
            if ai is None:
                continue
            tot_cat += len(ai) * len(bj)
            tot_attr += int(attr_mask(ai, bj).sum())

    def in_phi(fac_idx, phi):
        keep = np.ones(len(fac_idx), dtype=bool)
        for d, v in phi:
            keep &= fac_idx[:, d] == v
        return keep

    qs = load_queries(os.path.join(args.data, 'queries.tsv'))
    eq = [x.strip() for x in args.eps_q.split(',')]  # 用字符串作 key，避免 float/str 混用
    rows = []
    for qid, u, phi in qs:
        inA = in_phi(facA, phi)
        inB = in_phi(facB, phi)
        Asel = {ca: ai[inA[ai]] for ca, ai in locA.items() if ca in desc[u]}
        Bsel = {cb: bj[inB[bj]] for cb, bj in locB.items() if cb in desc[u]}
        nAs = sum(len(v) for v in Asel.values())
        nBs = sum(len(v) for v in Bsel.values())
        pa, pb = [], []
        for cb, bj in Bsel.items():
            for ca in anc[cb]:
                ai = Asel.get(ca)
                if ai is None or not len(ai):
                    continue
                m = attr_mask(ai, bj)
                if not m.any():
                    continue
                ia, ib = np.nonzero(m)
                pa.append(ai[ia])
                pb.append(bj[ib])
        if pa:
            pa = np.concatenate(pa)
            pb = np.concatenate(pb)
            dist = np.asarray(D[pa, pb], dtype=np.float32)
            counts = {q: int((dist <= np.quantile(dist, float(q))).sum()) for q in eq}
            eps_at = {q: float(np.quantile(dist, float(q))) for q in eq}
        else:
            counts, eps_at = {}, {}
        rows.append({'qid': qid, 'u': u, 'phi': phi, 'A_Q': int(nAs), 'B_Q': int(nBs),
                     'pairs_after_attr': len(pa), 'counts_at_eps_quantile': counts,
                     'eps_at_quantile': eps_at})

    live = [r for r in rows if r['pairs_after_attr'] > 0]
    summary = {
        'n_queries': len(rows),
        'queries_with_pairs': len(live),
        'global_pairs_cat_compat': tot_cat,
        'global_pairs_attr_compat': tot_attr,
        'A_Q_median': float(np.median([r['A_Q'] for r in rows])),
        'B_Q_median': float(np.median([r['B_Q'] for r in rows])),
        'pairs_after_attr_median': float(np.median([r['pairs_after_attr'] for r in rows])),
        'pairs_after_attr_max': int(max(r['pairs_after_attr'] for r in rows)),
        'pairs_after_attr_sum': int(sum(r['pairs_after_attr'] for r in rows)),
        'counts_median_by_quantile': {q: float(np.median([r['counts_at_eps_quantile'].get(q, 0) for r in live]))
                                      for q in eq},
    }
    with open(out, 'w') as f:
        json.dump({'summary': summary, 'per_query': rows}, f, indent=1, ensure_ascii=False)
    print(json.dumps(summary, indent=1, ensure_ascii=False))


if __name__ == '__main__':
    main()
