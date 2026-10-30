#!/usr/bin/env python3
"""Offline joint-information attribution; no online index or algorithm speed claim."""
from __future__ import annotations
import argparse
import csv
import gzip
import io
import json
import math
import os
from pathlib import Path
import time

for key in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS"):
    os.environ[key] = "1"
import numpy as np
import faiss
import conditional_geometry_probe as old

EXP = Path(__file__).resolve().parents[1]
REFERENCE = EXP / "data/conditional-geometry-run2"
FIELDS = ["dataset", "clusters", "basis", "scope", "anchor", "predicate", "control", "qidx",
          "family", "r", "g", "order", "threshold_mode", "tau", "eligible", "nonempty_blocks",
          "singleton_blocks", "whole_skip_items", "conditional_skip_items", "filter_extra_items",
          "joint_extra_over_g1_items", "conditional_multi_skip_items", "raw_oracle_skip_items",
          "false_pruned_blocks", "eligible_min_reduction_values", "whole_min_reduction_values"]


def group_values(squared, perm, g):
    r = len(perm)
    return squared[:, perm].reshape(len(squared), r // g, g).sum(axis=2)


def block_minima(values, blocks):
    result = np.full((len(blocks), values.shape[1]), np.inf)
    for b, ids in enumerate(blocks):
        if len(ids):
            result[b] = values[ids].min(axis=0)
    return result.sum(axis=1)


def smoke():
    witness = np.array([[0., 1.], [1., 0.]])
    sq = witness ** 2
    independent = float(group_values(sq, np.arange(2), 1).min(axis=0).sum())
    joint = float(group_values(sq, np.arange(2), 2).min(axis=0).sum())
    assert independent == 0 and joint == 1
    rng = np.random.default_rng(old.SEED)
    points, q = rng.normal(size=(7, 8)), rng.normal(size=8)
    squared = (points - q) ** 2
    observed = []
    for g in (1, 2, 4, 8):
        lb = float(group_values(squared, np.arange(8), g).min(axis=0).sum())
        observed.append(lb)
    assert all(a <= b + 1e-12 for a, b in zip(observed, observed[1:]))
    assert np.isclose(observed[-1], squared.sum(axis=1).min())
    radial = []
    for r in (0, 2, 8):
        tailx = np.linalg.norm(points[:, r:], axis=1)
        tailq = np.linalg.norm(q[r:])
        lifted = squared[:, :r].sum(axis=1) + (tailx - tailq) ** 2
        assert np.all(lifted <= squared.sum(axis=1) + 1e-12)
        radial.append(float(lifted.min()))
    return {"different_witness_g1": independent, "same_witness_g2": joint,
            "nested_group_minima": observed, "radial_lift_minima": radial}


def reference_key(row):
    return tuple(str(row[k]) for k in ("dataset", "clusters", "basis", "scope", "predicate",
                                       "control", "qidx", "r", "threshold_mode"))


def run(args, checks):
    n, dim = (11869, 1024) if args.dataset == "wiki-root3626" else (12000, 128)
    prior = json.loads((REFERENCE / "provenance.json").read_text())
    source = next(s for s in prior["datasets"] if s["dataset"] == args.dataset)
    data = Path(source["input"])
    for f, expected in source["input_sha256"].items():
        if old.digest(data / f) != expected:
            raise ValueError(f"input changed: {f}")
    ref = {}
    with (REFERENCE / "queries.csv").open() as f:
        for row in csv.DictReader(f):
            if row["dataset"] == args.dataset:
                ref[reference_key(row)] = row
    distance_file = EXP / "work/conditional-geometry/run2" / args.dataset / "distance.f32"
    dist = np.fromfile(distance_file, np.float32).reshape(8, n)
    if not np.isfinite(dist).all() or (dist < 0).any():
        raise ValueError("invalid reused raw distance oracle")
    x = np.fromfile(data / "base.f32", np.float32, count=n * dim).reshape(n, dim)
    q = np.fromfile(data / "test.f32", np.float32, count=8 * dim).reshape(8, dim)
    owner, scopes = old.load_tree(data, n)
    xd = x.astype(float)
    center = xd.mean(axis=0)
    centered = xd - center
    axes = np.argsort(-np.var(xd, axis=0), kind="stable")
    eig, vectors = np.linalg.eigh(centered.T @ centered / n)
    u = vectors[:, ::-1].T.copy()
    gram = u @ u.T
    beta = max(1., float(np.abs(gram).sum(axis=1).max())) * (1 + 32 * np.finfo(float).eps * dim)
    bases = [("coordinate", centered[:, axes], (q.astype(float) - center)[:, axes], 1.),
             ("pca", centered @ u.T, (q.astype(float) - center) @ u.T, beta)]
    ranks = sorted(set(r for r in (16, 64, 256, dim) if r <= dim))
    faiss.omp_set_num_threads(1)
    row_count = g1_checks = bound_checks = 0
    max_bound_excess = 0.
    started = time.perf_counter()
    audit_geometries = []
    with (args.out / "queries.csv.gz").open("xb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as gz:
            with io.TextIOWrapper(gz, encoding="utf-8", newline="") as text:
                writer = csv.DictWriter(text, fieldnames=FIELDS)
                writer.writeheader()
                for clusters in (32, 96):
                    km = faiss.Kmeans(dim, clusters, niter=10, seed=old.SEED, verbose=False,
                                      min_points_per_centroid=1, max_points_per_centroid=n)
                    km.train(x)
                    _, labels = km.index.search(x, 1)  # only kmeans centroid assignment
                    blocks = [np.flatnonzero(labels[:, 0] == b) for b in range(clusters)]
                    blocks = [ids for ids in blocks if len(ids)]
                    corr, shuffled, stats = old.attribute_pair(x, blocks, owner, axes, old.SEED + clusters)
                    expected_geo = next(g for g in source["geometries"] if g["clusters"] == clusters)
                    if [len(ids) for ids in blocks] != expected_geo["block_sizes"]:
                        raise AssertionError("kmeans scenario changed")
                    scenarios = []
                    for scope_name, anchor, scope in scopes:
                        paired_counts = {}
                        for control, attrs in (("correlated", corr), ("shuffled", shuffled)):
                            for pred in ("scope_only", "eq7", "eq7_and_hi6", "eq7_or_hi6"):
                                mask = scope & old.predicate(attrs, pred)
                                selected = [ids[mask[ids]] for ids in blocks]
                                counts = np.array([len(ids) for ids in selected])
                                if control == "correlated":
                                    paired_counts[pred] = counts
                                else:
                                    assert np.array_equal(counts, paired_counts[pred])
                                scenarios.append({"scope": scope_name, "anchor": anchor,
                                                  "predicate": pred, "control": control,
                                                  "mask": mask, "selected": selected, "counts": counts,
                                                  "scope_mask": scope})
                    audit_geometries.append({"clusters": clusters, "block_sizes": [len(ids) for ids in blocks], **stats})
                    for basis, z, zq, gram_upper in bases:
                        tail_norms = {r: np.linalg.norm(z[:, r:], axis=1) for r in (0, *ranks)}
                        query_norms = {r: np.linalg.norm(zq[:, r:], axis=1) for r in (0, *ranks)}
                        for j in range(8):
                            squared = np.square(z - zq[j])
                            prefix = np.cumsum(squared, axis=1)
                            truth, thresholds = [], []
                            for s in scenarios:
                                tmin = np.array([float(dist[j, ids].min()) if len(ids) else math.inf for ids in s["selected"]])
                                truth.append(tmin)
                                c = int(s["counts"].sum())
                                tau = float(np.partition(dist[j, s["mask"]], old.K - 1)[old.K - 1]) if c >= old.K else math.inf
                                sm = s["scope_mask"]
                                radius = float(np.partition(dist[j, sm], old.K - 1)[old.K - 1]) if sm.sum() >= old.K else math.inf
                                thresholds.append((("filtered_topk", tau), ("scope_radius", radius)))
                            # Closure writes only diagnostic records, never timed online estimates.
                            def emit(values, family, r, g, order, baseline=None, previous=None):
                                nonlocal row_count, g1_checks, bound_checks, max_bound_excess
                                whole = old.guarded(block_minima(values, blocks), gram_upper, dim)
                                current = []
                                for si, s in enumerate(scenarios):
                                    counts = s["counts"]
                                    active = counts > 0
                                    lb = old.guarded(block_minima(values, s["selected"]), gram_upper, dim)
                                    current.append(lb)
                                    actual = truth[si]
                                    excess = lb[active] - actual[active]
                                    max_bound_excess = max(max_bound_excess, float(excess.max(initial=0)))
                                    if np.any(excess > 0):
                                        raise AssertionError(f"observed FP32 lower bound failure: {basis} {r} {g}")
                                    if np.any(lb[active] + 1e-10 * np.maximum(1, actual[active]) < whole[active]):
                                        raise AssertionError("conditioning weakened bound")
                                    if previous is not None and np.any(lb[active] + 1e-10 * np.maximum(1, actual[active]) < previous[si][active]):
                                        raise AssertionError("nested group ordering")
                                    bound_checks += int(active.sum())
                                    for mode, threshold in thresholds[si]:
                                        skip = active & (lb > threshold)
                                        whole_skip = active & (whole > threshold)
                                        exact_skip = active & (actual > threshold)
                                        false_pruned = int((skip & ~exact_skip).sum())
                                        assert false_pruned == 0
                                        base_skip = active & (baseline[si] > threshold) if baseline is not None else skip
                                        common = {"dataset": args.dataset, "clusters": clusters, "basis": basis,
                                                  **{key: s[key] for key in ("scope", "anchor", "predicate", "control")},
                                                  "qidx": j, "r": r, "threshold_mode": mode}
                                        result = {**common, "family": family, "g": g, "order": order,
                                                  "tau": threshold if math.isfinite(threshold) else "inf",
                                                  "eligible": int(counts.sum()), "nonempty_blocks": int(active.sum()),
                                                  "singleton_blocks": int((counts == 1).sum()),
                                                  "whole_skip_items": int(counts[whole_skip].sum()),
                                                  "conditional_skip_items": int(counts[skip].sum()),
                                                  "filter_extra_items": int(counts[skip & ~whole_skip].sum()),
                                                  "joint_extra_over_g1_items": int(counts[skip & ~base_skip].sum()) if family == "group" else "",
                                                  "conditional_multi_skip_items": int(counts[skip & (counts > 1)].sum()),
                                                  "raw_oracle_skip_items": int(counts[exact_skip].sum()),
                                                  "false_pruned_blocks": false_pruned,
                                                  "eligible_min_reduction_values": int(counts.sum()) * values.shape[1],
                                                  "whole_min_reduction_values": n * values.shape[1]}
                                        if family == "group" and g == 1 and order == "contiguous" and r in (16, 64, dim):
                                            before = ref[reference_key(common)]
                                            for key, oldkey in (("eligible", "eligible"), ("conditional_skip_items", "gap_skip_items"),
                                                                ("raw_oracle_skip_items", "exact_oracle_skip_items")):
                                                if result[key] != int(before[oldkey]):
                                                    raise AssertionError(f"prior case mismatch: {key} {common}")
                                            g1_checks += 1
                                        writer.writerow(result)
                                        row_count += 1
                                return current
                            for r in ranks:
                                for order in ("contiguous", "permuted"):
                                    perm = np.arange(r) if order == "contiguous" else np.random.default_rng(old.SEED + r).permutation(r)
                                    base = previous = None
                                    for power in range(int(math.log2(r)) + 1):
                                        g = 2 ** power
                                        previous = emit(group_values(squared, perm, g), "group", r, g, order, base, previous)
                                        if g == 1:
                                            base = previous
                            for r in (0, *ranks):
                                head = prefix[:, r - 1] if r else np.zeros(n)
                                lift = head + np.square(tail_norms[r] - query_norms[r][j])
                                emit(lift[:, None], "radial_lift", r, 0, "na")
                        print(f"{args.dataset} K={clusters} {basis} done", flush=True)
    audit = {"dataset": args.dataset, "query_rows": row_count, "old_g1_query_matches": g1_checks,
             "checked_nonempty_bounds": bound_checks, "max_observed_bound_excess": max_bound_excess,
             "false_pruned_blocks": 0, "nested_group_and_conditioning_checks": True,
             "geometries": audit_geometries, "smoke": checks}
    old.dump(args.out / "audit.json", audit)
    old.dump(args.out / "provenance.json", {"dataset": args.dataset, "input_source": source,
             "reused_raw_distance_file": str(distance_file), "reused_raw_distance_sha256": old.digest(distance_file),
             "raw_distance_new_calls": 0, "reference_sha256": old.digest(REFERENCE / "queries.csv"),
             "script_sha256": old.digest(Path(__file__)), "protocol_sha256": old.digest(EXP / "joint-information-protocol.md"),
             "helper_sha256": old.digest(Path(old.__file__)), "seed": old.SEED, "ranks": ranks,
             "numpy": np.__version__, "faiss": faiss.__version__, "wall_s": time.perf_counter() - started,
             "timing_is_not_online_ann": True, "no_online_prototype": True,
             "offline_full_coordinate_square_terms": 2 * 2 * 8 * n * dim,
             "counter_scope": "min_reduction_values counts scalar inputs to block min; excludes transform and group-sum formation; no online cost estimate",
             "pca_gram_upper": beta, "pca64_variance_fraction": float(eig[-64:].sum() / eig.sum())})
    print(json.dumps({"dataset": args.dataset, "rows": row_count, "wall_s": time.perf_counter() - started}))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dataset", choices=("wiki-root3626", "sift12000"))
    ap.add_argument("--out", type=Path)
    ap.add_argument("--smoke-only", action="store_true")
    args = ap.parse_args()
    checks = smoke()
    if args.smoke_only:
        print(json.dumps(checks))
        return
    if not args.dataset or args.out is None:
        ap.error("--dataset and --out required unless --smoke-only")
    args.out.mkdir(parents=True, exist_ok=False)
    run(args, checks)


if __name__ == "__main__":
    main()
