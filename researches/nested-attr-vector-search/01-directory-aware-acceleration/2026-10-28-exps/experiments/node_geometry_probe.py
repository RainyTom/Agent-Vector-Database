#!/usr/bin/env python3
"""Does a directory Node occupy a compact region of embedding space?

Build-time statistics only (corpus), used to test the premise of
tree-graph co-design and multi-ball node envelopes. Query/GT unused.
"""
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[5]
DAY = Path(__file__).resolve().parents[1]
FIX = ROOT / "codes/directory-aware-vector-attr-search/data/wiki_full"
SEED = 20261028
BINS = [(50, 200), (200, 2000), (2000, 20000), (20000, 10 ** 9)]
PER_BIN = 18


def sqdist(A, B):
    return np.maximum(
        np.einsum("ij,ij->i", A, A)[:, None]
        + np.einsum("ij,ij->i", B, B)[None, :]
        - 2.0 * A @ B.T,
        0.0,
    )


def kmeans_radii(X, m, rng, iters=12):
    idx = rng.choice(len(X), m, replace=False)
    cen = X[idx].copy()
    labels = np.zeros(len(X), dtype=np.int64)
    for _ in range(iters):
        labels = np.argmin(sqdist(X, cen), axis=1)
        for j in range(m):
            member = labels == j
            if member.any():
                cen[j] = X[member].mean(0)
    radii = [float(np.sqrt(sqdist(X[labels == j], cen[j][None, :]).max()))
             for j in range(m) if (labels == j).any()]
    return max(radii), sum(radii)


def main():
    bindings = np.loadtxt(FIX / "wiki_bindings.txt", dtype=np.int64)
    counts = np.bincount(bindings)
    nodes = np.flatnonzero(counts)
    order = np.argsort(bindings, kind="stable")
    starts = np.r_[0, np.cumsum(counts[nodes])[:-1]]
    base = np.memmap(FIX / "base.f32", mode="r", dtype="<f4", shape=(len(bindings), 1024))
    rng = np.random.default_rng(SEED)
    gs = base[rng.choice(len(bindings), 4000, replace=False)].astype(np.float64)
    gs /= np.linalg.norm(gs, axis=1, keepdims=True)
    probe = gs[:2000]
    d = np.sqrt(sqdist(probe, probe))
    iu = np.triu_indices(len(probe), 1)
    global_mean = float(d[iu].mean())
    rows = []
    for lo, hi in BINS:
        cand = [i for i in range(len(nodes)) if lo <= counts[nodes[i]] < hi]
        if not cand:
            continue
        for gi in rng.choice(cand, min(PER_BIN, len(cand)), replace=False):
            ids = order[starts[gi]:starts[gi] + counts[nodes[gi]]]
            take = rng.choice(ids, min(1200, len(ids)), replace=False)
            X = base[take].astype(np.float64)
            X /= np.linalg.norm(X, axis=1, keepdims=True)
            center = X.mean(0)
            radius = float(np.sqrt(sqdist(X, center[None, :]).max()))
            S = X[:min(400, len(X))]
            ds = np.sqrt(sqdist(S, S))
            iu = np.triu_indices(len(S), 1)
            intra = ds[iu]
            nn10 = float(np.sort(np.where(np.eye(len(S), dtype=bool), np.inf, ds), axis=1)[:, :10].mean())
            diam = float(intra.max())
            # Size-matched control: same number of points drawn uniformly from the corpus.
            Xc = base[rng.choice(len(bindings), len(S), replace=False)].astype(np.float64)
            Xc /= np.linalg.norm(Xc, axis=1, keepdims=True)
            control = float(np.sqrt(sqdist(Xc, Xc))[iu].mean())
            intra_mean = float(intra.mean())
            r4max, r4sum = kmeans_radii(X, 4, rng)
            r8max, r8sum = kmeans_radii(X, 8, rng)
            rows.append({"size": int(counts[nodes[gi]]), "R1": radius,
                         "R4max": r4max, "R8max": r8max,
                         "R4sum": r4sum, "R8sum": r8sum, "diam": diam, "nn10": nn10,
                         "intra_mean": intra_mean, "control_mean": control,
                         "cohesion_ratio": intra_mean / control,
                         "R4_shrink": r4max / radius, "R8_shrink": r8max / radius,
                         "R1_over_global": radius / global_mean, "R1_over_nn10": radius / nn10})
    del base
    aggregate = []
    for lo, hi in BINS:
        sub = [r for r in rows if lo <= r["size"] < hi]
        if not sub:
            continue
        aggregate.append({
            "size_bin": f"{lo}-{'inf' if hi >= 10 ** 9 else hi}",
            "nodes": len(sub),
            "median_size": int(np.median([r["size"] for r in sub])),
            "median_R1": round(float(np.median([r["R1"] for r in sub])), 4),
            "median_R4max": round(float(np.median([r["R4max"] for r in sub])), 4),
            "median_R8max": round(float(np.median([r["R8max"] for r in sub])), 4),
            "median_R1_over_global": round(float(np.median([r["R1_over_global"] for r in sub])), 3),
            "median_R1_over_nn10": round(float(np.median([r["R1_over_nn10"] for r in sub])), 2),
            "median_diam": round(float(np.median([r["diam"] for r in sub])), 4),
            "median_intra_mean": round(float(np.median([r["intra_mean"] for r in sub])), 4),
            "median_control_mean": round(float(np.median([r["control_mean"] for r in sub])), 4),
            "median_cohesion_ratio": round(float(np.median([r["cohesion_ratio"] for r in sub])), 3),
            "median_R4_radius_shrink": round(float(np.median([r["R4_shrink"] for r in sub])), 3),
            "median_R8_radius_shrink": round(float(np.median([r["R8_shrink"] for r in sub])), 3),
        })
        print(json.dumps(aggregate[-1]), flush=True)
    out = DAY / "data/node_geometry_probe.json"
    out.write_text(json.dumps({"seed": SEED, "global_mean_pairwise_distance": global_mean,
                               "sampled_nodes": len(rows), "note": "corpus-only statistics; no query or GT",
                               "aggregate": aggregate, "per_node": rows}, indent=2) + "\n")
    print("saved", out, flush=True)


if __name__ == "__main__":
    main()
