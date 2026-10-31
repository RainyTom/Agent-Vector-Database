#!/usr/bin/env python3
"""Generate SIFT1M six-variant hierarchical attribute manifests
(2026-10-29 NodeANN-first hnswlib plan, §7.2).

Variants: D4/D6/D12 trees (branch 8/4/2, depth 4/6/12, always 4,096
leaves) x {balanced, skewed} leaf loads. Every object has exactly one
direct binding: 80% to a leaf, 20% to an ancestor whose depth is
uniform in [1, D-1]. Workloads use the same fixed 1,000 test-vector
sequence for all six variants; anchors are stratified by normalized
depth bucket and scope-cardinality bucket, half recursive and half
non-recursive.

Outputs (all under the experiment work dir, nothing else is touched):

  <work>/sift1m/base.f32, test.f32        shared raw vectors (LE f32, no header)
  <work>/sift_hierarchies/<variant>/tree.txt        "N" then "id parent" lines
  <work>/sift_hierarchies/<variant>/bindings.txt    one node id per line (line = global id)
  <work>/sift_hierarchies/<variant>/workload.txt    "nq 0" then "qidx rec anchor"
  <work>/sift_hierarchies/<variant>/manifest.json   hashes + load statistics

Requires numpy + h5py (conda env `sage` on this machine has both).
Ground truth is NOT generated here; it is rebuilt later by the unified
RawL2Space path.

Usage: python gen_sift_hierarchies.py [--work <dir>] [--force]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import h5py
import numpy as np

# ------------------------------------------------------------------ config --
SEED = 20261029
N_OBJECTS = 1_000_000
LEAVES = 4_096
LEAF_FRACTION = 0.80          # bound to leaves; rest to ancestors
ZIPF_ALPHA = 1.2
NQ = 1_000                    # fixed test-vector sequence length
K = 10                        # recorded for consumers; GT is not built here

# variant id -> (branch factor, max depth)
VARIANTS = {
    "d4_balanced": (8, 4),
    "d4_skewed": (8, 4),
    "d6_balanced": (4, 6),
    "d6_skewed": (4, 6),
    "d12_balanced": (2, 12),
    "d12_skewed": (2, 12),
}

REPO = Path(__file__).resolve().parents[3]
HDF5 = REPO / "datasets" / "sift-128-euclidean.hdf5"
DEFAULT_WORK = (
    REPO / "researches" / "nested-attr-vector-search" / "01-directory-aware-acceleration"
    / "2026-10-29-exps" / "work"
)

DEPTH_BUCKETS = 4            # normalized-depth strata
SCOPE_BUCKETS = 4            # scope-cardinality strata (log-spaced quartiles)


# ------------------------------------------------------------- statistics ---
def dist_stats(counts: np.ndarray) -> dict:
    x = np.asarray(counts, dtype=np.float64)
    n = int(x.size)
    total = float(x.sum())
    mean = total / n if n else 0.0
    sd = float(x.std())
    nz = x[x > 0]
    p = nz / nz.sum() if nz.sum() > 0 else nz
    entropy = float(-(p * np.log2(p)).sum()) if nz.size else 0.0
    xs = np.sort(x)
    gini = float((2.0 * np.arange(1, n + 1) @ xs) / (n * total) - (n + 1) / n) if total > 0 else 0.0
    return {
        "total": int(total),
        "mean": mean,
        "p50": float(np.percentile(x, 50)),
        "p95": float(np.percentile(x, 95)),
        "p99": float(np.percentile(x, 99)),
        "max": float(x.max()),
        "cv": sd / mean if mean > 0 else 0.0,
        "gini": gini,
        "empty_ratio": float((x == 0).mean()),
        "entropy_bits": entropy,
        "entropy_normalized": entropy / np.log2(n) if n > 1 else 0.0,
    }


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ------------------------------------------------------------- tree build ---
def build_tree(branch: int, depth: int) -> tuple[np.ndarray, np.ndarray]:
    """Perfect b-ary tree, level-order ids (root=0). Returns (parent, depth)."""
    assert branch ** depth == LEAVES
    n_nodes = sum(branch ** i for i in range(depth + 1))
    level_start = np.zeros(depth + 1, dtype=np.int64)
    for i in range(depth):
        level_start[i + 1] = level_start[i] + branch ** i
    parent = np.full(n_nodes, -1, dtype=np.int64)
    node_depth = np.zeros(n_nodes, dtype=np.int64)
    for i in range(1, depth + 1):
        lo, hi = level_start[i], level_start[i] + branch ** i
        node_depth[lo:hi] = i
        parent[lo:hi] = level_start[i - 1] + (np.arange(lo, hi) - lo) // branch
    return parent, node_depth


def compute_subtree_sizes(parent: np.ndarray, bindings: np.ndarray, n_nodes: int) -> np.ndarray:
    size = np.bincount(bindings, minlength=n_nodes).astype(np.int64)
    for v in range(n_nodes - 1, 0, -1):  # ids are level-ordered: reverse = leaves first
        size[int(parent[v])] += size[v]
    return size


# ---------------------------------------------------------- binding build ---
def allocate_leaf_counts(rng: np.random.Generator, load: str) -> np.ndarray:
    """Objects bound to leaves (80% of N_OBJECTS) per leaf id."""
    n_leaf_obj = int(round(N_OBJECTS * LEAF_FRACTION))
    if load == "balanced":
        w = np.full(LEAVES, 1.0 / LEAVES)
    else:
        perm = rng.permutation(LEAVES)          # fixed-seed random permutation
        rank = np.empty(LEAVES)
        rank[perm] = np.arange(1, LEAVES + 1)   # permuted rank 1..4096
        w = 1.0 / np.power(rank, ZIPF_ALPHA)
        w /= w.sum()
    exact = w * n_leaf_obj
    counts = np.floor(exact).astype(np.int64)
    rem = n_leaf_obj - int(counts.sum())
    if rem > 0:  # largest remainder, ties by lowest leaf id (deterministic)
        frac = exact - counts
        take = np.argsort(-frac, kind="stable")[:rem]
        counts[take] += 1
    assert counts.sum() == n_leaf_obj
    return counts


def build_bindings(rng: np.random.Generator, parent: np.ndarray, node_depth: np.ndarray,
                   branch: int, depth: int, load: str) -> np.ndarray:
    """One direct node id per object. Single binding by construction."""
    n_nodes = len(parent)
    level_start = np.zeros(depth + 1, dtype=np.int64)
    for i in range(depth):
        level_start[i + 1] = level_start[i] + branch ** i

    bindings = np.empty(N_OBJECTS, dtype=np.int64)
    n_leaf_obj = int(round(N_OBJECTS * LEAF_FRACTION))
    leaf_counts = allocate_leaf_counts(rng, load)
    leaf_ids = np.arange(level_start[depth], level_start[depth] + LEAVES)
    bindings[:n_leaf_obj] = np.repeat(leaf_ids, leaf_counts)

    anc_depth = rng.integers(1, depth, size=N_OBJECTS - n_leaf_obj)   # uniform [1, D-1]
    anc_node = np.empty_like(anc_depth)
    for d in np.unique(anc_depth):
        m = int(branch ** d)
        cand = level_start[d] + rng.integers(0, m, size=int((anc_depth == d).sum()))
        anc_node[anc_depth == d] = cand
    bindings[n_leaf_obj:] = anc_node

    assert len(bindings) == N_OBJECTS
    assert (bindings >= 0).all() and (bindings < n_nodes).all()
    # exactly one binding per object (array of node ids, one per object)
    assert np.unique(bindings).size <= n_nodes
    return bindings


# ------------------------------------------------------------ workload build --
def build_workload(rng: np.random.Generator, parent: np.ndarray, node_depth: np.ndarray,
                   bindings: np.ndarray, depth: int) -> np.ndarray:
    """Returns rows (nq, 3): qidx, rec, anchor. qidx sequence is fixed and
    shared across variants; anchors are stratified per variant."""
    direct = np.bincount(bindings, minlength=len(parent))
    subtree = compute_subtree_sizes(parent, bindings, len(parent))
    norm_depth = node_depth / depth

    rows = np.empty((NQ, 3), dtype=np.int64)
    rows[:, 0] = np.arange(NQ)                # fixed vector sequence, identical everywhere
    rows[:, 1] = (np.arange(NQ) < NQ // 2).astype(np.int64)  # 500 recursive, 500 not

    for rec in (0, 1):
        scope = subtree if rec else direct
        cand = np.where(scope > 0)[0]         # non-empty scopes only
        sc = scope[cand].astype(np.float64)
        # log-spaced scope-cardinality quartile edges over candidates
        edges = np.quantile(np.log2(sc), np.linspace(0, 1, SCOPE_BUCKETS + 1))
        edges[0], edges[-1] = -np.inf, np.inf
        d_edges = np.quantile(norm_depth[cand], np.linspace(0, 1, DEPTH_BUCKETS + 1))
        d_edges[0], d_edges[-1] = -np.inf, np.inf

        cells: list[np.ndarray] = []
        for db in range(DEPTH_BUCKETS):
            for sb in range(SCOPE_BUCKETS):
                m = ((norm_depth[cand] > d_edges[db]) & (norm_depth[cand] <= d_edges[db + 1])
                     & (np.log2(sc) > edges[sb]) & (np.log2(sc) <= edges[sb + 1]))
                cells.append(cand[m])

        idx = np.where(rows[:, 1] == rec)[0]
        for j, qi in enumerate(idx):
            cell = cells[j % len(cells)]
            if cell.size == 0:                # fallback: whole candidate set
                cell = cand
            rows[qi, 2] = cell[int(rng.integers(cell.size))]
    return rows


# ------------------------------------------------------------------ export --
def export_vectors(work: Path, force: bool) -> dict:
    sift_dir = work / "sift1m"
    sift_dir.mkdir(parents=True, exist_ok=True)
    out = {}
    with h5py.File(HDF5, "r") as f:
        for key, name, expect_n in (("train", "base", 1_000_000), ("test", "test", 10_000)):
            arr = np.asarray(f[key], dtype="<f4")
            assert arr.shape == (expect_n, 128), f"{key} shape {arr.shape}"
            dst = sift_dir / f"{name}.f32"
            if dst.exists() and not force:
                assert dst.stat().st_size == arr.nbytes, f"{dst} size mismatch"
            else:
                arr.tofile(dst)
            out[name] = {"path": str(dst.relative_to(REPO)), "sha256": sha256_of(dst),
                         "bytes": dst.stat().st_size, "n": expect_n, "dim": 128}
    return out


def write_variant(work: Path, vid: str, vectors: dict, force: bool) -> dict:
    branch, depth = VARIANTS[vid]
    load = "skewed" if vid.endswith("skewed") else "balanced"
    # deterministic per-variant seed: master seed + stable variant index
    rng = np.random.default_rng([SEED, list(VARIANTS).index(vid)])

    parent, node_depth = build_tree(branch, depth)
    bindings = build_bindings(rng, parent, node_depth, branch, depth, load)
    workload = build_workload(rng, parent, node_depth, bindings, depth)

    direct = np.bincount(bindings, minlength=len(parent))
    subtree = compute_subtree_sizes(parent, bindings, len(parent))

    # ---- assertions: depth, node count, binding totals, single binding ----
    assert int(node_depth.max()) == depth and len(parent) == sum(branch ** i for i in range(depth + 1))
    assert parent[0] == -1
    assert len(bindings) == N_OBJECTS and int(direct.sum()) == N_OBJECTS
    level_start = sum(branch ** i for i in range(depth))
    n_leaf_bound = int((bindings >= level_start).sum())
    assert n_leaf_bound == int(round(N_OBJECTS * LEAF_FRACTION)), n_leaf_bound
    anc = bindings[int(round(N_OBJECTS * LEAF_FRACTION)):]
    assert ((node_depth[anc] >= 1) & (node_depth[anc] <= depth - 1)).all()
    assert len(workload) == NQ and (workload[:, 1].sum() == NQ // 2)
    assert (workload[:, 2] >= 0).all() and (workload[:, 2] < len(parent)).all()
    if load == "balanced":
        leaf_counts = direct[level_start:]
        assert leaf_counts.max() - leaf_counts.min() <= 1

    vdir = work / "sift_hierarchies" / vid
    vdir.mkdir(parents=True, exist_ok=True)

    tree_p = vdir / "tree.txt"
    with tree_p.open("w") as f:
        f.write(f"{len(parent)}\n")
        for v in range(len(parent)):
            f.write(f"{v} {int(parent[v])}\n")

    bind_p = vdir / "bindings.txt"
    with bind_p.open("w") as f:
        f.write("\n".join(map(str, bindings.tolist())) + "\n")

    work_p = vdir / "workload.txt"
    with work_p.open("w") as f:
        f.write(f"{NQ} 0\n")
        for qidx, rec, anchor in workload:
            f.write(f"{qidx} {rec} {anchor}\n")

    manifest = {
        "variant": vid,
        "seed": SEED,
        "k": K,
        "n_objects": N_OBJECTS,
        "tree": {"branch": branch, "depth": depth, "leaves": LEAVES, "nodes": len(parent)},
        "load": {"type": load, "zipf_alpha": ZIPF_ALPHA if load == "skewed" else None,
                 "leaf_fraction": LEAF_FRACTION},
        "sha256": {p.name: sha256_of(p) for p in (tree_p, bind_p, work_p)},
        "vectors": vectors,
        "workload": {
            "nq": NQ,
            "qidx_sequence": "fixed 0..999, identical across all six variants",
            "recursive": int(NQ // 2), "non_recursive": int(NQ // 2),
            "stratification": {"depth_buckets": DEPTH_BUCKETS, "scope_buckets": SCOPE_BUCKETS},
            "scope_cards": {
                "recursive_subtree": dist_stats(subtree[workload[:, 2][workload[:, 1] == 1]]),
                "non_recursive_direct": dist_stats(direct[workload[:, 2][workload[:, 1] == 0]]),
            },
        },
        "direct": dist_stats(direct),
        "subtree": dist_stats(subtree),
    }
    (vdir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK)
    ap.add_argument("--force", action="store_true", help="re-export vectors even if present")
    args = ap.parse_args()
    args.work = args.work.resolve()
    assert HDF5.exists(), HDF5

    vectors = export_vectors(args.work, args.force)
    manifests = {}
    qidx_ref = None
    for vid in VARIANTS:
        m = write_variant(args.work, vid, vectors, args.force)
        manifests[vid] = m
        wl = np.loadtxt(args.work / "sift_hierarchies" / vid / "workload.txt",
                        skiprows=1, dtype=np.int64)
        qidx = wl[:, 0] if wl.ndim == 2 else None
        assert qidx is not None and len(qidx) == NQ
        if qidx_ref is None:
            qidx_ref = qidx
        else:
            assert (qidx == qidx_ref).all(), "query vector sequence diverged"
        print(f"{vid}: nodes={m['tree']['nodes']} direct mean={m['direct']['mean']:.2f} "
              f"gini={m['direct']['gini']:.4f} empty={m['direct']['empty_ratio']:.4f} "
              f"subtree max={m['subtree']['max']}")

    # cross-variant check: identical qidx everywhere
    summary = {"seed": SEED, "n_objects": N_OBJECTS, "variants": {
        v: {"tree": m["tree"], "load": m["load"], "direct_gini": m["direct"]["gini"],
            "direct_cv": m["direct"]["cv"], "subtree_gini": m["subtree"]["gini"]}
        for v, m in manifests.items()}}
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
