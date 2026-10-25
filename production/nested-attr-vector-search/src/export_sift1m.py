#!/usr/bin/env python3
"""Export base/test/meta for the 1M selectivity scale test.

Requires h5py; use the nested-attr work venv:
  production/nested-attr-vector-search/work/venv/bin/python \
    production/nested-attr-vector-search/src/export_sift1m.py

The 1M run uses random predicates only, so no tree/labels are written.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--hdf5", type=Path,
                    default=ROOT / "datasets" / "sift-128-euclidean.hdf5")
    ap.add_argument("--out", type=Path,
                    default=ROOT / "production" / "nested-attr-vector-search" /
                    "work" / "fixtures_1m")
    ap.add_argument("--n", type=int, default=1_000_000)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with h5py.File(args.hdf5, "r") as f:
        train = f["train"][:args.n].astype(np.float32)
        test = f["test"][:].astype(np.float32)
    train.tofile(args.out / "base.f32")
    test.tofile(args.out / "test.f32")
    (args.out / "meta.txt").write_text(
        f"{train.shape[0]} {train.shape[1]} {test.shape[0]} 10\n")
    print(f"wrote {args.out}: base={train.shape} test={test.shape}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
