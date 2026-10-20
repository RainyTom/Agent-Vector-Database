#!/usr/bin/env python3
"""Convert five-slot CCS-shaped fixtures into the PathSeer/FAISS layout.

Outputs:
  load/load.bin                    u32 n, u32 d, n*d float32 (raw integer-valued
                                   vectors in [0,255], no normalization)
  query/query.bin                  u32 nq, u32 d, nq*d float32
  query/query_sub.bv               nq * ceil(n/8) bytes, bit i = object i is
                                   feasible for Subtree query q (LSB first,
                                   matching faiss IDSelectorBitmap)
  query/query_exact.bv             same for Exact queries
  golden/golden_sub_l2_10.bin      u32 nq, u32 k, nq*k int32 (0-based IDs)
  golden/golden_exact_l2_10.bin
  check.json                       popcount == oracle scope size, golden
                                   feasibility, bitmap construction timing

The converter also prints one line per check so a failure is visible without
reading the JSON.
"""

from __future__ import annotations

import argparse
import json
import time
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


def read_workload(path):
    toks = path.read_text().split()
    nq, k = int(toks[0]), int(toks[1])
    pos = 2
    out = []
    for _ in range(nq):
        w = int(toks[pos]); pos += 1
        out.append([int(x) for x in toks[pos:pos + w]])
        pos += w
    return out


def subtree_masks(qnodes, children, labels, nodes):
    n = labels.shape[0]
    masks = np.zeros((len(qnodes), n), dtype=bool)
    for qi, us in enumerate(qnodes):
        mark = np.zeros(nodes, dtype=bool)
        stack = list(us)
        while stack:
            v = stack.pop()
            mark[v] = True
            stack.extend(children[v])
        idx = np.where(labels >= 0, labels, nodes)
        ext = np.append(mark, False)
        masks[qi] = ext[idx].any(axis=1)
    return masks


def exact_masks(qnodes, labels):
    n = labels.shape[0]
    masks = np.zeros((len(qnodes), n), dtype=bool)
    for qi, us in enumerate(qnodes):
        for u in us:
            for j in range(labels.shape[1]):
                masks[qi] |= labels[:, j] == u
    return masks


def pack_bitmaps(masks):
    nq, n = masks.shape
    nbytes = (n + 7) // 8
    out = np.zeros((nq, nbytes), dtype=np.uint8)
    for qi in range(nq):
        out[qi] = np.packbits(masks[qi], bitorder="little")
    return out


def write_bin(path, arr):
    path.write_bytes(arr.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    fix = Path(args.fixtures)
    out = Path(args.out)
    for sub in ["load", "query", "golden"]:
        (out / sub).mkdir(parents=True, exist_ok=True)

    n, dim, nq, k, nodes, words = map(int, (fix / "meta.txt").read_text().split())
    base = np.fromfile(fix / "base.f32", dtype=np.float32).reshape(n, dim)
    queries = np.fromfile(fix / "test.f32", dtype=np.float32).reshape(nq, dim)
    labels = np.fromfile(fix / "labels.bin", dtype=np.int32).reshape(n, 5)
    parent, children = load_tree(fix / "tree.txt")
    sub_nodes = read_workload(fix / "workload_sub.txt")
    exact_nodes = read_workload(fix / "workload_exact.txt")
    gt_sub = np.fromfile(fix / "gt_sub.bin", dtype=np.int32).reshape(nq, k)
    gt_exact = np.fromfile(fix / "gt_exact.bin", dtype=np.int32).reshape(nq, k)

    # ---- vector files -----------------------------------------------------
    header = np.array([n, dim], dtype=np.uint32)
    with open(out / "load/load.bin", "wb") as f:
        f.write(header.tobytes())
        f.write(base.astype(np.float32).tobytes())
    qheader = np.array([nq, dim], dtype=np.uint32)
    with open(out / "query/query.bin", "wb") as f:
        f.write(qheader.tobytes())
        f.write(queries.astype(np.float32).tobytes())

    # ---- bitmaps ----------------------------------------------------------
    # (a) from labels, per query (what our evaluator does)
    t0 = time.perf_counter()
    sub_masks = subtree_masks(sub_nodes, children, labels, nodes)
    t_sub = time.perf_counter() - t0
    t0 = time.perf_counter()
    exact_masks_arr = exact_masks(exact_nodes, labels)
    t_exact = time.perf_counter() - t0

    # (b) precomputed per-node bitmaps + OR per query (a system would cache
    # these; this is the realistic construction path).
    order = []
    stack = [0]
    while stack:
        v = stack.pop()
        order.append(v)
        stack.extend(children[v])
    node_sub = np.zeros((nodes, n), dtype=bool)
    for j in range(labels.shape[1]):
        l = labels[:, j]
        ok = l >= 0
        node_sub[l[ok], np.nonzero(ok)[0]] = True
    node_exact = node_sub.copy()  # direct labels only, before propagation
    for v in reversed(order):
        if parent[v] >= 0:
            node_sub[parent[v]] |= node_sub[v]
    node_sub_bits = np.packbits(node_sub, axis=1, bitorder="little")
    node_exact_bits = np.packbits(node_exact, axis=1, bitorder="little")

    t0 = time.perf_counter()
    sub_bits = np.zeros((len(sub_nodes), (n + 7) // 8), dtype=np.uint8)
    for qi, us in enumerate(sub_nodes):
        sub_bits[qi] = np.bitwise_or.reduce(node_sub_bits[us], axis=0)
    t_sub_pre = time.perf_counter() - t0
    t0 = time.perf_counter()
    exact_bits = np.zeros((len(exact_nodes), (n + 7) // 8), dtype=np.uint8)
    for qi, us in enumerate(exact_nodes):
        exact_bits[qi] = np.bitwise_or.reduce(node_exact_bits[us], axis=0)
    t_exact_pre = time.perf_counter() - t0

    # sanity: precomputed == from-labels
    if not np.array_equal(sub_bits, pack_bitmaps(sub_masks)):
        raise SystemExit("subtree precomputed bitmaps differ from label masks")
    if not np.array_equal(exact_bits, pack_bitmaps(exact_masks_arr)):
        raise SystemExit("exact precomputed bitmaps differ from label masks")

    write_bin(out / "query/query_sub.bv", sub_bits)
    write_bin(out / "query/query_exact.bv", exact_bits)

    # ACORN build metadata: one primary direct label per vector. This is a
    # partial, single-label encoding of the hierarchy; it is not the full
    # multi-label structure.
    primary = labels[:, 0].astype(np.int64)
    with open(out / "load/load.meta", "w") as f:
        for v in primary:
            f.write(f"{int(v)}\n")

    # ---- golden -----------------------------------------------------------
    for name, gt in [("golden_sub_l2_10.bin", gt_sub), ("golden_exact_l2_10.bin", gt_exact)]:
        with open(out / "golden" / name, "wb") as f:
            f.write(np.array([nq, k], dtype=np.uint32).tobytes())
            f.write(gt.astype(np.int32).tobytes())

    # ---- checks -----------------------------------------------------------
    nbytes = (n + 7) // 8
    checks = {
        "n": n, "dim": dim, "nq": nq, "k": k, "nbytes": nbytes,
        "sub_bitmap_build_s_from_labels": t_sub,
        "exact_bitmap_build_s_from_labels": t_exact,
        "sub_bitmap_build_s_precomputed": t_sub_pre,
        "exact_bitmap_build_s_precomputed": t_exact_pre,
        "sub": {}, "exact": {},
    }
    failures = []
    for mode, masks, bits, gt in [
        ("sub", sub_masks, sub_bits, gt_sub),
        ("exact", exact_masks_arr, exact_bits, gt_exact),
    ]:
        pops = np.unpackbits(bits, axis=1, count=n, bitorder="little").sum(axis=1)
        scope = masks.sum(axis=1)
        pop_ok = bool(np.array_equal(pops, scope))
        if not pop_ok:
            failures.append(f"{mode}: popcount != scope size")
        # golden IDs are zero-based, in range, and feasible
        id_min = int(gt.min()); id_max = int(gt.max())
        id_ok = id_min >= 0 and id_max < n
        if not id_ok:
            failures.append(f"{mode}: golden id out of range [{id_min},{id_max}]")
        feas_ok = True
        for qi in range(nq):
            for x in gt[qi]:
                if x < 0 or x >= n or not masks[qi, x]:
                    feas_ok = False
                    break
            if not feas_ok:
                break
        if not feas_ok:
            failures.append(f"{mode}: golden contains infeasible id")
        checks[mode] = {
            "scope_size_min": int(scope.min()),
            "scope_size_max": int(scope.max()),
            "popcount_matches": pop_ok,
            "golden_id_range": [id_min, id_max],
            "golden_feasible": feas_ok,
            "queries_checked": nq,
        }
        print(f"[{mode}] scope min/max {scope.min()}/{scope.max()} "
              f"popcount_ok={pop_ok} golden_ok={feas_ok}")
    checks["failures"] = failures
    (out / "check.json").write_text(json.dumps(checks, indent=2))
    if failures:
        print("CHECK FAILURES:", failures)
        raise SystemExit(1)
    print(f"bitmap build from labels: sub {t_sub:.3f}s, exact {t_exact:.3f}s")
    print(f"bitmap build precomputed: sub {t_sub_pre:.3f}s, exact {t_exact_pre:.3f}s "
          f"({t_sub_pre / nq * 1e6:.1f} / {t_exact_pre / nq * 1e6:.1f} us/query)")
    print("conversion OK", out)


if __name__ == "__main__":
    main()
