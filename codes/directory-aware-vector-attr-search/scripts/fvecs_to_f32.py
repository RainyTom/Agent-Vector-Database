#!/usr/bin/env python3
"""Convert .fvecs (per-vector int32 dim header) to flat .f32 for repro.cpp."""
import sys

import numpy as np

def main() -> int:
    src, dst, dim = sys.argv[1], sys.argv[2], int(sys.argv[3])
    raw = np.fromfile(src, dtype=np.uint8)
    rec = 4 + 4 * dim
    n = raw.size // rec
    assert raw.size == n * rec, f"size {raw.size} not a multiple of {rec}"
    m = raw.view(np.int32).reshape(n, dim + 1)
    assert (m[:, 0] == dim).all(), "fvecs dim header mismatch"
    out = m[:, 1:].copy().astype(np.float32)
    out.tofile(dst)
    print(f"{src} -> {dst}: {n} x {dim}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
