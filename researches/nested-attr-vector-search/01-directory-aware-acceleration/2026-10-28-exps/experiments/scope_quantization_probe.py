#!/usr/bin/env python3
"""Cheap kill test for scope-aligned quantization (no codec required).

Question: within one directory scope, are quantization residuals on candidate
pairs MORE correlated than for random cross-scope pairs? Only then can
scope-aligned coarse assignment reduce ranking-flip risk through the difference
residual variance nu^2 = K_xx + K_yy - 2K_xy. A common additive bias cancels in
within-scope ranking, so correlation -- not the mean -- is what matters.

Proxy quantizer: centroid-only reconstruction (0 residual bits), the coarsest
possible case. This is deliberately conservative: if a shared coarse centroid
already gives no correlation advantage, residual bits cannot create scope
structure that is not present.

Not a benchmark: no recall claim, no latency claim, no memory claim.
Deterministic given --seed.

Usage:
  python scope_quantization_probe.py --data codes/directory-aware-vector-attr-search/data/wiki_full \
      --out researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/data/scope_quant_probe.json

Exit code 0 on success. Prints the JSON summary to stdout as well.
"""
import argparse
import json
import os

import numpy as np


def load_tree(path):
    with open(path) as f:
        m = int(f.readline().split()[0])
        parent = np.full(m, -1, dtype=np.int64)
        for line in f:
            parts = line.split()
            if len(parts) < 2:
                continue
            v, p = int(parts[0]), int(parts[1])
            parent[v] = p
    return parent


def load_bindings(path):
    with open(path) as f:
        return np.array([int(x) for x in f.read().split()], dtype=np.int64)


def node_preorder(parent):
    """DFS preorder over node ids: subtree(u) == order[pos[u]:pos[u]+size[u]]."""
    m = parent.shape[0]
    ch = [[] for _ in range(m)]
    for v in range(1, m):
        if parent[v] >= 0:
            ch[parent[v]].append(v)
    order = np.empty(m, dtype=np.int64)
    pos = np.empty(m, dtype=np.int64)
    size = np.ones(m, dtype=np.int64)
    order[0] = 0
    pos[0] = 0
    cursor = 1
    stack = [(0, 0)]
    while stack:
        u, ci = stack[-1]
        if ci < len(ch[u]):
            c = ch[u][ci]
            stack[-1] = (u, ci + 1)
            pos[c] = cursor
            order[cursor] = c
            cursor += 1
            stack.append((c, 0))
        else:
            stack.pop()
    for i in range(m - 1, 0, -1):
        v = order[i]
        size[parent[v]] += size[v]
    return order, pos, size


def kmeans(x, k, iters, rng):
    n = x.shape[0]
    cent = x[rng.choice(n, size=k, replace=False)].copy()
    xx = (x * x).sum(1)[:, None]
    for _ in range(iters):
        cc = (cent * cent).sum(1)[None, :]
        assign = (xx - 2.0 * x @ cent.T + cc).argmin(1)
        for j in range(k):
            sel = assign == j
            if sel.any():
                cent[j] = x[sel].mean(0)
    return cent, assign


def pearson(a, b):
    if a.size < 3:
        return float("nan")
    sa, sb = a.std(), b.std()
    if sa == 0.0 or sb == 0.0:
        return float("nan")
    return float(((a - a.mean()) * (b - b.mean())).mean() / (sa * sb))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ns", default="wiki")
    ap.add_argument("--dim", type=int, default=1024)
    ap.add_argument("--sample-vectors", type=int, default=60000)
    ap.add_argument("--centroids", type=int, default=256)
    ap.add_argument("--iters", type=int, default=8)
    ap.add_argument("--scopes", type=int, default=200)
    ap.add_argument("--min-scope", type=int, default=10)
    ap.add_argument("--max-scope", type=int, default=5000)
    ap.add_argument("--queries", type=int, default=32)
    ap.add_argument("--competitors", type=int, default=16)
    ap.add_argument("--seed", type=int, default=20261028)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    dim = args.dim

    parent = load_tree(f"{args.data}/{args.ns}_tree.txt")
    node_of_entry = load_bindings(f"{args.data}/{args.ns}_bindings.txt")
    n_entries = node_of_entry.shape[0]
    order, pos, size = node_preorder(parent)

    base_path = f"{args.data}/base.f32"
    n_vec = os.path.getsize(base_path) // (4 * dim)
    if n_vec != n_entries:
        raise SystemExit(f"base.f32 holds {n_vec} vectors, bindings has {n_entries}")

    ns = min(args.sample_vectors, n_vec)
    pick = np.sort(rng.choice(n_vec, size=ns, replace=False))
    mm = np.memmap(base_path, dtype=np.float32, mode="r").reshape(n_vec, dim)
    x = np.ascontiguousarray(mm[pick], dtype=np.float32)
    del mm

    cent, assign = kmeans(x, args.centroids, args.iters, rng)
    xhat = cent[assign]

    # sampled entries sorted by node preorder -> subtree members form a slice
    samp_pos = pos[node_of_entry[pick]]
    srt = np.argsort(samp_pos, kind="stable")
    samp_pos = samp_pos[srt]

    def scope_slice(u):
        lo = np.searchsorted(samp_pos, pos[u], "left")
        hi = np.searchsorted(samp_pos, pos[u] + size[u], "left")
        return srt[lo:hi]

    counts = np.bincount(node_of_entry, minlength=parent.shape[0]).astype(np.int64)
    sub_entries = counts.copy()
    for i in range(order.shape[0] - 1, 0, -1):
        v = order[i]
        sub_entries[parent[v]] += sub_entries[v]

    cand = np.where((sub_entries >= args.min_scope) & (sub_entries <= args.max_scope))[0]
    if cand.size == 0:
        raise SystemExit("no scope in the requested size band")
    scopes = rng.choice(cand, size=min(args.scopes, cand.size), replace=False)

    tq_path = f"{args.data}/test.f32"
    n_q_all = os.path.getsize(tq_path) // (4 * dim)
    qm = np.memmap(tq_path, dtype=np.float32, mode="r").reshape(n_q_all, dim)
    qsel = rng.choice(n_q_all, size=min(args.queries, n_q_all), replace=False)

    # accumulators
    w_xi_a, w_xi_b, w_margin, w_flip = [], [], [], []
    c_xi_a, c_xi_b, c_flip = [], [], []
    w_same_cent = w_pairs = 0
    c_same_cent = c_pairs = 0
    used_scopes = 0

    for qi in qsel:
        q = np.ascontiguousarray(qm[qi], dtype=np.float32)
        dq_all = ((x - q) ** 2).sum(1)
        dqh_all = ((xhat - q) ** 2).sum(1)
        xi_all = dqh_all - dq_all

        # --- within-scope candidate pairs (anchor = exact nearest member) ---
        for u in scopes:
            members = scope_slice(u)
            if members.size < 3:
                continue
            used_scopes += 1
            d = dq_all[members]
            rank = np.argsort(d, kind="stable")
            anchor = members[rank[0]]
            comp = members[rank[1 : 1 + args.competitors]]
            if comp.size == 0:
                continue
            ax = float(xi_all[anchor])
            bx = xi_all[comp]
            w_xi_a.append(np.full(comp.size, ax))
            w_xi_b.append(bx)
            w_margin.append(dq_all[comp] - dq_all[anchor])
            # ordering disagreement of the centroid-only proxy
            w_flip.append((dqh_all[comp] < dqh_all[anchor]).astype(np.int64))
            w_same_cent += int((assign[comp] == assign[anchor]).sum())
            w_pairs += int(comp.size)

        # --- cross-scope baseline: random distinct nodes -------------------
        for _ in range(args.competitors):
            a = int(rng.integers(0, ns))
            b = int(rng.integers(0, ns))
            if a == b or node_of_entry[pick][a] == node_of_entry[pick][b]:
                continue
            c_xi_a.append(np.array([xi_all[a]]))
            c_xi_b.append(np.array([xi_all[b]]))
            c_flip.append(np.array([int((dqh_all[b] < dqh_all[a]) != (dq_all[b] < dq_all[a]))]))
            c_same_cent += int(assign[a] == assign[b])
            c_pairs += 1

    def cat(lst):
        return np.concatenate(lst) if lst else np.array([])

    wa, wb = cat(w_xi_a), cat(w_xi_b)
    ca, cb = cat(c_xi_a), cat(c_xi_b)
    wf, cf = cat(w_flip), cat(c_flip)
    wm = cat(w_margin)

    def ratio(a, b, flip):
        """Independence ratio and residual-correlation summary."""
        out = {"pairs": int(a.size)}
        if a.size:
            diff = b - a
            var_diff = float(diff.var())
            var_sum = float(a.var() + b.var())
            out.update(
                pearson=pearson(a, b),
                var_sum_over_var_diff=(var_sum / var_diff) if var_diff > 0 else None,
                flip_rate=float(flip.mean()) if flip.size else None,
            )
        return out

    summary = {
        "kind": "scope_structure_probe_not_benchmark",
        "date": "2026-10-28",
        "inputs": {"data": args.data, "dim": dim, "vectors_in_base": int(n_vec),
                   "sample_vectors": int(ns), "centroids": int(args.centroids),
                   "kmeans_iters": int(args.iters), "seed": int(args.seed)},
        "quantizer": "centroid-only reconstruction (0 residual bits); conservative proxy",
        "scope_band_entries": [args.min_scope, args.max_scope],
        "sampled_scopes": int(scopes.size),
        "within_scope": ratio(wa, wb, wf),
        "cross_scope": ratio(ca, cb, cf),
        "same_centroid_fraction": {
            "within_scope": (w_same_cent / w_pairs) if w_pairs else None,
            "cross_scope": (c_same_cent / c_pairs) if c_pairs else None,
        },
        "within_scope_margin_below_1pct_of_median_fraction": (
            float((wm < 0.01 * np.median(wm)).mean()) if wm.size else None
        ),
        "interpretation_rule": {
            "kill_direction_if": "within_scope pearson <= cross_scope pearson, or within-scope same-centroid fraction not elevated",
            "note": "A higher same-centroid fraction alone is not evidence of lower flip risk; report both.",
        },
    }

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
