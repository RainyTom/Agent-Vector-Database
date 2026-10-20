#!/usr/bin/env python3
"""Measure subtree covering radii on the 85-node SIFT100k fixture.

Supports the interval-local quantization component: quantization error of a
codebook built over a set scales with that set's covering radius, so we report
the per-depth radius ratio gamma_u = R(u) / R(root) as a *measured* statistic.

Inputs are read-only fixtures produced by prep.cpp; nothing here changes them.
"""
import os
import struct

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
FIX = os.path.join(ROOT, "production", "nested-attr-vector-search", "work",
                   "fixtures")
OUT = os.path.join(ROOT, "researches", "nested-attr-vector-search",
                   "07-laminar-edge-sharing", "2026-10-20-exps", "data")

meta = open(os.path.join(FIX, "meta.txt")).read().split()
n, dim, nq, k, n_nodes, _ = map(int, meta)

base = np.fromfile(os.path.join(FIX, "base.f32"), dtype=np.float32).reshape(n, dim)

parents = []
with open(os.path.join(FIX, "tree.txt")) as f:
    first = int(f.readline())
    assert first == n_nodes
    for _ in range(n_nodes):
        _, p = map(int, f.readline().split())
        parents.append(p)
children = [[] for _ in range(n_nodes)]
for v, p in enumerate(parents):
    if p >= 0:
        children[p].append(v)

depth = [0] * n_nodes
for v in range(n_nodes):  # parents precede children in tree.txt
    if parents[v] >= 0:
        depth[v] = depth[parents[v]] + 1

labels = np.fromfile(os.path.join(FIX, "labels.bin"), dtype=np.int32).reshape(n, -1)

# feasible members of each subtree
members = {}
for v in range(n_nodes):
    desc = set()
    stack = [v]
    while stack:
        x = stack.pop()
        desc.add(x)
        stack.extend(children[x])
    members[v] = np.where(np.isin(labels, list(desc)).any(axis=1))[0]

root_ids = members[0]
root_mean = base[root_ids].mean(axis=0)
root_radius = float(np.sqrt(((base[root_ids] - root_mean) ** 2).sum(axis=1).max()))

rows = []
for v in range(n_nodes):
    ids = members[v]
    if len(ids) < 8:
        continue
    mean = base[ids].mean(axis=0)
    radius = float(np.sqrt(((base[ids] - mean) ** 2).sum(axis=1).max()))
    rows.append((v, depth[v], len(ids), radius, radius / root_radius))

print(f"root_radius={root_radius:.2f} nodes_measured={len(rows)}")
by_depth = {}
for v, d, s, r, g in rows:
    by_depth.setdefault(d, []).append(g)
for d in sorted(by_depth):
    gs = np.array(by_depth[d])
    print(f"depth {d}: n={len(gs):3d} gamma mean={gs.mean():.3f} "
          f"median={np.median(gs):.3f} min={gs.min():.3f} max={gs.max():.3f}")

os.makedirs(OUT, exist_ok=True)
path = os.path.join(OUT, "subtree-radius.csv")
with open(path, "w") as f:
    f.write("node,depth,members,radius,gamma\n")
    for v, d, s, r, g in rows:
        f.write(f"{v},{d},{s},{r:.4f},{g:.4f}\n")
print("wrote", path)
