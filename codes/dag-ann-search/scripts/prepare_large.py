#!/usr/bin/env python3
"""1M 规模：SIFT1M + 同一 CCS DAG；对象->概念的绑定按原 168,937 标签的经验分布扩张（固定种子）。"""
import argparse, json, os
import numpy as np, h5py

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', default='work/dag-scope/workload-run1')
    ap.add_argument('--sift', default='datasets/sift-128-euclidean.hdf5')
    ap.add_argument('--out', default='work/dag-ann-1m')
    ap.add_argument('--n', type=int, default=1000000)
    ap.add_argument('--queries', type=int, default=200)
    ap.add_argument('--k', type=int, default=50)
    ap.add_argument('--seed', type=int, default=20261105)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rng = np.random.default_rng(a.seed)

    labels = np.array([int(x) for x in open(f'{a.work}/objects.txt') if x.strip()])
    if a.n <= len(labels):
        obj = labels[:a.n]
    else:
        obj = rng.choice(labels, size=a.n, replace=True)     # 保持经验分布，扩张到 n
    np.savetxt(f'{a.out}/objects.txt', obj, fmt='%d')

    with h5py.File(a.sift, 'r') as f:
        B = f['train'][:a.n].astype(np.float32)
        Q = f['test'][:a.queries].astype(np.float32)
    B.tofile(f'{a.out}/orig.bin')
    Q.tofile(f'{a.out}/queries.bin')
    lo, hi = B.min(0), B.max(0)
    scale = np.where(hi > lo, (hi - lo) / 255.0, 1.0).astype(np.float32)
    code = np.clip(np.round((B - lo) / scale), 0, 255).astype(np.uint8)
    recon = code.astype(np.float32) * scale + lo.astype(np.float32)
    recon.tofile(f'{a.out}/recon_sq8.bin')
    code.tofile(f'{a.out}/codes_sq8.bin')
    del B

    # GT：DAG 可达闭包
    par = {}
    for line in open(f'{a.work}/edges.tsv'):
        c, p = (int(x) for x in line.split()[:2]); par.setdefault(c, set()).add(p)
    ch = {}
    for c, ps in par.items():
        for p in ps: ch.setdefault(p, []).append(c)
    qs = []
    for line in open(f'{a.work}/queries.tsv'):
        pp = [int(x) for x in line.split()]
        if pp: qs.append(pp[1:1 + pp[0]])
    qs = qs[:a.queries]
    by = {}
    for i, c in enumerate(obj): by.setdefault(int(c), []).append(i)
    def clo(roots):
        seen, st = set(), list(roots)
        while st:
            v = st.pop()
            if v in seen: continue
            seen.add(v); st.extend(ch.get(v, ()))
        return seen
    gt = np.full((len(Q), a.k), -1, dtype=np.int32); sizes = []
    for qi, q in enumerate(Q):
        m = set()
        for x in qs[qi]:
            for c in clo([x]): m.update(by.get(c, ()))
        sizes.append(len(m))
        if not m: continue
        idx = np.fromiter(m, dtype=np.int64)
        d = ((recon[idx] - Q[qi]) ** 2).sum(1)        # GT 与搜索同向量（SQ8 还原），以隔离图的影响
        order = np.lexsort((idx, d))[:a.k]
        gt[qi, :len(order)] = idx[order]
    gt.tofile(f'{a.out}/gt_{a.k}.txt')
    meta = {'objects': int(a.n), 'queries': len(Q), 'k': a.k,
            'concepts': len(by), 'bind': 'multinomial over original label distribution',
            'eligible_mean': float(np.mean(sizes)), 'eligible_max': int(max(sizes)),
            'empty': int(sum(1 for s in sizes if s == 0)),
            'bytes_fp32': int(a.n*128*4), 'bytes_sq8_codes': int(a.n*128),
            'compression_sq8': 4.0, 'gt_vector': 'SQ8 reconstructed'}
    json.dump(meta, open(f'{a.out}/meta.json','w'), indent=1)
    print(json.dumps(meta, indent=1))

if __name__ == '__main__':
    main()
