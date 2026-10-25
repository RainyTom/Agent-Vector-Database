#!/usr/bin/env python3
"""Train product quantization on potion segment embeddings (numpy only).

Reads points.bin from a prep.py/prep_potion.py workload, samples 200k segment
vectors, trains M=8 subspaces x K=256 centroids with Lloyd k-means
(15 iterations, fixed seed), encodes every segment into M uint8 codes and
writes codes.bin / centroids.npy / meta.json to the output directory.

Usage:
  pq_train.py --dir work/all6_8760h_s50_potion --out work/pq
"""

import argparse
import json
import os
import time

import numpy as np

D = 128
M = 8
K = 256
ITERS = 15
NSAMPLE = 200_000
SEED = 20261025

# points.bin: int32 N, int32 D, then N * (int32 user, int64 ts, float lat,
# float lon, int32 cat, float emb[128]) -- packed, 536 bytes per record.
REC = np.dtype([
    ("user", "<i4"), ("ts", "<i8"), ("lat", "<f4"), ("lon", "<f4"),
    ("cat", "<i4"), ("emb", "<f4", (D,)),
])


def kmeans(X, K, iters, rng):
    """Lloyd k-means with seeded random init; returns (centroids, inertia)."""
    n = X.shape[0]
    init = rng.choice(n, K, replace=False)
    C = X[init].copy()
    x2 = np.einsum("ij,ij->i", X, X)
    assign = np.empty(n, dtype=np.int32)
    inertia = np.inf
    for _ in range(iters):
        c2 = np.einsum("ij,ij->i", C, C)
        # ||x-c||^2 = ||x||^2 - 2 x.c + ||c||^2
        D2 = x2[:, None] - 2.0 * (X @ C.T) + c2[None, :]
        assign = np.argmin(D2, axis=1)
        inertia = float(np.maximum(D2[np.arange(n), assign], 0.0).mean())
        counts = np.bincount(assign, minlength=K)
        sums = np.empty_like(C)
        for j in range(X.shape[1]):
            sums[:, j] = np.bincount(assign, weights=X[:, j], minlength=K)
        nz = counts > 0
        C[nz] = sums[nz] / counts[nz, None]
        # Reseed empty clusters to the points furthest from their centroid.
        empty = np.flatnonzero(~nz)
        if empty.size:
            dist = D2[np.arange(n), assign]
            far = np.argsort(dist)[-empty.size:]
            C[empty] = X[far]
    return C, inertia


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sample", type=int, default=NSAMPLE)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    t_start = time.time()

    pts_path = os.path.join(args.dir, "points.bin")
    with open(pts_path, "rb") as f:
        n = int(np.frombuffer(f.read(4), dtype="<i4")[0])
        d = int(np.frombuffer(f.read(4), dtype="<i4")[0])
    assert d == D, f"dim {d} != {D}"
    rec_bytes = REC.itemsize
    expected = 8 + n * rec_bytes
    size = os.path.getsize(pts_path)
    assert size == expected, f"points.bin size {size} != {expected}"
    print(f"points N={n} D={d} rec={rec_bytes} size={size}")

    mm = np.memmap(pts_path, dtype=REC, mode="r", offset=8, shape=(n,))

    rng = np.random.default_rng(SEED)
    ns = min(args.sample, n)
    idx = rng.choice(n, ns, replace=False)
    idx.sort()
    sample = np.asarray(mm["emb"][idx], dtype=np.float32)  # (ns, 128)
    print(f"sampled {ns} vectors, mem={sample.nbytes/1e6:.1f} MB")

    dsub = D // M
    centroids = np.empty((M, K, dsub), dtype=np.float32)
    inertia = []
    train_t = time.time()
    for s in range(M):
        X = np.ascontiguousarray(sample[:, s * dsub:(s + 1) * dsub])
        t0 = time.time()
        C, inert = kmeans(X, K, ITERS, rng)
        centroids[s] = C
        inertia.append(inert)
        print(f"  sub {s}: inertia(mean sq err)={inert:.6f} {time.time()-t0:.1f}s")
    train_s = time.time() - train_t
    print(f"trained M={M} K={K} iters={ITERS} in {train_s:.1f}s")

    # Encode all segments in chunks (per subspace GEMM, argmin -> uint8).
    codes = np.empty((n, M), dtype=np.uint8)
    c2 = [np.einsum("ij,ij->i", centroids[s], centroids[s]) for s in range(M)]
    # Counts per codeword for a quick usage check.
    usage = np.zeros((M, K), dtype=np.int64)
    chunk = 65536
    quant_err = 0.0
    for lo in range(0, n, chunk):
        hi = min(lo + chunk, n)
        X = np.asarray(mm["emb"][lo:hi], dtype=np.float32)
        for s in range(M):
            Xs = X[:, s * dsub:(s + 1) * dsub]
            x2 = np.einsum("ij,ij->i", Xs, Xs)
            D2 = x2[:, None] - 2.0 * (Xs @ centroids[s].T) + c2[s][None, :]
            ci = np.argmin(D2, axis=1)
            codes[lo:hi, s] = ci.astype(np.uint8)
            usage[s] += np.bincount(ci, minlength=K)
            quant_err += float(np.maximum(D2[np.arange(hi - lo), ci], 0.0).sum())
        if (lo // chunk) % 4 == 0:
            print(f"  encoded {hi}/{n}")
    del mm
    quant_err /= n
    print(f"quantization mean squared error per subvector: {quant_err:.6f}")

    np.save(os.path.join(args.out, "centroids.npy"), centroids)
    codes.tofile(os.path.join(args.out, "codes.bin"))
    empty_codes = int((usage == 0).sum())
    meta = {
        "source_points": pts_path,
        "N": int(n),
        "D": D,
        "M": M,
        "K": K,
        "dsub": dsub,
        "iters": ITERS,
        "seed": SEED,
        "sample": int(ns),
        "kmeans_init": "seeded random sample of 256 distinct training vectors",
        "train_s": round(train_s, 3),
        "code_bytes": int(n * M),
        "model_bytes": int(centroids.nbytes),
        "bytes_per_vector": M,
        "compression_ratio_vs_f32": round(D * 4 / M, 2),
        "quant_mean_sq_err_per_subvector": round(quant_err, 6),
        "inertia_per_subspace": [round(x, 6) for x in inertia],
        "empty_codewords": empty_codes,
        "codeword_usage_min": int(usage.min()),
        "codeword_usage_max": int(usage.max()),
        "wall_s": round(time.time() - t_start, 3),
    }
    with open(os.path.join(args.out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)
    print(json.dumps(meta, indent=2))


if __name__ == "__main__":
    main()
