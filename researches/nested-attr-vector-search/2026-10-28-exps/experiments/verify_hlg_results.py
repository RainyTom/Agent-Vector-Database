#!/usr/bin/env python3
"""Independent verification of hlg_tool bench CSV output.

Re-derives, without any hlg_tool code or intermediate state:
  * the scope of every query from wiki_tree.txt + wiki_bindings.txt
    (DFS-preorder interval layout, recursive = subtree, non-recursive = direct bindings);
  * scope cardinality against the frozen GT file;
  * hit / validity / duplicate counts from the emitted result_ids only;
  * macro (query-average) and global (pair-pooled) recall.

Usage:
  verify_hlg_results.py --data DIR --csv BENCH.csv --gt reference.json [--k 10]
                        [--ns wiki] [--expect-scope]

Exit code 0 always; mismatches are reported on stdout.
"""
import argparse
import csv
import json
import sys

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


def layout(parent, node_of_entry):
    """DFS preorder; S(u) = [lo[u], hi[u]) in entry-ID space, L(u) the first nloc entries."""
    m = parent.shape[0]
    ch = [[] for _ in range(m)]
    for v in range(1, m):
        if parent[v] >= 0:
            ch[parent[v]].append(v)
    nloc = np.bincount(node_of_entry, minlength=m).astype(np.int64)
    lo = np.zeros(m, dtype=np.int64)
    hi = np.zeros(m, dtype=np.int64)
    cursor = 0
    lo[0] = 0
    cursor += nloc[0]
    stack = [(0, 0)]
    while stack:
        u, ci = stack[-1]
        if ci < len(ch[u]):
            c = ch[u][ci]
            stack[-1] = (u, ci + 1)
            lo[c] = cursor
            cursor += nloc[c]
            stack.append((c, 0))
        else:
            hi[u] = cursor
            stack.pop()
    return lo, hi, nloc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--csv", required=True)
    ap.add_argument("--gt", required=True)
    ap.add_argument("--k", type=int, default=10)
    ap.add_argument("--ns", default="wiki")
    ap.add_argument("--expect-scope", action="store_true",
                    help="fail loudly if our scope cardinality differs from the frozen GT")
    args = ap.parse_args()

    parent = load_tree(f"{args.data}/{args.ns}_tree.txt")
    node_of_entry = load_bindings(f"{args.data}/{args.ns}_bindings.txt")
    entries = node_of_entry.shape[0]
    lo, hi, nloc = layout(parent, node_of_entry)
    # entry-ID -> base vector id, and the inverse (same construction as hlg_tool)
    orig = np.full(entries, -1, dtype=np.int64)
    slot = lo.copy()
    for i in range(entries):
        v = node_of_entry[i]
        orig[slot[v]] = i
        slot[v] += 1
    eid = np.full(entries, -1, dtype=np.int64)
    eid[orig] = np.arange(entries, dtype=np.int64)

    ref = json.load(open(args.gt))["queries"]
    if len(ref) != len({r["query_index"] for r in ref}):
        print("WARN: duplicate query_index in GT")

    rows = {}
    with open(args.csv) as f:
        for r in csv.DictReader(f):
            key = (r["mode"], int(r["ef"]), r["query_type"])
            rows.setdefault(key, []).append(r)

    print("mode,ef,query_type,queries,macro_recall,global_pair_recall,zero_results,"
          "out_of_scope,duplicates,scope_card_mismatch,mean_search_us")
    for (mode, ef, qtype), rs in sorted(rows.items()):
        rs.sort(key=lambda r: int(r["query_index"]))
        macro = 0.0
        ghit = 0
        gtot = 0
        zero = outside = dup = card_bad = 0
        lat = 0.0
        by_idx = {int(g["query_index"]): g for g in ref}
        for r in rs:
            g = by_idx[int(r["query_index"])]
            qi = int(r["query_index"])
            qtype = r["query_type"]
            anchor = int(r["anchor"])
            rec = qtype == "recursive"
            sb, se = (lo[anchor], hi[anchor]) if rec else (lo[anchor], lo[anchor] + nloc[anchor])
            card = int(se - sb)
            if card != int(g["scope_card"]):
                card_bad += 1
                if args.expect_scope and card_bad <= 5:
                    print(f"SCOPE-MISMATCH qi={qi} ours={card} ref={g['scope_card']}",
                          file=sys.stderr)
            ids = [int(x) for x in r["result_ids"].split()] if r["result_ids"].strip() else []
            if not ids:
                zero += 1
            seen = set()
            valid = 0
            hit = 0
            gts = set(g["gt_ids"])
            for x in ids:
                if x in seen:
                    dup += 1
                    continue
                seen.add(x)
                if x < 0 or x >= entries or not (sb <= eid[x] < se):
                    outside += 1
                    continue
                valid += 1
                if x in gts:
                    hit += 1
            macro += hit / args.k
            ghit += hit
            gtot += len(gts)
            lat += float(r["search_us"])
            _ = valid
        n = len(rs)
        print(f"{mode},{ef},{qtype},{n},{macro/n:.6f},{ghit/gtot if gtot else 0:.6f},{zero},"
              f"{outside},{dup},{card_bad},{lat/n:.1f}")


if __name__ == "__main__":
    main()
