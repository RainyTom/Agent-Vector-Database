#!/usr/bin/env python3
"""Convert .fvecs (per-vector int32 dim header) to flat .f32 for repro.cpp."""
import os
import sys

import numpy as np

def main() -> int:
    src, dst, dim = sys.argv[1], sys.argv[2], int(sys.argv[3])
    rec = 4 + 4 * dim
    size = os.path.getsize(src)
    n, remainder = divmod(size, rec)
    assert remainder == 0, f"size {size} not a multiple of {rec}"
    raw = np.memmap(src, mode="r", dtype="<f4", shape=(n, dim + 1))
    assert (raw[:, 0].view("<i4") == dim).all(), "fvecs dim header mismatch"
    # Strip the integer headers without numerically converting float bit patterns.
    raw[:, 1:].tofile(dst)
    print(f"{src} -> {dst}: {n} x {dim}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
