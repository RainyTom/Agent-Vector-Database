#!/usr/bin/env python3
"""统一量化：PQ(m=16,k=256)。幂等：已有 codes/recon/GT 则跳过（--force 可强制重建）。

训练用抽样（默认 10 万），编码全量；GT 在原始 FP32 上算，k 默认 50。
用法: prepare_pq.py --data datasets/sift1m/ccs-attr-1m
"""
import argparse
import json
import os
import sys

import numpy as np


def kmeans(X, k, iters, seed):
    rng = np.random.default_rng(seed)
    C = X[rng.choice(len(X), k, replace=False)].copy()
    for _ in range(iters):
        a = np.empty(len(X), dtype=np.int32)
        for s in range(0, len(X), 8192):
            e = min(s + 8192, len(X))
            d = ((X[s:e, None, :] - C[None, :, :]) ** 2).sum(-1)
            a[s:e] = d.argmin(1)
        for j in range(k):
            m = a == j
            if m.any():
                C[j] = X[m].mean(0)
    return C


def encode_pq(B, m, k, train_n, iters):
    n, d = B.shape
    ds = d // m
    rng = np.random.default_rng(7)
    sample = B[rng.choice(n, min(train_n, n), replace=False)]
    codes = np.empty((n, m), dtype=np.uint8)
    recon = np.empty((n, d), dtype=np.float32)
    for j in range(m):
        Xs = sample[:, j * ds:(j + 1) * ds]
        C = kmeans(Xs, k, iters, seed=100 + j)
        for s in range(0, n, 16384):
            e = min(s + 16384, n)
            Xb = B[s:e, j * ds:(j + 1) * ds]
            dd = ((Xb[:, None, :] - C[None, :, :]) ** 2).sum(-1)
            idx = dd.argmin(1)
            codes[s:e, j] = idx
            recon[s:e, j * ds:(j + 1) * ds] = C[idx]
    return codes, recon


def build_gt(data, work, topk, nq):
    B = np.fromfile(f'{data}/orig.bin', dtype=np.float32).reshape(-1, 128)
    obj = np.array([int(x) for x in open(f'{data}/objects.txt') if x.strip()])
    par = {}
    for line in open(f'{data}/edges.tsv'):
        c, p = (int(x) for x in line.split()[:2])
        par.setdefault(c, set()).add(p)
    ch = {}
    for c, ps in par.items():
        for p in ps:
            ch.setdefault(p, []).append(c)
    qs = []
    for line in open(f'{data}/queries.tsv'):
        pp = [int(x) for x in line.split()]
        if pp:
            qs.append(pp[1:1 + pp[0]])
    qs = qs[:nq]
    Q = np.fromfile(f'{data}/queries.bin', dtype=np.float32).reshape(-1, 128)[:nq]
    by = {}
    for i, c in enumerate(obj):
        by.setdefault(int(c), []).append(i)

    def clo(roots):
        seen, st = set(), list(roots)
        while st:
            v = st.pop()
            if v in seen:
                continue
            seen.add(v)
            st.extend(ch.get(v, ()))
        return seen

    gt = np.full((len(Q), topk), -1, dtype=np.int32)
    for qi, q in enumerate(Q):
        mm = set()
        for x in qs[qi]:
            for c in clo([x]):
                mm.update(by.get(c, ()))
        if not mm:
            continue
        idx = np.fromiter(mm, dtype=np.int64)
        d = ((B[idx] - Q[qi]) ** 2).sum(1)
        order = np.lexsort((idx, d))[:topk]
        gt[qi, :len(order)] = idx[order]
    gt.tofile(f'{data}/gt_{topk}.txt')
    return gt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', required=True)
    ap.add_argument('--m', type=int, default=16)
    ap.add_argument('--k', type=int, default=256)
    ap.add_argument('--train', type=int, default=100000)
    ap.add_argument('--iters', type=int, default=12)
    ap.add_argument('--topk', type=int, default=50)
    ap.add_argument('--queries', type=int, default=200)
    ap.add_argument('--force', action='store_true')
    a = ap.parse_args()

    codes_p = f'{a.data}/codes_pq.bin'
    recon_p = f'{a.data}/recon_pq.bin'
    gt_p = f'{a.data}/gt_{a.topk}.txt'

    B = np.fromfile(f'{a.data}/orig.bin', dtype=np.float32).reshape(-1, 128)
    n = len(B)

    if a.force or not (os.path.exists(codes_p) and os.path.exists(recon_p)):
        codes, recon = encode_pq(B, a.m, a.k, a.train, a.iters)
        codes.tofile(codes_p)
        recon.tofile(recon_p)
        print('[built] PQ codes + recon')
    else:
        print('[skip] PQ codes exist')

    if a.force or not os.path.exists(gt_p):
        build_gt(a.data, a.data, a.topk, a.queries)
        print('[built] GT', gt_p)
    else:
        print('[skip] GT exists')

    meta_p = f'{a.data}/meta.json'
    meta = json.load(open(meta_p)) if os.path.exists(meta_p) else {}
    meta.update({'pq_m': a.m, 'pq_k': a.k, 'pq_bytes_per_vec': a.m,
                 'compression_pq': round(n * 128 * 4 / (n * a.m), 2),
                 'gt_vector': 'original FP32', 'k': a.topk, 'queries': a.queries})
    json.dump(meta, open(meta_p, 'w'), indent=1)
    print(json.dumps({k: meta[k] for k in ('objects', 'queries', 'k', 'pq_m', 'compression_pq')}, indent=1))


if __name__ == '__main__':
    main()
