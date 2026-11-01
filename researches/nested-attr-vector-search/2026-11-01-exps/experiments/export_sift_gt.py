#!/usr/bin/env python3
"""Export ann-benchmarks SIFT1M exact top-100 ids to .ivecs.

Source: datasets/sift-128-euclidean.hdf5 (neighbors, 10000x100).
Output: <out>.ivecs, standard ivecs records (int32 dim + int32 ids).
Only ids are exported; distances are recomputed by the C++ arm.
"""
import argparse
import hashlib
from pathlib import Path

import h5py
import numpy as np


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--hdf5', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--limit', type=int, default=1000)
    args = parser.parse_args()

    with h5py.File(args.hdf5, 'r') as handle:
        neighbors = np.asarray(handle['neighbors'][: args.limit], dtype=np.int32)
        queries = handle['test'].shape[0]
        base = handle['train'].shape[0]
    rows, k = neighbors.shape
    with args.out.open('wb') as handle:
        for row in neighbors:
            handle.write(np.int32(k).tobytes())
            handle.write(row.astype(np.int32, copy=False).tobytes())
    print({
        'kind': 'sift1m_exact_top100_ids',
        'source': str(args.hdf5),
        'source_sha256': sha256(args.hdf5),
        'base_vectors': base,
        'query_vectors_available': queries,
        'exported_queries': rows,
        'k': k,
        'output': str(args.out),
        'output_sha256': sha256(args.out),
        'note': 'ids only; distances recomputed in the C++ probe',
    })


if __name__ == '__main__':
    main()
