#!/usr/bin/env python3
"""DAG 搜索工作负载准备：量化（SQ8 / PQ）+ 精确 GT。

产物（默认写 work/dag-ann/）：
  meta.json         规模、压缩比、GT 参数
  recon.bin         还原后的 float32 向量（供 hnswlib 建图/搜索）
  codes_sq8.bin     SQ8 码（d 字节/向量）
  codes_pq.bin      PQ 码（m 字节/向量）
  gt_K.txt          每查询 k 个精确最近邻（对象 ID）
"""
import argparse, json, os
import numpy as np
import h5py

def load_dag(path):
    par = {}
    for line in open(path):
        a, b = (int(x) for x in line.split()[:2])
        par.setdefault(a, set()).add(b)
    ch = {}
    for c, ps in par.items():
        for p in ps: ch.setdefault(p, []).append(c)
    return par, ch

def closure_desc(roots, ch):
    seen, stack = set(), list(roots)
    while stack:
        v = stack.pop()
        if v in seen: continue
        seen.add(v); stack.extend(ch.get(v, ()))
    return seen

def kmeans(X, k, iters=20, seed=0):
    rng = np.random.default_rng(seed)
    C = X[rng.choice(len(X), k, replace=False)].copy()
    for _ in range(iters):
        d = ((X[:, None, :] - C[None, :, :]) ** 2).sum(-1) if len(X) < 4096 else None
        if d is None:
            assign = np.empty(len(X), dtype=np.int32)
            for s in range(0, len(X), 4096):
                e = min(s + 4096, len(X))
                dd = ((X[s:e, None, :] - C[None, :, :]) ** 2).sum(-1)
                assign[s:e] = dd.argmin(1)
        else:
            assign = d.argmin(1)
        for j in range(k):
            m = assign == j
            if m.any(): C[j] = X[m].mean(0)
    return C

def quant_sq8(X):
    lo, hi = X.min(0), X.max(0)
    scale = np.where(hi > lo, (hi - lo) / 255.0, 1.0)
    code = np.clip(np.round((X - lo) / scale), 0, 255).astype(np.uint8)
    recon = code.astype(np.float32) * scale.astype(np.float32) + lo.astype(np.float32)
    return code, recon

def quant_pq(X, m, k=256, seed=0):
    n, d = X.shape
    ds = d // m
    codes = np.empty((n, m), dtype=np.uint8)
    recon = np.empty_like(X, dtype=np.float32)
    for j in range(m):
        Xs = X[:, j*ds:(j+1)*ds]
        C = kmeans(Xs, k, iters=15, seed=seed + j)
        dd = ((Xs[:, None, :] - C[None, :, :]) ** 2).sum(-1)
        a = dd.argmin(1)
        codes[:, j] = a
        recon[:, j*ds:(j+1)*ds] = C[a]
    return codes, recon

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', default='work/dag-scope/workload-run1')
    ap.add_argument('--sift', default='datasets/sift-128-euclidean.hdf5')
    ap.add_argument('--out', default='work/dag-ann')
    ap.add_argument('--n', type=int, default=0, help='0=全部对象')
    ap.add_argument('--queries', type=int, default=200)
    ap.add_argument('--k', type=int, default=10)
    ap.add_argument('--pq-m', type=int, default=16)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    par, ch = load_dag(f'{args.work}/edges.tsv')
    obj = [int(x) for x in open(f'{args.work}/objects.txt') if x.strip()]
    qs = []
    for line in open(f'{args.work}/queries.tsv'):
        p = [int(x) for x in line.split()]
        if p: qs.append(p[1:1 + p[0]])
    n = args.n or len(obj)
    obj = obj[:n]
    with h5py.File(args.sift, 'r') as f:
        B = f['train'][:n].astype(np.float32)
        Q = f['test'][:args.queries].astype(np.float32)

    sq8_code, _ = quant_sq8(B)
    pq_code, pq_recon = quant_pq(B, args.pq_m)
    _, sq_recon = quant_sq8(B)
    # 搜索用 PQ 还原向量（code vector → 还原）
    np.ascontiguousarray(pq_recon, dtype=np.float32).tofile(f'{args.out}/recon.bin')
    sq8_code.tofile(f'{args.out}/codes_sq8.bin')
    pq_code.tofile(f'{args.out}/codes_pq.bin')

    # 精确 GT（原始 FP32）
    by_concept = {}
    for i, c in enumerate(obj):
        by_concept.setdefault(c, []).append(i)
    gt = np.full((len(Q), args.k), -1, dtype=np.int64)
    elig_sizes = []
    for qi, q in enumerate(Q):
        members = set()
        for a in qs[qi]:
            for c in closure_desc([a], ch):
                members.update(by_concept.get(c, ()))
        elig_sizes.append(len(members))
        if not members:
            continue
        idx = np.fromiter(members, dtype=np.int64)
        d = ((B[idx] - q) ** 2).sum(1)
        order = np.lexsort((idx, d))[:args.k]
        gt[qi, :len(order)] = idx[order]
    gt.tofile(f'{args.out}/gt_{args.k}.txt')

    meta = {
        'objects': n, 'queries': len(Q), 'k': args.k,
        'concepts_total': len(set(obj)), 'dag_nodes': len(par) + len(set().union(*par.values())),
        'multi_parent': sum(1 for v in par.values() if len(v) > 1),
        'eligible_mean': float(np.mean(elig_sizes)), 'eligible_max': int(np.max(elig_sizes)),
        'empty_queries': int(sum(1 for s in elig_sizes if s == 0)),
        'bytes_fp32': int(B.nbytes), 'bytes_sq8': int(sq8_code.nbytes), 'bytes_pq': int(pq_code.nbytes),
        'compression_sq8': round(B.nbytes / sq8_code.nbytes, 2),
        'compression_pq': round(B.nbytes / pq_code.nbytes, 2),
        'search_vector': 'PQ(m=%d, k=256) reconstructed float32' % args.pq_m,
    }
    json.dump(meta, open(f'{args.out}/meta.json', 'w'), indent=1)
    print(json.dumps(meta, indent=1))

if __name__ == '__main__':
    main()
