#!/usr/bin/env python3
"""Information-value diagnostic, NOT an online ANN or new index benchmark."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import time

for key in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS"):
    os.environ[key] = "1"
import numpy as np
import faiss

ROOT = Path(__file__).resolve().parents[5]
EXP = Path(__file__).resolve().parents[1]
OLD = EXP.parent / "2026-10-29-exps"
SEED = 20261030
K = 10


def dump(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2, allow_nan=False) + "\n")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def csv_out(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def load_tree(data, n):
    lines = (data / "tree.txt").read_text().splitlines()
    m = int(lines[0])
    parent = np.full(m, -2, dtype=np.int64)
    for line in lines[1:]:
        i, p = map(int, line.split())
        parent[i] = p
    if (parent == -2).any() or (parent < 0).sum() != 1:
        raise ValueError("malformed tree")
    owner = np.loadtxt(data / "bindings.txt", dtype=np.int64, max_rows=n, ndmin=1)
    if len(owner) != n or owner.min() < 0 or owner.max() >= m:
        raise ValueError("bad direct bindings")
    sizes = np.bincount(owner, minlength=m).astype(np.int64)
    depth = np.zeros(m, dtype=np.int64)
    for v in range(m):
        seen = set()
        u = v
        while parent[u] >= 0:
            if u in seen:
                raise ValueError("tree cycle")
            seen.add(u)
            depth[v] += 1
            u = int(parent[u])
    for v in np.argsort(-depth):
        if parent[v] >= 0:
            sizes[parent[v]] += sizes[v]
    root = int(np.flatnonzero(parent < 0)[0])
    chosen = [("root", root)]
    possible = [v for v in range(m) if v != root and sizes[v] > 0]
    for name, target in (("broad", .25 * n), ("narrow", .05 * n)):
        if possible:
            chosen.append((name, min(possible, key=lambda v: (abs(sizes[v] - target), v))))
    scopes = []
    for name, anchor in chosen:
        included = np.zeros(m, dtype=bool)
        for v in range(m):
            u = v
            while u >= 0:
                if u == anchor:
                    included[v] = True
                    break
                u = int(parent[u])
        mask = included[owner]
        assert int(mask.sum()) == int(sizes[anchor])
        scopes.append((name, anchor, mask))
    return owner, scopes


def attribute_pair(x, blocks, owner, axes, seed):
    n = len(x)
    corr = np.zeros((n, 2), dtype=np.uint8)
    shuffled = np.zeros_like(corr)
    rng = np.random.default_rng(seed)
    movable = 0
    for ids in blocks:
        for node in np.unique(owner[ids]):
            cell = ids[owner[ids] == node]
            if len(cell) > 1:
                movable += len(cell)
            for j, axis in enumerate(axes[:2]):
                ordered = cell[np.argsort(x[cell, axis], kind="stable")]
                corr[ordered, j] = np.minimum(7, np.arange(len(cell)) * 8 // len(cell))
            shuffled[cell] = corr[rng.permutation(cell)]
    return corr, shuffled, {"movable_fraction": movable / n,
                           "tuple_changed_fraction": float(np.any(corr != shuffled, axis=1).mean())}


def predicate(attrs, name):
    a, b = attrs[:, 0], attrs[:, 1]
    if name == "scope_only":
        return np.ones(len(a), dtype=bool)
    if name == "eq7":
        return a == 7
    if name == "eq7_and_hi6":
        return (a == 7) & (b >= 6)
    if name == "eq7_or_hi6":
        return (a == 7) | (b >= 6)
    raise ValueError(name)


def orders_for(z, blocks, r):
    orders, values = [], []
    for ids in blocks:
        order = ids[np.argsort(z[ids, :r], axis=0, kind="stable").T].astype(np.int32)
        values.append(np.ascontiguousarray(z[order, np.arange(r)[:, None]]))
        orders.append(np.ascontiguousarray(order))
    return orders, values


def extrema_from_index(order, values, mask):
    r, b = order.shape
    lo, hi = np.empty(r), np.empty(r)
    probes = 0
    for j in range(r):
        p = 0
        while p < b:
            probes += 1
            if mask[order[j, p]]:
                break
            p += 1
        if p == b:
            assert j == 0
            return None, probes, 0
        t = b - 1
        while t >= 0:
            probes += 1
            if mask[order[j, t]]:
                break
            t -= 1
        lo[j], hi[j] = values[j, p], values[j, t]
    return (lo, hi), probes, 2 * r


def gap_from_index(order, values, mask, query):
    r, b = order.shape
    delta = np.empty(r)
    probes = comparisons = value_reads = 0
    for j in range(r):
        left, right = 0, b
        while left < right:
            mid = (left + right) // 2
            comparisons += 1
            value_reads += 1
            if values[j, mid] < query[j]:
                left = mid + 1
            else:
                right = mid
        found = []
        for start, step in ((left - 1, -1), (left, 1)):
            p = start
            while 0 <= p < b:
                probes += 1
                if mask[order[j, p]]:
                    found.append(abs(values[j, p] - query[j]))
                    value_reads += 1
                    break
                p += step
        if not found:
            assert j == 0
            return None, probes, comparisons, value_reads
        delta[j] = min(found)
    return delta, probes, comparisons, value_reads


def guarded(raw, beta, dim):
    # Conservative diagnostic guard; no arbitrary-input floating-point theorem.
    u = np.finfo(np.float32).eps / 2
    t = (8 * dim + 32) * u
    gamma = t / (1 - t)
    return np.maximum(0, np.nextafter(raw / beta * (1 - gamma), -np.inf))


def self_check(binary, work):
    folder = work / "smoke"
    folder.mkdir()
    x = np.array([[0, 0], [3, 4]], dtype=np.float32)
    x.tofile(folder / "x.f32")
    x.tofile(folder / "q.f32")
    cmd = [str(binary), "--base", str(folder / "x.f32"), "--queries", str(folder / "q.f32"),
           "--out", str(folder / "d.f32"), "--n", "2", "--nq", "2", "--dim", "2"]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True)
    got = np.fromfile(folder / "d.f32", np.float32).reshape(2, 2)
    assert np.array_equal(got, [[0, 25], [25, 0]])
    z = np.array([[-1., 1.], [1., -1.], [2., 3.]])
    ids, vals = orders_for(z, [np.arange(3)], 2)
    for mask in (np.array([1, 1, 0], bool), np.array([0, 1, 0], bool), np.zeros(3, bool)):
        ex, _, _ = extrema_from_index(ids[0], vals[0], mask)
        gap, _, _, _ = gap_from_index(ids[0], vals[0], mask, np.zeros(2))
        if mask.any():
            assert np.array_equal(ex[0], z[mask].min(axis=0))
            assert np.array_equal(ex[1], z[mask].max(axis=0))
            assert np.array_equal(gap, np.abs(z[mask]).min(axis=0))
        else:
            assert ex is None and gap is None
    same_witness = np.array([[0., 1.], [1., 0.]])
    assert float(np.square(np.abs(same_witness).min(axis=0)).sum()) == 0
    assert float(np.square(same_witness).sum(axis=1).min()) == 1
    return {"distance_oracle": json.loads(out.stdout), "expected_matrix": got.tolist(),
            "indexed_extrema_and_gap_match_enumeration": True,
            "joint_witness_relaxation_gap": 1.0}


def run_dataset(name, data, n, dim, binary, work, query_rows, cert_rows, cost_rows, provenance):
    qcount = 8
    x = np.fromfile(data / "base.f32", np.float32, count=n * dim).reshape(n, dim)
    q = np.fromfile(data / "test.f32", np.float32, count=qcount * dim).reshape(qcount, dim)
    if not np.isfinite(x).all() or not np.isfinite(q).all():
        raise ValueError("nonfinite vectors")
    owner, scopes = load_tree(data, n)
    basework = work / name
    basework.mkdir()
    distfile = basework / "distance.f32"
    cmd = [str(binary), "--base", str(data / "base.f32"), "--queries", str(data / "test.f32"),
           "--out", str(distfile), "--n", str(n), "--nq", str(qcount), "--dim", str(dim)]
    oracle = subprocess.run(cmd, capture_output=True, text=True, check=True)
    dist = np.fromfile(distfile, np.float32).reshape(qcount, n)
    xd = x.astype(np.float64)
    center = xd.mean(axis=0)
    centered = xd - center
    variance_axes = np.argsort(-np.var(xd, axis=0), kind="stable")
    t0 = time.perf_counter()
    eig, vectors = np.linalg.eigh(centered.T @ centered / n)
    u = vectors[:, ::-1].T.copy()
    gram = u @ u.T
    beta_pca = max(1., float(np.abs(gram).sum(axis=1).max())) * (1 + 32 * np.finfo(float).eps * dim)
    pca_z = centered @ u.T
    pca_q = (q.astype(np.float64) - center) @ u.T
    pca_s = time.perf_counter() - t0
    bases = [("coordinate", xd[:, variance_axes], q.astype(float)[:, variance_axes], 1., 0.),
             ("pca", pca_z, pca_q, beta_pca, pca_s)]
    source = {"dataset": name, "input": str(data), "n": n, "dim": dim,
              "query_ids": list(range(qcount)), "distance_oracle_command": cmd,
              "distance_oracle": json.loads(oracle.stdout), "pca_build_s": pca_s,
              "pca_beta_upper": beta_pca,
              "pca64_variance_fraction": float(np.maximum(eig[-min(64, dim):], 0).sum() / np.maximum(eig, 0).sum()),
              "input_sha256": {f: digest(data / f) for f in ("base.f32", "test.f32", "tree.txt", "bindings.txt")},
              "scopes": [{"name": s, "anchor": a, "items": int(mask.sum())} for s, a, mask in scopes],
              "geometries": []}
    for clusters in (32, 96):
        start = time.perf_counter()
        km = faiss.Kmeans(dim, clusters, niter=10, seed=SEED, verbose=False,
                          min_points_per_centroid=1, max_points_per_centroid=n)
        km.train(x)
        _, labels = km.index.search(x, 1)  # centroid assignment only, not retrieval ANN
        blocks = [np.flatnonzero(labels[:, 0] == b) for b in range(clusters)]
        blocks = [b for b in blocks if len(b)]
        corr, shuffled, shuffle_stats = attribute_pair(x, blocks, owner, variance_axes, SEED + clusters)
        geo = {"clusters": clusters, "nonempty_blocks": len(blocks),
               "block_sizes": [len(b) for b in blocks], "kmeans_s": time.perf_counter() - start,
               **shuffle_stats}
        source["geometries"].append(geo)
        for basis, z, zq, beta, basis_s in bases:
            r_index = min(64, dim)
            ranks = sorted(set((16, 64, dim)))
            start = time.perf_counter()
            orders, sorted_values = orders_for(z, blocks, r_index)
            sort_s = time.perf_counter() - start
            storage = sum(a.nbytes + b.nbytes for a, b in zip(orders, sorted_values))
            full_lo = np.array([z[ids].min(axis=0) for ids in blocks])
            full_hi = np.array([z[ids].max(axis=0) for ids in blocks])
            for scope_name, anchor, scope in scopes:
                scope_tau = [float(np.partition(dist[j, scope], K - 1)[K - 1])
                             if int(scope.sum()) >= K else math.inf for j in range(qcount)]
                masks = {}
                for control, attrs in (("correlated", corr), ("shuffled", shuffled)):
                    for pred in ("scope_only", "eq7", "eq7_and_hi6", "eq7_or_hi6"):
                        begin = time.perf_counter()
                        mask = scope & predicate(attrs, pred)
                        predicate_s = time.perf_counter() - begin
                        selected = [ids[mask[ids]] for ids in blocks]
                        counts = np.array([len(a) for a in selected], dtype=np.int64)
                        masks[(control, pred)] = counts
                        if control == "shuffled":
                            assert np.array_equal(counts, masks[("correlated", pred)])
                        cond_lo = np.full_like(full_lo, np.nan)
                        cond_hi = np.full_like(full_hi, np.nan)
                        for b, ids in enumerate(selected):
                            if len(ids):
                                cond_lo[b], cond_hi[b] = z[ids].min(axis=0), z[ids].max(axis=0)
                        begin = time.perf_counter()
                        ex_probes = ex_values = 0
                        for b, (order, values) in enumerate(zip(orders, sorted_values)):
                            ex, probes, reads = extrema_from_index(order, values, mask)
                            ex_probes += probes
                            ex_values += reads
                            if counts[b]:
                                assert ex is not None
                                assert np.array_equal(ex[0], cond_lo[b, :r_index])
                                assert np.array_equal(ex[1], cond_hi[b, :r_index])
                            else:
                                assert ex is None
                        ex_s = time.perf_counter() - begin
                        common = {"dataset": name, "clusters": clusters, "basis": basis,
                                  "scope": scope_name, "anchor": anchor, "predicate": pred, "control": control}
                        cert_rows.append({**common, "eligible": int(counts.sum()),
                                          "nonempty_blocks": int((counts > 0).sum()),
                                          "singleton_blocks": int((counts == 1).sum()),
                                          "predicate_item_evaluations": n,
                                          "attribute_comparisons": 0 if pred == "scope_only" else n * (1 if pred == "eq7" else 2),
                                          "scope_mask_positions": n, "predicate_python_s": predicate_s,
                                          "index_r": r_index, "sorted_arrays_bytes": storage,
                                          "basis_matrix64_bytes": r_index * dim * 8 if basis == "pca" else r_index * 8,
                                          "block_assignment_bytes": n * 4,
                                          "sort_build_python_s": sort_s, "projection_build_s": basis_s,
                                          "extrema_membership_probes": ex_probes,
                                          "extrema_coordinate_reads": ex_values,
                                          "extrema_python_s": ex_s,
                                          "logical_extrema_read_bytes": ex_probes * 5 + ex_values * 8})
                        for j in range(qcount):
                            eligible = int(counts.sum())
                            tau = float(np.partition(dist[j, mask], K - 1)[K - 1]) if eligible >= K else math.inf
                            whole_delta = np.maximum(np.maximum(full_lo - zq[j], zq[j] - full_hi), 0)
                            cond_delta = np.maximum(np.maximum(cond_lo - zq[j], zq[j] - cond_hi), 0)
                            gap_delta = np.full_like(cond_delta, np.nan)
                            true_min = np.full(len(blocks), np.inf)
                            for b, ids in enumerate(selected):
                                if len(ids):
                                    gap_delta[b] = np.abs(z[ids] - zq[j]).min(axis=0)
                                    true_min[b] = float(dist[j, ids].min())
                            nonempty = counts > 0
                            if j < 2:
                                begin = time.perf_counter()
                                probes = comparisons = values_read = 0
                                for b, (order, values) in enumerate(zip(orders, sorted_values)):
                                    gap, p, c, v = gap_from_index(order, values, mask, zq[j, :r_index])
                                    probes += p
                                    comparisons += c
                                    values_read += v
                                    if counts[b]:
                                        assert gap is not None and np.array_equal(gap, gap_delta[b, :r_index])
                                    else:
                                        assert gap is None
                                cost_rows.append({**common, "qidx": j, "r": r_index,
                                                  "membership_probes": probes, "binary_comparisons": comparisons,
                                                  "coordinate_reads": values_read,
                                                  "logical_read_bytes": probes * 5 + values_read * 8,
                                                  "gap_python_s": time.perf_counter() - begin,
                                                  "full_eligible_vector_bytes": eligible * dim * 4,
                                                  "query_projection_multiply_adds": r_index * dim if basis == "pca" else 0})
                            for r in ranks:
                                lb0 = guarded(np.square(whole_delta[:, :r]).sum(axis=1), beta, dim)
                                lb1 = guarded(np.square(cond_delta[:, :r]).sum(axis=1), beta, dim)
                                lb2 = guarded(np.square(gap_delta[:, :r]).sum(axis=1), beta, dim)
                                if np.any(lb1[nonempty] + 1e-12 < lb0[nonempty]) or np.any(lb2[nonempty] + 1e-12 < lb1[nonempty]):
                                    raise AssertionError("bound ordering")
                                violations = int(np.sum(lb2[nonempty] > true_min[nonempty]))
                                if violations:
                                    raise AssertionError(f"FP32 minimum guard failure: {common} q={j} r={r}")
                                for mode, threshold in (("filtered_topk", tau), ("scope_radius", scope_tau[j])):
                                    skip0 = nonempty & (lb0 > threshold)
                                    skip1 = nonempty & (lb1 > threshold)
                                    skip2 = nonempty & (lb2 > threshold)
                                    exact_skip = nonempty & (true_min > threshold)
                                    false_pruned = int(np.sum(skip2 & ~exact_skip))
                                    assert false_pruned == 0
                                    row = {**common, "qidx": j, "r": r, "threshold_mode": mode,
                                           "tau": threshold if math.isfinite(threshold) else "inf",
                                           "scope_items": int(scope.sum()), "eligible": eligible,
                                           "empty_blocks": int((~nonempty).sum()),
                                           "nonempty_blocks": int(nonempty.sum()),
                                           "singleton_blocks": int((counts == 1).sum()),
                                           "whole_skip_blocks": int(skip0.sum()), "conditional_skip_blocks": int(skip1.sum()),
                                           "gap_skip_blocks": int(skip2.sum()), "exact_oracle_skip_blocks": int(exact_skip.sum()),
                                           "whole_skip_items": int(counts[skip0].sum()),
                                           "conditional_skip_items": int(counts[skip1].sum()),
                                           "gap_skip_items": int(counts[skip2].sum()),
                                           "exact_oracle_skip_items": int(counts[exact_skip].sum()),
                                           "box_extra_items": int(counts[skip1 & ~skip0].sum()),
                                           "gap_extra_over_box_items": int(counts[skip2 & ~skip1].sum()),
                                           "gap_skip_singleton_items": int(counts[skip2 & (counts == 1)].sum()),
                                           "gap_skip_multi_items": int(counts[skip2 & (counts > 1)].sum()),
                                           "false_pruned_blocks": false_pruned,
                                           "guard_violations": violations}
                                    query_rows.append(row)
            print(f"{name} K={clusters} basis={basis} completed", flush=True)
    provenance.append(source)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    args = ap.parse_args()
    if args.out.exists() or args.work.exists():
        raise ValueError("fresh output/work directories required")
    args.out.mkdir(parents=True)
    args.work.mkdir(parents=True)
    faiss.omp_set_num_threads(1)
    checks = self_check(args.binary.resolve(), args.work)
    query_rows, cert_rows, cost_rows, provenance = [], [], [], []
    datasets = [
        ("wiki-root3626", OLD / "work/dynamic_triad/wiki-topology/wiki-topology-s20261029-q456-f7046c642b34/root-3626/native", 11869, 1024),
        ("sift12000", OLD / "work/dynamic_triad/crossover/datasets/sift-n12000-r1000-q64-d3-s20261029", 12000, 128)]
    started = time.perf_counter()
    for name, data, n, dim in datasets:
        run_dataset(name, data, n, dim, args.binary.resolve(), args.work, query_rows, cert_rows, cost_rows, provenance)
    csv_out(args.out / "queries.csv", query_rows)
    csv_out(args.out / "certificates.csv", cert_rows)
    csv_out(args.out / "indexed-costs.csv", cost_rows)
    dump(args.out / "provenance.json", {"seed": SEED, "k": K, "numpy": np.__version__,
                                       "faiss": faiss.__version__, "binary_sha256": digest(args.binary),
                                       "script_sha256": digest(Path(__file__)), "datasets": provenance,
                                       "wall_s": time.perf_counter() - started, "smoke": checks,
                                       "no_ann_index_or_online_candidate_prototype": True,
                                       "timing_limit": "Python certificate times are not C++ ANN speed comparisons"})
    dump(args.out / "audit.json", {"query_rows": len(query_rows), "certificate_rows": len(cert_rows),
                                  "indexed_cost_rows": len(cost_rows), "same_cardinality_pairs": True,
                                  "bound_order_and_observed_FP32_safety": True,
                                  "indexed_matches_enumerated_oracle": True,
                                  "errors": []})
    print(json.dumps({"out": str(args.out), "query_rows": len(query_rows), "wall_s": time.perf_counter() - started}))


if __name__ == "__main__":
    main()
