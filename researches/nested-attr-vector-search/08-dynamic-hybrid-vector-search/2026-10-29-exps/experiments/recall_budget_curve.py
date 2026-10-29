#!/usr/bin/env python3
"""C1 gating experiment, part 3: recall-vs-budget curve (the decision number).

Rank scope nodes by a cheap summary, visit them in that order, and record the
visited-item fraction at which GT-item recall reaches 0.9 / 0.99 / 0.999 / 1.0.
This is the achievable ceiling of "route then search exactly", so it upper-bounds
any routed method that uses this summary.

Scorers: centroid distance, centroid-minus-radius, per-node sample min distance.
"""
import argparse
import csv
import json
import time
from pathlib import Path

import numpy as np


def load_tree(path):
    toks = Path(path).read_text().split()
    m = int(toks[0])
    parent = np.full(m, -1, dtype=np.int64)
    vals = np.asarray(toks[1:], dtype=np.int64).reshape(m, 2)
    parent[vals[:, 0]] = vals[:, 1]
    children = [[] for _ in range(m)]
    for v in range(1, m):
        children[int(parent[v])].append(v)
    return parent, children


def analyze(name, data_dir, gt_csv, dim, sample_s, out_prefix):
    t0 = time.time()
    parent, children = load_tree(Path(data_dir) / "tree.txt")
    bindings = np.loadtxt(Path(data_dir) / "bindings.txt", dtype=np.int64)
    n, m = len(bindings), len(parent)
    wl = np.loadtxt(Path(data_dir) / "workload.txt", dtype=np.int64, skiprows=1)
    base = np.memmap(Path(data_dir) / "base.f32", mode="r", dtype="<f4", shape=(n, dim))
    test = np.memmap(Path(data_dir) / "test.f32", mode="r", dtype="<f4").reshape(-1, dim)

    gt = {}
    with open(gt_csv) as f:
        for r in csv.DictReader(f):
            gt[int(r["query_index"])] = [int(t) for t in
                                         r["result_ids"].replace(";", " ").split() if int(t) >= 0]

    counts = np.bincount(bindings, minlength=m)
    order = np.argsort(bindings, kind="stable")
    starts = np.zeros(m + 1, dtype=np.int64)
    np.cumsum(counts, out=starts[1:])
    item_node = np.empty(n, dtype=np.int64)
    item_node[order] = bindings[order]

    cent = np.zeros((m, dim), dtype=np.float64)
    np.add.at(cent, bindings, np.asarray(base, dtype=np.float64))
    nonempty = counts > 0
    cent[nonempty] /= counts[nonempty, None]
    radii = np.zeros(m)
    for s0 in range(0, n, 4096):
        idx = np.arange(s0, min(s0 + 4096, n))
        d = ((np.asarray(base[idx], dtype=np.float64) - cent[bindings[idx]]) ** 2).sum(1)
        np.maximum.at(radii, bindings[idx], d)
    radii = np.sqrt(radii)

    rng = np.random.default_rng(20261029)
    samp_node, samp_item = [], []
    for v in np.flatnonzero(nonempty):
        its = order[starts[v]:starts[v + 1]]
        if len(its) > sample_s:
            its = its[rng.choice(len(its), sample_s, replace=False)]
        samp_node.extend([v] * len(its))
        samp_item.extend(its.tolist())
    samp_node = np.asarray(samp_node)
    samp_item = np.asarray(samp_item)
    sorder = np.argsort(samp_node, kind="stable")
    samp_node, samp_vec = samp_node[sorder], np.asarray(base[samp_item[sorder]], dtype=np.float32)
    print(f"[{name}] setup {time.time()-t0:.0f}s samples={len(samp_item)} "
          f"({len(samp_item)/n*100:.0f}% of corpus)", flush=True)

    targets = (0.9, 0.99, 0.999, 1.0)
    rows = []
    for qi, (qidx, rec, anchor) in enumerate(wl):
        if rec != 1:
            continue
        ids = gt.get(qi)
        if not ids:
            continue
        scope = desc(children, int(anchor))
        scope = np.sort(np.asarray([v for v in scope if counts[v] > 0], dtype=np.int64))
        q = np.asarray(test[qidx], dtype=np.float64)
        d_cent = np.sqrt(((cent[scope] - q) ** 2).sum(1))
        d_lb = d_cent - radii[scope]
        d_samp = np.full(len(scope), np.inf)
        smask = np.zeros(m, dtype=bool)
        smask[scope] = True
        keep = smask[samp_node]
        if keep.any():
            ds = np.sqrt(((samp_vec[keep] - q.astype(np.float32)) ** 2).sum(1))
            np.minimum.at(d_samp, np.searchsorted(scope, samp_node[keep]), ds)

        gt_arr = np.asarray(ids)
        gt_node = item_node[gt_arr]
        scope_items = int(counts[scope].sum())
        out = {"query_index": qi, "scope_items": scope_items, "scope_nodes": len(scope)}
        for nm, sc in (("centroid", d_cent), ("lb", d_lb), ("samplevote", d_samp)):
            o = np.argsort(sc, kind="stable")
            nodes_in_order = scope[o]
            # cumulative items visited
            cum_items = np.cumsum(counts[nodes_in_order])
            # GT item -> its node -> position of that node in the score order
            by_id = np.argsort(nodes_in_order)
            nodes_sorted = nodes_in_order[by_id]
            p_sorted = np.searchsorted(nodes_sorted, gt_node)
            p_sorted = np.clip(p_sorted, 0, len(nodes_sorted) - 1)
            # keep only true matches (defensive: GT must be in scope)
            pos = by_id[p_sorted]
            pos = pos[nodes_sorted[p_sorted] == gt_node]
            # accumulate GT coverage in order of node appearance
            hit_items = np.zeros(len(nodes_in_order))
            for p in pos:
                hit_items[p] += 1
            cum_hits = np.cumsum(hit_items)
            frac_items = cum_items / scope_items
            for t in targets:
                need = t * len(gt_arr)
                idx = np.searchsorted(cum_hits, need, side="left")
                out[f"{nm}_budget_at_{t}"] = (frac_items[idx]
                                              if idx < len(frac_items) else 1.0)
        rows.append(out)
        if len(rows) % 40 == 0:
            print(f"[{name}] {len(rows)} ({time.time()-t0:.0f}s)", flush=True)

    with open(f"{out_prefix}.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    def blk(sub, tag):
        d = {f"{tag}n": len(sub)}
        for nm in ("centroid", "lb", "samplevote"):
            for t in targets:
                d[f"{tag}{nm}_at_{t}_median_items_frac"] = float(
                    np.median([r[f"{nm}_budget_at_{t}"] for r in sub]))
        return d
    big = [r for r in rows if r["scope_items"] >= 10000]
    agg = {"dataset": name, "sample_s": sample_s,
           "sample_fraction_of_corpus": round(float(len(samp_item) / n), 4),
           "elapsed_s": round(time.time() - t0, 1)}
    agg.update(blk(rows, "all_"))
    if big:
        agg.update(blk(big, "ge10k_"))
    Path(f"{out_prefix}.json").write_text(json.dumps(agg, indent=2) + "\n")
    print(json.dumps(agg, indent=2), flush=True)


def desc(children, root):
    out, st = [], [root]
    while st:
        v = st.pop()
        out.append(v)
        st.extend(children[v])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--gt", required=True)
    ap.add_argument("--dim", type=int, required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--sample-s", type=int, default=4)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    analyze(a.name, a.data, a.gt, a.dim, a.sample_s, a.out)


if __name__ == "__main__":
    main()
