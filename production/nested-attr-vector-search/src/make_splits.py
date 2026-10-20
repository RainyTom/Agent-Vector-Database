#!/usr/bin/env python3
"""Create disjoint calibration-fit / validation / test query splits.

Uses the existing 1000-query sift100k fixture. Sizes are below the handoff's
1000-per-split target (only 1000 queries exist); the manifest states actual
counts. No query index is reused across splits.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fixture", type=Path,
                    default=Path("production/nested-attr-vector-search/work/five_l2/fixtures/sift100k"))
    ap.add_argument("--pathseer", type=Path,
                    default=Path("production/nested-attr-vector-search/work/five_l2/pathseer/sift100k"))
    ap.add_argument("--out", type=Path,
                    default=Path("production/nested-attr-vector-search/work/five_l2/splits"))
    ap.add_argument("--sizes", default="400,300,300")
    ap.add_argument("--seed", type=int, default=20261020)
    args = ap.parse_args()
    nf, ne, nt = map(int, args.sizes.split(","))
    total = nf + ne + nt
    rng = np.random.default_rng(args.seed)
    order = rng.permutation(1000)[:total]
    splits = {"fit": order[:nf], "val": order[nf:nf + ne], "test": order[nf + ne:]}

    qdata = (args.pathseer / "query/query.bin").read_bytes()
    nq, dim = np.frombuffer(qdata[:8], dtype=np.uint32)
    qvec = np.frombuffer(qdata[8:], dtype=np.float32).reshape(nq, dim)
    n, dmeta, nqmeta, k, nodes, words = map(
        int, (args.fixture / "meta.txt").read_text().split())
    width = (n + 7) // 8
    gt = np.fromfile(args.fixture / "gt_sub.bin", dtype=np.int32).reshape(nqmeta, k)
    bm = np.fromfile(args.pathseer / "query/query_sub.bv", dtype=np.uint8).reshape(nqmeta, width)
    wl = (args.fixture / "workload_sub.txt").read_text().split()
    pos, wl_lines = 2, []
    for _ in range(nqmeta):
        w = int(wl[pos]); pos += 1
        wl_lines.append(" ".join(wl[pos:pos + w])); pos += w

    manifest = {"seed": args.seed, "sizes": args.sizes, "dim": int(dim), "k": int(k),
                "source_queries": int(nq), "splits": {}}
    for name, idx in splits.items():
        out = args.out / name
        out.mkdir(parents=True, exist_ok=True)
        q = qvec[idx]
        with open(out / "query.bin", "wb") as f:
            f.write(np.array([len(idx), dim], dtype=np.uint32).tobytes())
            f.write(q.astype(np.float32).tobytes())
        q.astype(np.float32).tofile(out / "test.f32")
        with open(out / "workload.txt", "w") as f:
            f.write(f"{len(idx)} {k}\n")
            for i in idx:
                parts = wl_lines[i].split()
                f.write(f"{len(parts)} {wl_lines[i]}\n")
        gt[idx].astype(np.int32).tofile(out / "gt.bin")
        bm[idx].tofile(out / "bitmaps.bv")
        (out / "meta.txt").write_text(f"{n} {dmeta} {len(idx)} {k} {nodes} {words}\n")
        manifest["splits"][name] = {
            "count": int(len(idx)), "indices": [int(x) for x in idx],
            "sha256": {fn: sha(out / fn) for fn in
                       ("query.bin", "test.f32", "workload.txt", "gt.bin", "bitmaps.bv", "meta.txt")}}
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({k: v["count"] for k, v in manifest["splits"].items()}))
    print("wrote", args.out / "manifest.json")


if __name__ == "__main__":
    main()
