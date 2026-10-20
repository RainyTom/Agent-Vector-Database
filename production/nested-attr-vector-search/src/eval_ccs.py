#!/usr/bin/env python3
"""Score a candidate on the CCS-shaped workload (Subtree or Exact).

Reads the fixture, workload, ground truth and candidate ids/timings, checks
feasibility and duplicates against the direct labels, and prints recall/qps.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


def load_tree(path):
    toks = path.read_text().split()
    nodes = int(toks[0])
    parent = np.full(nodes, -1, dtype=np.int64)
    children = [[] for _ in range(nodes)]
    it = iter(toks[1:])
    for _ in range(nodes):
        v = int(next(it)); p = int(next(it))
        parent[v] = p
        if p >= 0:
            children[p].append(v)
    return parent, children


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--workload", required=True)
    ap.add_argument("--gt", required=True)
    ap.add_argument("--ids", required=True)
    ap.add_argument("--timings", required=True)
    ap.add_argument("--query-seconds", type=float, required=True)
    ap.add_argument("--mode", choices=["subtree", "exact"], required=True)
    args = ap.parse_args()
    fix = Path(args.fixtures)

    parent, children = load_tree(fix / "tree.txt")
    n, dim, nq, k, nodes, words = map(int, (fix / "meta.txt").read_text().split())
    labels = np.fromfile(fix / "labels.bin", dtype=np.int32).reshape(n, 5)
    gt = np.fromfile(args.gt, dtype=np.int32).reshape(nq, k)
    ids = np.fromfile(args.ids, dtype=np.int32).reshape(nq, k)

    toks = Path(args.workload).read_text().split()
    nq2, k2 = int(toks[0]), int(toks[1])
    pos = 2
    qnodes = []
    for _ in range(nq2):
        w = int(toks[pos]); pos += 1
        qnodes.append([int(x) for x in toks[pos:pos + w]])
        pos += w

    hits = 0
    violations = 0
    empty = 0
    for qi in range(nq):
        if args.mode == "subtree":
            mark = np.zeros(nodes, dtype=bool)
            stack = list(qnodes[qi])
            while stack:
                v = stack.pop(); mark[v] = True; stack.extend(children[v])
            idx = np.where(labels >= 0, labels, nodes)
            ext = np.append(mark, False)
            feasible = ext[idx].any(axis=1)
        else:
            feasible = np.zeros(n, dtype=bool)
            for u in qnodes[qi]:
                for j in range(labels.shape[1]):
                    feasible |= labels[:, j] == u
        pred = ids[qi]
        valid = pred[pred >= 0]
        if len(set(valid.tolist())) != len(valid):
            violations += 1
        for x in valid:
            if x >= n or not feasible[x]:
                violations += 1
        if len(valid) == 0:
            empty += 1
        hits += len(set(valid.tolist()) & set(gt[qi].tolist()))

    recall = hits / (nq * k)
    qps = nq / args.query_seconds
    tim = np.fromfile(args.timings, dtype=np.float64)
    print(f"mode={args.mode} nq={nq} recall@10={recall:.4f} qps={qps:.1f} "
          f"p95_ms={np.percentile(tim,95):.4f} violations={violations} empty={empty}")


if __name__ == "__main__":
    main()
