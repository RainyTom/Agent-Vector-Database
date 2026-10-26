#!/usr/bin/env python3
"""Simulate hierarchical nested attributes on SIFT for the
Directory-Aware (arXiv:2606.16903v1) reproduction.

The paper evaluates two directory-structured datasets:
  * WIKI-Dir: 1.94M entries, 363,467 directories, average depth 11.95,
    canonical category tree; shallow anchors hold very large subtrees.
  * ARXIV-Dir: 2.76M entries, two independent namespaces (subject:
    168 directories, average depth 2.19; temporal /yyyy/mm: 432
    directories, average depth 1.92).

We reproduce both shapes on SIFT100k (128-dim) with deterministic
synthetic hierarchies:

  * "wiki-like": deep random tree (~19k directories for 100k entries,
    entries bound mostly at deep nodes so that shallow anchors expand
    to large subtrees).
  * "arxiv-like": subject tree 1+14+42+168 (attach depths sampled to
    average ~2.19) and temporal tree /yyyy/mm with 20 years (attach
    at year w.p. 0.3, month w.p. 0.7).

Workloads follow the paper: queries are sampled from the test vectors;
each query carries one directory constraint with recursive or
non-recursive semantics. WIKI-like depth mix follows Fig. 10 of the
paper (anchors at depths 1/2/4/9 with counts 83/39/72/114 of 456).

Outputs into --out (text formats, consumed by src/repro.cpp):
  base.f32, test.f32
  wiki_tree.txt / wiki_entry_node.txt
  subject_tree.txt / subject_entry_node.txt
  temporal_tree.txt / temporal_entry_node.txt
  workload_wiki.txt / workload_arxiv.txt
  dsm_wiki.txt  (MOVE / MERGE operation lists)
  stats.json
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

SEED = 20261026
N = 100_000
NQ_WIKI = 500          # paper: 456 queries on WIKI-Dir
NQ_ARXIV = 1_000       # paper: 1,000 queries on ARXIV-Dir
N_MOVE = 1_000         # paper: 1,000 MOVE operations
N_MERGE = 1_000        # paper: 1,000 MERGE operations
WIKI_NODES = 19_000    # paper ratio: 363k dirs / 1.94M entries ~ 0.187


# ---------------------------------------------------------------- trees ----
def gen_wiki_tree(rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray]:
    """Deep random tree: prefer extending deep open nodes, else close them."""
    parent = np.full(WIKI_NODES, -1, dtype=np.int64)
    depth = np.zeros(WIKI_NODES, dtype=np.int64)
    MAXD = 14  # paper WIKI-Dir average depth 11.95
    for v in range(1, WIKI_NODES):
        cand = np.where(depth[:v] < MAXD)[0]
        w = (depth[cand] + 1).astype(np.float64) ** 2
        w /= w.sum()
        p = int(cand[int(rng.choice(len(cand), p=w))])
        parent[v] = p
        depth[v] = depth[p] + 1
    return parent, depth


def gen_subject_tree() -> tuple[np.ndarray, np.ndarray]:
    """1 + 14 + 42 + 168 = 225 directories, depth 3."""
    parent = [-1]
    depth = [0]
    for a in range(14):
        parent.append(0)
        depth.append(1)
        for b in range(3):
            u = len(parent)
            parent.append(1 + a)
            depth.append(2)
            for c in range(4):
                parent.append(u)
                depth.append(3)
    return np.asarray(parent), np.asarray(depth)


def gen_temporal_tree() -> tuple[np.ndarray, np.ndarray]:
    """root + 20 years + 20*12 months, depth 2."""
    parent = [-1]
    depth = [0]
    for y in range(20):
        parent.append(0)
        depth.append(1)
        for m in range(12):
            parent.append(1 + y)
            depth.append(2)
    return np.asarray(parent), np.asarray(depth)


def children_of(parent: np.ndarray) -> list[list[int]]:
    ch: list[list[int]] = [[] for _ in range(len(parent))]
    for v in range(1, len(parent)):
        ch[int(parent[v])].append(v)
    return ch


def assign_entries(rng, parent, depth, deep_p=0.9, deep_min=8):
    """Bind each entry to one directory: mostly deep nodes."""
    nodes = len(parent)
    deep_nodes = np.where(depth >= deep_min)[0]
    entry_node = np.empty(N, dtype=np.int64)
    for i in range(N):
        if deep_p > 0 and rng.random() < deep_p and len(deep_nodes):
            entry_node[i] = deep_nodes[int(rng.integers(len(deep_nodes)))]
        else:
            entry_node[i] = int(rng.integers(nodes))
    return entry_node


# ------------------------------------------------------------- workloads ----
def sample_anchor(rng, depth, want_depth):
    cand = np.where(depth == want_depth)[0]
    if len(cand) == 0:
        cand = np.arange(len(depth))
    return int(cand[int(rng.integers(len(cand)))])


def gen_wiki_workload(rng, depth):
    # Fig. 10 mix: depths 1/2/4/9 with counts 83/39/72/114 of 456,
    # remainder spread over deep levels.
    mix = [(1, 83), (2, 39), (4, 72), (9, 114)]
    fixed = sum(c for _, c in mix)
    rest = NQ_WIKI - fixed
    rows = []
    q = 0
    for d, c in mix:
        for _ in range(round(c * NQ_WIKI / 456)):
            rows.append((q, int(rng.integers(2)), sample_anchor(rng, depth, d)))
            q += 1
    for i in range(rest):
        d = int(rng.integers(10, 13))
        rows.append((q, int(rng.integers(2)), sample_anchor(rng, depth, d)))
        q += 1
    rng.shuffle(rows)
    return rows


def gen_arxiv_workload(rng, sdepth, tdepth):
    rows = []
    for q in range(NQ_ARXIV):
        if rng.random() < 0.5:
            d = int(rng.integers(1, 4))
            rows.append((q, 0, int(rng.integers(2)), sample_anchor(rng, sdepth, d)))
        else:
            d = int(rng.integers(1, 3))
            rows.append((q, 1, int(rng.integers(2)), sample_anchor(rng, tdepth, d)))
    return rows


def gen_dsm_ops(rng, parent, entry_node):
    """Random MOVE(u,w) / MERGE(u,w): u non-root, w not inside subtree(u).

    Targets are weighted by subtree size (paper Fig. 9/10: expansion-based
    DSM cost is driven by large, shallow subtrees; deep directories are
    cheap for every design).
    """
    ch = children_of(parent)
    n = len(parent)
    # subtree ENTRY counts (DSM cost in the paper scales with the moved
    # subtree's content, see Fig. 9/10); parent index < child index holds
    sub_size = np.bincount(entry_node, minlength=n).astype(np.float64) + 1.0
    for v in range(n - 1, 0, -1):
        sub_size[parent[v]] += sub_size[v]
    wt = sub_size.copy()
    wt[0] = 0  # never move/merge the root
    wt /= wt.sum()

    def in_subtree(u, wnode):
        stack = [u]
        while stack:
            x = stack.pop()
            if x == wnode:
                return True
            stack.extend(ch[x])
        return False

    def sample_u():
        return int(rng.choice(n, p=wt))

    def sample_pair():
        for _ in range(50):
            u = sample_u()
            wnode = int(rng.integers(1, n))
            if u != wnode and not in_subtree(u, wnode):
                return u, wnode
        return 1, 2

    ops = []
    for i in range(N_MOVE):
        u, w = sample_pair()
        ops.append(("MOVE", u, w))
    for i in range(N_MERGE):
        u, w = sample_pair()
        ops.append(("MERGE", u, w))
    return ops


# ------------------------------------------------------------------ main ----
def write_tree(path: Path, parent: np.ndarray):
    with path.open("w") as f:
        f.write(f"{len(parent)}\n")
        for v in range(len(parent)):
            f.write(f"{v} {int(parent[v])}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", default="datasets/sift-128-euclidean.hdf5")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)

    import h5py

    with h5py.File(args.dataset, "r") as f:
        base = np.asarray(f["train"][:N], dtype=np.float32)
        test = np.asarray(f["test"][:1000], dtype=np.float32)
    base.tofile(out / "base.f32")
    test.tofile(out / "test.f32")

    wiki_parent, wiki_depth = gen_wiki_tree(rng)
    subj_parent, subj_depth = gen_subject_tree()
    temp_parent, temp_depth = gen_temporal_tree()

    # subject entries: attach depth 1 w.p. .25 / 2 w.p. .45 / 3 w.p. .30
    def subject_entry(rng):
        r = rng.random()
        d = 1 if r < 0.25 else 2 if r < 0.70 else 3
        return sample_anchor(rng, subj_depth, d)

    subj_entry = np.array([subject_entry(rng) for _ in range(N)], dtype=np.int64)
    # temporal entries: year (depth1) w.p. 0.3, month (depth2) w.p. 0.7
    temp_entry = np.array([
        sample_anchor(rng, temp_depth, 1 if rng.random() < 0.3 else 2)
        for _ in range(N)
    ], dtype=np.int64)
    wiki_entry = assign_entries(rng, wiki_parent, wiki_depth)

    write_tree(out / "wiki_tree.txt", wiki_parent)
    write_tree(out / "subject_tree.txt", subj_parent)
    write_tree(out / "temporal_tree.txt", temp_parent)
    np.savetxt(out / "wiki_entry_node.txt", wiki_entry, fmt="%d")
    np.savetxt(out / "subject_entry_node.txt", subj_entry, fmt="%d")
    np.savetxt(out / "temporal_entry_node.txt", temp_entry, fmt="%d")

    with (out / "workload_wiki.txt").open("w") as f:
        f.write(f"{NQ_WIKI} 0\n")
        for q, rec, a in gen_wiki_workload(rng, wiki_depth):
            f.write(f"{q} {rec} {a}\n")
    with (out / "workload_arxiv.txt").open("w") as f:
        f.write(f"{NQ_ARXIV} 0\n")
        for q, ns, rec, a in gen_arxiv_workload(rng, subj_depth, temp_depth):
            f.write(f"{q} {ns} {rec} {a}\n")

    with (out / "dsm_wiki.txt").open("w") as f:
        f.write(f"{N_MOVE + N_MERGE} 0\n")
        for op, u, w in gen_dsm_ops(rng, wiki_parent, wiki_entry):
            f.write(f"{op} {u} {w}\n")

    stats = {
        "seed": SEED,
        "n": N,
        "wiki": {
            "dirs": int(len(wiki_parent)),
            "max_depth": int(wiki_depth.max()),
            "mean_depth": float(wiki_depth.mean()),
        },
        "subject": {"dirs": int(len(subj_parent)), "mean_depth": float(subj_depth[subj_entry].mean())},
        "temporal": {"dirs": int(len(temp_parent)), "mean_depth": float(temp_depth[temp_entry].mean())},
        "nq_wiki": NQ_WIKI,
        "nq_arxiv": NQ_ARXIV,
        "n_move": N_MOVE,
        "n_merge": N_MERGE,
    }
    (out / "stats.json").write_text(json.dumps(stats, indent=2))
    print(json.dumps(stats, indent=2))


if __name__ == "__main__":
    main()
