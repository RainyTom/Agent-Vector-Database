#!/usr/bin/env python3
"""C1 gating experiment, part 2: hierarchical (top-down) routing.

The flat probe showed a large oracle upper bound but weak flat-summary coverage.
The G2 mechanism is top-down: at each node, score only its direct children by a
cheap summary and descend into the best m. Cost per level is the fan-out, not
the whole scope, and coarse comparisons are more discriminative.

Reported per query (recursive only):
  ceiling_recall = |GT ∩ items(visited)| / |GT|   (perfect per-node search)
  savings        = 1 - items(visited)/scope_items
  cost           = number of summary distance computations (centroid or sample)
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


def analyze(name, data_dir, gt_csv, dim, sample_s, keep_m, out_prefix):
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
            ids = [int(t) for t in r["result_ids"].replace(";", " ").split() if int(t) >= 0]
            gt[int(r["query_index"])] = ids

    counts = np.bincount(bindings, minlength=m)
    order = np.argsort(bindings, kind="stable")
    starts = np.zeros(m + 1, dtype=np.int64)
    np.cumsum(counts, out=starts[1:])

    cent = np.zeros((m, dim), dtype=np.float64)
    np.add.at(cent, bindings, np.asarray(base, dtype=np.float64))
    nonempty = counts > 0
    cent[nonempty] /= counts[nonempty, None]

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
    samp_node = samp_node[sorder]
    samp_vec = np.asarray(base[samp_item[sorder]], dtype=np.float32)
    scounts = np.bincount(samp_node, minlength=m)
    sstarts = np.zeros(m + 1, dtype=np.int64)
    np.cumsum(scounts, out=sstarts[1:])
    print(f"[{name}] setup {time.time()-t0:.0f}s samples={len(samp_item)}", flush=True)

    rows = []
    for qi, (qidx, rec, anchor) in enumerate(wl):
        if rec != 1:
            continue
        ids = gt.get(qi)
        if not ids:
            continue
        q = np.asarray(test[qidx], dtype=np.float64)
        qf = q.astype(np.float32)
        visited, cost = [], 0
        # BFS with per-level best-m pruning; anchor itself always visited
        level = [int(anchor)]
        while level:
            nxt = []
            for v in level:
                if counts[v] > 0:
                    visited.append(v)
                kids = [c for c in children[v] if counts[c] > 0]
                if not kids:
                    continue
                sc = np.empty(len(kids))
                sco = np.empty(len(kids))  # sample min distance
                for j, c in enumerate(kids):
                    sc[j] = np.sqrt(((cent[c] - q) ** 2).sum())
                    if sstarts[c + 1] > sstarts[c]:
                        sv = samp_vec[sstarts[c]:sstarts[c + 1]]
                        sco[j] = np.sqrt(((sv - qf) ** 2).sum(1)).min()
                    else:
                        sco[j] = np.inf
                cost += len(kids) * (1 + sample_s)
                # rank by sample score (better of the two summaries in part 1)
                k = min(keep_m, len(kids))
                keep = np.argsort(sco, kind="stable")[:k]
                nxt.extend(kids[j] for j in keep)
            level = nxt

        visited = np.unique(np.asarray(visited, dtype=np.int64))
        gt_set = set(ids)
        vis_items = set()
        for v in visited:
            vis_items.update(order[starts[v]:starts[v + 1]].tolist())
        ceil = len(gt_set & vis_items) / len(gt_set)
        scope_items = int(sum(counts[c] for c in desc(children, int(anchor)) if counts[c] > 0))
        rows.append({"query_index": qi, "scope_items": scope_items,
                     "visited_nodes": len(visited), "cost_dist": cost,
                     "ceiling_recall": ceil,
                     "savings": 1 - len(vis_items) / max(1, scope_items)})
        if len(rows) % 50 == 0:
            print(f"[{name}] {len(rows)} ({time.time()-t0:.0f}s)", flush=True)

    with open(f"{out_prefix}.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    def blk(sub, tag):
        return {
            f"{tag}n": len(sub),
            f"{tag}ceiling_recall_mean": float(np.mean([r["ceiling_recall"] for r in sub])),
            f"{tag}ceiling_recall_p10": float(np.percentile([r["ceiling_recall"] for r in sub], 10)),
            f"{tag}perfect_frac": float(np.mean([r["ceiling_recall"] == 1.0 for r in sub])),
            f"{tag}savings_median": float(np.median([r["savings"] for r in sub])),
            f"{tag}cost_dist_median": float(np.median([r["cost_dist"] for r in sub])),
            f"{tag}visited_nodes_median": float(np.median([r["visited_nodes"] for r in sub])),
        }
    big = [r for r in rows if r["scope_items"] >= 10000]
    agg = {"dataset": name, "keep_m": keep_m, "sample_s": sample_s,
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
    ap.add_argument("--keep-m", type=int, default=2)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    analyze(a.name, a.data, a.gt, a.dim, a.sample_s, a.keep_m, a.out)


if __name__ == "__main__":
    main()
