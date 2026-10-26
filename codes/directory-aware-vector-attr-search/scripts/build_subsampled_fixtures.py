#!/usr/bin/env python3
"""Subsample the released real datasets to a RAM-bounded ANN fixture.

Reads fvecs corpus + fvecs queries, keeps K uniformly sampled entries
(renumbered 0..K-1), remaps directory bindings, and emits a fixture
directory compatible with src/repro.cpp --mode dsq (base.f32/test.f32).

Usage:
  python build_subsampled_fixtures.py --dataset wiki --n-keep 400000
  python build_subsampled_fixtures.py --dataset arxiv --n-keep 400000

Output: data/real_fixtures/<dataset><K>/{base.f32,test.f32,old_to_new.txt,
  <tree/bindings/workload files copied with remapped ids>}
"""
import argparse
import json
import os
import random
import shutil
import struct

import numpy as np

SRC = "data/real"
FX = "data/real_fixtures"

DATASETS = {
    "wiki": {
        "corpus_fvecs": f"{SRC}/wiki/dbpedia_dir_2m_corpus_vectors.fvecs",
        "query_fvecs": f"{SRC}/wiki/dbpedia_dir_2m_query_vectors.fvecs",
        "dim": 1024,
        "trees": [("wiki_tree.txt", "wiki_bindings.txt", "ints")],
        "workload": "workload_wiki.txt",
    },
    "arxiv": {
        # NOTE: the Drive file named arxiv_query_constraint.json actually
        # contains the 1000 general-query vectors (fvecs, dim=1024).
        "corpus_fvecs": f"{SRC}/arxiv/arxiv_corpus_vectors.fvecs",
        "query_fvecs": f"{SRC}/arxiv/arxiv_query_constraint.json",
        "dim": 1024,
        "trees": [("subject_tree.txt", "subject_bindings.pairs", "pairs"),
                  ("temporal_tree.txt", "temporal_bindings.txt", "ints")],
        "workload": "workload_arxiv.txt",
    },
}


def fvecs_rows(path, dim):
    """Return row count and a memmap view (n, 4+4*dim) uint8."""
    mm = np.memmap(path, dtype=np.uint8, mode="r")
    rec = 4 + 4 * dim
    assert mm.size % rec == 0, f"{path}: size {mm.size} not multiple of {rec}"
    n = mm.size // rec
    # sanity-check the dim header of the first row
    (d0,) = struct.unpack("<i", mm[:4])
    assert d0 == dim, f"{path}: first dim={d0}, expected {dim}"
    return n, mm.reshape(n, rec)


def write_f32(rows, keep, dim, out_path):
    out = np.memmap(out_path, dtype=np.float32, mode="w+", shape=(len(keep), dim))
    for j, i in enumerate(keep):
        out[j] = rows[i, 4:].view(np.float32)
    out.flush()
    return out.shape


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True, choices=list(DATASETS))
    ap.add_argument("--n-keep", type=int, default=400_000)
    ap.add_argument("--seed", type=int, default=20261026)
    args = ap.parse_args()
    spec = DATASETS[args.dataset]
    dim = spec["dim"]
    rng = random.Random(args.seed)

    out_dir = f"{FX}/{args.dataset}{args.n_keep}"
    os.makedirs(out_dir, exist_ok=True)

    n, rows = fvecs_rows(spec["corpus_fvecs"], dim)
    keep = np.array(sorted(rng.sample(range(n), args.n_keep)))
    print(f"[{args.dataset}] corpus n={n}, keeping {args.n_keep}", flush=True)
    shape = write_f32(rows, keep, dim, f"{out_dir}/base.f32")
    print(f"  base.f32 {shape}", flush=True)
    del rows

    nq, qrows = fvecs_rows(spec["query_fvecs"], dim)
    qshape = write_f32(qrows, list(range(nq)), dim, f"{out_dir}/test.f32")
    print(f"  test.f32 {qshape} ({nq} queries)", flush=True)
    del qrows

    old_to_new = -np.ones(n, dtype=np.int64)
    old_to_new[keep] = np.arange(len(keep))
    np.savetxt(f"{out_dir}/old_to_new.txt", np.column_stack([keep, np.arange(len(keep))]),
               fmt="%d")

    kept = np.zeros(n, dtype=bool)
    kept[keep] = True
    for tree_file, bind_file, kind in spec["trees"]:
        shutil.copy(f"{FX}/{tree_file}", f"{out_dir}/{tree_file}")
        src_b = f"{FX}/{bind_file}"
        dst_b = f"{out_dir}/{bind_file}"
        kept_n = dropped_n = 0
        with open(src_b) as fi, open(dst_b, "w") as fo:
            if kind == "ints":
                # one node per entry, line i == entry i
                for e, line in enumerate(fi):
                    if e < n and kept[e]:
                        fo.write(line)
                        kept_n += 1
                    else:
                        dropped_n += 1
            else:
                for line in fi:
                    e, v = map(int, line.split())
                    if e < n and kept[e]:
                        fo.write(f"{old_to_new[e]} {v}\n")
                        kept_n += 1
                    else:
                        dropped_n += 1
        print(f"  {bind_file}: kept={kept_n} dropped={dropped_n}", flush=True)
    shutil.copy(f"{FX}/{spec['workload']}", f"{out_dir}/{spec['workload']}")

    meta = {"dataset": args.dataset, "full_entries": int(n), "kept": int(args.n_keep),
            "dim": dim, "queries": int(nq), "seed": args.seed,
            "note": "entries renumbered 0..K-1 (old_to_new.txt); trees and "
                    "workload anchors unchanged; scopes are subsampled"}
    json.dump(meta, open(f"{out_dir}/meta.json", "w"), indent=2)
    print("meta:", json.dumps(meta), flush=True)


if __name__ == "__main__":
    main()
