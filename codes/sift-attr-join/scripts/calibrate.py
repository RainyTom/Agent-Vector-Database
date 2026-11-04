#!/usr/bin/env python3
"""独立校验：全量品类兼容 pair 数与属性兼容 pair 数（块枚举，每块只计一次）。

ε 标定与分段计时在 C++ 侧完成（codes/sift-attr-join/src/join_bench.cpp）。
用法：work/venv-embed/bin/python codes/sift-attr-join/scripts/calibrate.py
"""
import argparse
import json
import os
from collections import defaultdict

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
    anc, desc = {}, defaultdict(list)
    for v in parent:
        chain, cur = [], v
        while cur >= 0:
            chain.append(cur)
            cur = parent.get(cur, -1)
        anc[v] = chain
        for a in chain:
            desc[a].append(v)
    return parent, children, anc, dict(desc), roots


def load_objects(path, nf):
    cats, fac = [], []
    for line in open(path):
        p = line.split()
        if not p:
            continue
        cats.append(int(p[2]))
        fac.append([int(x) for x in p[3:3 + nf]])
    return np.array(cats), np.array(fac, dtype=np.int32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default='datasets/sift-attr')
    ap.add_argument('--tree', default='datasets/scihtc/scihtc_tree.txt')
    ap.add_argument('--nf', type=int, default=3)
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    out = a.out or os.path.join(a.data, 'pair-counts.json')

    parent, children, anc, desc, roots = load_tree(a.tree)
    catA, facA = load_objects(os.path.join(a.data, 'objects_A.tsv'), a.nf)
    catB, facB = load_objects(os.path.join(a.data, 'objects_B.tsv'), a.nf)

    locA, locB = defaultdict(list), defaultdict(list)
    for i, c in enumerate(catA):
        locA[int(c)].append(i)
    for i, c in enumerate(catB):
        locB[int(c)].append(i)
    locA = {k: np.array(v) for k, v in locA.items()}
    locB = {k: np.array(v) for k, v in locB.items()}

    def attr_mask(ai, bj):
        ok = np.ones((len(ai), len(bj)), dtype=bool)
        for d in range(a.nf):
            x, y = facA[ai, d][:, None], facB[bj, d][None, :]
            ok &= ~((x != UNKNOWN) & (y != UNKNOWN) & (x != y))
        return ok

    tot_cat = tot_attr = 0
    nblocks = 0
    for ca, ai in locA.items():
        cand = [(cb, locB.get(cb)) for cb in anc[ca]]                      # c_b 是 c_a 的祖先（含相等）
        cand += [(cb, locB.get(cb)) for cb in desc[ca] if cb != ca]        # c_b 是 c_a 的真后代
        for cb, bj in cand:
            if bj is None:
                continue
            nblocks += 1
            tot_cat += len(ai) * len(bj)
            tot_attr += int(attr_mask(ai, bj).sum())

    res = {
        'tree_nodes': len(parent), 'tree_roots': len(roots),
        'A': int(len(catA)), 'B': int(len(catB)),
        'compatible_blocks': nblocks,
        'global_pairs_cat_compat': tot_cat,
        'global_pairs_attr_compat': tot_attr,
        'attr_pass_rate': round(tot_attr / tot_cat, 4) if tot_cat else 0.0,
    }
    with open(out, 'w') as f:
        json.dump(res, f, indent=1, ensure_ascii=False)
    print(json.dumps(res, indent=1, ensure_ascii=False))


if __name__ == '__main__':
    main()
