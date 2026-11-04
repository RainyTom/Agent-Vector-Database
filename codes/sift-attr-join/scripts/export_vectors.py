#!/usr/bin/env python3
"""把 objects_A/B 用到的 SIFT 向量按 A 序、B 序导出为 float32 裸文件，供 join_bench 直接读取。"""
import argparse, os
import h5py, numpy as np

def load_vec_idx(path):
    out = []
    for line in open(path):
        p = line.split()
        if p: out.append(int(p[1]))
    return np.array(out)

ap = argparse.ArgumentParser()
ap.add_argument('--data', default='datasets/sift-attr')
ap.add_argument('--sift', default='datasets/sift-128-euclidean.hdf5')
a = ap.parse_args()
va = load_vec_idx(os.path.join(a.data, 'objects_A.tsv'))
vb = load_vec_idx(os.path.join(a.data, 'objects_B.tsv'))
used = np.unique(np.concatenate([va, vb]))
with h5py.File(a.sift, 'r') as f:
    allv = f['train'][used]
pos = {int(v): i for i, v in enumerate(used)}
A = allv[[pos[int(v)] for v in va]].astype('float32')
B = allv[[pos[int(v)] for v in vb]].astype('float32')
p = os.path.join(a.data, 'cache', 'vectors.bin')
os.makedirs(os.path.dirname(p), exist_ok=True)
with open(p, 'wb') as f:
    f.write(A.tobytes()); f.write(B.tobytes())
print('wrote', p, A.shape, B.shape)
