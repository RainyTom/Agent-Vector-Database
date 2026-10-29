#!/usr/bin/env python3
"""C1 gating experiment: oracle routing analysis (offline, no new index).

For every query, given the exact top-k GT (from the B3 exact run):
  (a) oracle node set O(q): direct nodes holding GT items (|O| <= k);
  (b) upper-bound item-visit savings: 1 - sum(|L(v)| for v in O) / |S|;
  (c) summary routing quality, three scorers:
      centroid   : d(q, c_v)                         (mean vector per node)
      lb         : d(q, c_v) - r_v                   (centroid minus radius)
      samplevote : min over per-node uniform samples of d(q, sample)
    For each scorer: minimal node-prefix (by rank) covering all oracle nodes,
    the item savings at full coverage, and coverage at fixed budgets.

Decision question (stage2 C1): is the oracle savings bound > 20% on >=10k
scopes, and can a cheap summary find oracle nodes? If not, C1 closes.
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


def descendants(children, root):
    out, stack = [], [root]
    while stack:
        v = stack.pop()
        out.append(v)
        stack.extend(children[v])
    return np.asarray(out, dtype=np.int64)


def analyze(name, data_dir, gt_csv, dim, sample_s, out_prefix):
    t0 = time.time()
    parent, children = load_tree(Path(data_dir) / "tree.txt")
    bindings = np.loadtxt(Path(data_dir) / "bindings.txt", dtype=np.int64)
    n = len(bindings)
    m = len(parent)
    wl = np.loadtxt(Path(data_dir) / "workload.txt", dtype=np.int64, skiprows=1)
    base = np.memmap(Path(data_dir) / "base.f32", mode="r",
                     dtype="<f4", shape=(n, dim))
    test_raw = np.memmap(Path(data_dir) / "test.f32", mode="r", dtype="<f4")
    test = test_raw.reshape(-1, dim)

    # GT from the exact run
    gt = {}
    with open(gt_csv) as f:
        for r in csv.DictReader(f):
            ids = [int(t) for t in r["result_ids"].replace(";", " ").split()
                   if int(t) >= 0]
            gt[int(r["query_index"])] = ids

    # per-node item lists (sorted by item id for determinism)
    order = np.argsort(bindings, kind="stable")
    counts = np.bincount(bindings, minlength=m)
    starts = np.zeros(m + 1, dtype=np.int64)
    np.cumsum(counts, out=starts[1:])
    items_sorted = order  # items_sorted[starts[v]:starts[v+1]] = items of v

    # centroids
    cent = np.zeros((m, dim), dtype=np.float64)
    np.add.at(cent, bindings, np.asarray(base, dtype=np.float64))
    nonempty = counts > 0
    cent[nonempty] /= counts[nonempty, None]
    # radii (exact)
    radii = np.zeros(m, dtype=np.float64)
    CHUNK = 4096
    for s0 in range(0, n, CHUNK):
        idx = np.arange(s0, min(s0 + CHUNK, n))
        d = ((np.asarray(base[idx], dtype=np.float64) - cent[bindings[idx]]) ** 2).sum(1)
        np.maximum.at(radii, bindings[idx], d)
    radii = np.sqrt(radii)

    # uniform samples per node: first `sample_s` items (stable order = deterministic)
    rng = np.random.default_rng(20261029)
    samp_node, samp_item = [], []
    for v in np.flatnonzero(nonempty):
        its = items_sorted[starts[v]:starts[v + 1]]
        if len(its) > sample_s:
            its = its[rng.choice(len(its), sample_s, replace=False)]
        for i in its:
            samp_node.append(v)
            samp_item.append(i)
    samp_node = np.asarray(samp_node)
    samp_item = np.asarray(samp_item)
    samp_vec = np.asarray(base[samp_item], dtype=np.float32)
    # sample positions per node for fast slicing
    sorder = np.argsort(samp_node, kind="stable")
    samp_node, samp_item = samp_node[sorder], samp_item[sorder]
    samp_vec = samp_vec[sorder]
    scounts = np.bincount(samp_node, minlength=m)
    sstarts = np.zeros(m + 1, dtype=np.int64)
    np.cumsum(scounts, out=sstarts[1:])

    print(f"[{name}] setup {time.time()-t0:.1f}s: n={n} nodes={m} "
          f"nonempty={nonempty.sum()} samples={len(samp_item)}", flush=True)

    rows = []
    for qi, (qidx, rec, anchor) in enumerate(wl):
        if rec != 1:
            continue  # non-recursive scopes are a single node; routing trivial
        ids = gt.get(qi)
        if not ids:
            continue
        scope_nodes = descendants(children, int(anchor))
        scope_nodes = np.sort(scope_nodes[nonempty[scope_nodes]])
        scope_items = int(counts[scope_nodes].sum())
        oracle_nodes = np.unique(bindings[ids])
        oracle_items = int(counts[oracle_nodes].sum())

        q = np.asarray(test[qidx], dtype=np.float64)
        cn = scope_nodes
        d_cent = ((cent[cn] - q) ** 2).sum(1) ** 0.5
        d_lb = d_cent - radii[cn]
        # sample vote: min distance over the node's samples (vectorized)
        d_samp = np.full(len(cn), np.inf)
        scope_mask = np.zeros(m, dtype=bool)
        scope_mask[cn] = True
        keep = scope_mask[samp_node]
        if keep.any():
            sv = samp_vec[keep]
            dists = np.sqrt(((sv - q.astype(np.float32)) ** 2).sum(1))
            np.minimum.at(d_samp, np.searchsorted(cn, samp_node[keep]), dists)

        oracle_mask = np.isin(cn, oracle_nodes)
        rec_out = {
            "query_index": qi, "scope_items": scope_items,
            "scope_nodes": len(cn), "oracle_nodes": len(oracle_nodes),
            "oracle_items": oracle_items,
            "oracle_item_savings": 1 - oracle_items / scope_items,
            "oracle_node_frac": len(oracle_nodes) / len(cn),
        }
        for scorer_name, scores in (("centroid", d_cent), ("lb", d_lb),
                                    ("samplevote", d_samp)):
            order_s = np.argsort(scores, kind="stable")
            # minimal prefix covering all oracle nodes
            seen = np.zeros(len(cn), dtype=bool)
            need = int(oracle_mask.sum())
            found = 0
            take = 0
            for j in order_s:
                take += 1
                if oracle_mask[j]:
                    found += 1
                if found == need:
                    break
            cover_items = int(counts[cn[order_s[:take]]].sum())
            rec_out[f"{scorer_name}_prefix_nodes"] = take
            rec_out[f"{scorer_name}_prefix_item_savings"] = 1 - cover_items / scope_items
            rec_out[f"{scorer_name}_prefix_node_frac"] = take / len(cn)
            # fixed budgets: top-10 nodes, top-1% nodes
            for bname, b in (("top10", 10),
                             ("top1pct", max(1, len(cn) // 100))):
                cov = oracle_mask[order_s[:b]].sum() / need
                rec_out[f"{scorer_name}_cov_{bname}"] = cov
        rows.append(rec_out)
        if len(rows) % 50 == 0:
            print(f"[{name}] {len(rows)} queries ({time.time()-t0:.0f}s)", flush=True)

    # aggregate
    out_rows = Path(f"{out_prefix}.csv")
    with open(out_rows, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    big = [r for r in rows if r["scope_items"] >= 10000]
    agg = {"dataset": name, "n_recursive_queries": len(rows),
           "n_ge10k": len(big), "elapsed_s": round(time.time() - t0, 1)}

    def agg_block(sub, key):
        key = f"{key}_" if key else ""
        return {
            f"{key}oracle_savings_median": float(np.median([r["oracle_item_savings"] for r in sub])),
            f"{key}oracle_savings_p10": float(np.percentile([r["oracle_item_savings"] for r in sub], 10)),
            f"{key}oracle_nodes_median": float(np.median([r["oracle_nodes"] for r in sub])),
            f"{key}scope_nodes_median": float(np.median([r["scope_nodes"] for r in sub])),
            **{f"{key}{s}_cov_top10": float(np.mean([r[f"{s}_cov_top10"] for r in sub]))
               for s in ("centroid", "lb", "samplevote")},
            **{f"{key}{s}_cov_top1pct": float(np.mean([r[f"{s}_cov_top1pct"] for r in sub]))
               for s in ("centroid", "lb", "samplevote")},
            **{f"{key}{s}_prefix_savings_median": float(np.median([r[f"{s}_prefix_item_savings"] for r in sub]))
               for s in ("centroid", "lb", "samplevote")},
        }

    agg.update(agg_block(rows, "all"))
    if big:
        agg.update(agg_block(big, "ge10k"))
    Path(f"{out_prefix}.json").write_text(json.dumps(agg, indent=2) + "\n")
    print(f"[{name}] wrote {out_prefix} ({time.time()-t0:.0f}s total)", flush=True)
    print(json.dumps(agg, indent=2), flush=True)


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
