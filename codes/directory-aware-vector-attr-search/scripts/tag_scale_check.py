#!/usr/bin/env python3
"""Measure the directory->label encoding scale (scheme E feasibility check).

For each bound object x with h(x)=u, scheme E attaches
  rec labels: (rec,a) for a in Anc(u) U {u}
  nrec label: (nrec,u)
This script reports: label universe size, labels per object, total entries,
and per-dataset comparison against Filtered-DiskANN's reported scale
(thousands of filter labels, tens-to-hundreds labels per point).

Usage: python3 tag_scale_check.py [--dir <fixture dir with *_tree.txt/*bindings*>]
"""
from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "codes" / "directory-aware-vector-attr-search" / "data"


def load_tree(path: Path):
    with open(path, encoding="utf-8") as f:
        n = int(f.readline())
        parent = [0] * n
        for _ in range(n):
            v, p = map(int, f.readline().split())
            parent[v] = p
    return parent


def load_bindings(path: Path, pairs: bool = False):
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if pairs:
                e, v = map(int, line.split())
                out.append(v)
            else:
                out.append(int(line))
    return out


def depth_of(parent):
    d = [0] * len(parent)
    for v in range(1, len(parent)):
        d[v] = d[parent[v]] + 1
    return d


def measure(name: str, tree: Path, bindings: Path, pairs: bool = False):
    parent = load_tree(tree)
    dep = depth_of(parent)
    bound = load_bindings(bindings, pairs)
    n = len(bound)
    rec_entries = sum(dep[u] + 1 for u in bound)   # ancestors incl. self
    nrec_entries = n
    used_nodes = len({u for u in bound})
    rec_labels = len(parent)      # every dir can be an anchor -> rec:<node>
    nrec_labels = used_nodes      # only nodes with direct bindings
    print(f"== {name}")
    print(f"  dirs={len(parent)} max_depth={max(dep)} "
          f"mean_bound_depth={sum(dep[u] for u in bound)/n:.2f}")
    print(f"  objects={n} nodes_with_bindings={used_nodes}")
    print(f"  rec labels (universe)={rec_labels}  nrec labels={nrec_labels}  "
          f"total distinct labels={rec_labels + nrec_labels}")
    print(f"  labels per object = {1 + (rec_entries + nrec_entries)/n:.2f} "
          f"(rec {rec_entries/n:.2f} + nrec 1)")
    print(f"  total label entries={rec_entries + nrec_entries} "
          f"({(rec_entries + nrec_entries)/1e6:.1f}M)")
    print(f"  Filtered-DiskANN reported scale: thousands of labels, "
          f"tens-to-hundreds labels/point")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", type=Path, default=DEFAULT)
    args = ap.parse_args()
    d = args.dir
    if (d / "data" / "real" / "wiki_tree.txt").exists():
        d = d / "data" / "real"
    real = d / "real" if (d / "real").exists() else d
    if (real / "wiki_tree.txt").exists():
        measure("WIKI-Dir (real)", real / "wiki_tree.txt", real / "wiki_bindings.txt")
    if (real / "sift1m" / "wiki_tree.txt").exists():
        measure("SIFT1M synthetic", real / "sift1m" / "wiki_tree.txt",
                real / "sift1m" / "wiki_entry_node.txt")
    if (d / "sift100k" / "wiki_tree.txt").exists():
        measure("SIFT100k synthetic", d / "sift100k" / "wiki_tree.txt",
                d / "sift100k" / "wiki_entry_node.txt")
    ar = d / "real_arxiv" if (d / "real_arxiv").exists() else None
    if ar and (ar / "subject_tree.txt").exists():
        measure("ARXIV-Dir subject", ar / "subject_tree.txt",
                ar / "subject_bindings.pairs", pairs=True)
        measure("ARXIV-Dir temporal", ar / "temporal_tree.txt",
                ar / "temporal_bindings.txt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
