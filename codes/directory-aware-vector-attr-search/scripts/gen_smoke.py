#!/usr/bin/env python3
"""Generate the small NodeANN smoke fixture: 2000x32 random vectors, a 7-node
tree, random single bindings, and a mixed workload. Deterministic (seed 42).

Output layout under the target dir:
  base.f32  test.f32  tree.txt  bindings.txt  workload.txt
Formats match the 2026-10-29 runner contract.
"""
import sys

import numpy as np


def main():
    out = sys.argv[1]
    rng = np.random.default_rng(42)
    dim, n_base, n_test = 32, 2000, 64

    base = rng.standard_normal((n_base, dim), dtype=np.float32)
    test = rng.standard_normal((n_test, dim), dtype=np.float32)

    # 7-node tree: 0 -> {1,2}, 1 -> {3,4}, 2 -> {5,6}
    parent = [-1, 0, 0, 1, 1, 2, 2]

    # Random bindings, but force node 5 to stay empty (exercises the -1 path)
    # and give every other node a non-trivial direct list.
    bind = rng.integers(0, 5, size=n_base)  # 0..4 only; node 5 stays empty
    for v, cnt in [(0, 40), (1, 60), (2, 50), (3, 90), (4, 80), (6, 100)]:
        idx = rng.choice(n_base, size=cnt, replace=False)
        bind[idx] = v

    # Workload: recursive/non-recursive halves, assorted anchors; anchor 5 has
    # an empty direct list; anchor 4's subtree is {4} only.
    rows = []
    anchors = [0, 1, 2, 3, 4, 5, 6]
    for i in range(40):
        rec = i % 2
        rows.append((i % n_test, rec, anchors[i % len(anchors)]))

    with open(f"{out}/base.f32", "wb") as f:
        f.write(base.tobytes())
    with open(f"{out}/test.f32", "wb") as f:
        f.write(test.tobytes())
    with open(f"{out}/tree.txt", "w") as f:
        f.write(f"{len(parent)}\n")
        for i, p in enumerate(parent):
            f.write(f"{i} {p}\n")
    with open(f"{out}/bindings.txt", "w") as f:
        for v in bind:
            f.write(f"{v}\n")
    with open(f"{out}/workload.txt", "w") as f:
        f.write(f"{len(rows)} 0\n")
        for qidx, rec, anchor in rows:
            f.write(f"{qidx} {rec} {anchor}\n")
    print(f"smoke fixture written to {out}: base={n_base}x{dim} "
          f"test={n_test} nodes={len(parent)} queries={len(rows)}")


if __name__ == "__main__":
    main()
