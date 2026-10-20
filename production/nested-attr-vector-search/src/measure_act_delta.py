#!/usr/bin/env python3
"""Measure compressibility of the LES activation array (act.bin).

Within one object's CSR segment the activation preorder is non-decreasing, so
the array is delta-encodable. We report the measured delta distribution and the
resulting entropy / varint cost, so any compression claim in the paper is
traceable to a measured quantity rather than an assumption.
"""
import math
import os

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
IDX = os.path.join(ROOT, "production", "nested-attr-vector-search", "work",
                   "les_ccs")
OUT = os.path.join(ROOT, "researches", "nested-attr-vector-search",
                   "07-laminar-edge-sharing", "2026-10-20-exps", "data")

offsets = np.fromfile(os.path.join(IDX, "offsets.bin"), dtype=np.uint32)
act = np.fromfile(os.path.join(IDX, "act.bin"), dtype=np.uint16)
n_obj = len(offsets) - 1

starts = offsets[:-1].astype(np.int64)
ends = offsets[1:].astype(np.int64)
lens = ends - starts
entries = int(lens.sum())

# vectorised per-segment deltas: act[k] - act[k-1] inside each segment
idx = np.arange(entries, dtype=np.int64)
owner = np.repeat(np.arange(n_obj), lens)
first = np.zeros(entries, dtype=bool)
first[starts[lens > 0]] = True
prev = np.empty(entries, dtype=np.int64)
prev[0] = 0
prev[1:] = act[:-1].astype(np.int64)
delta = act.astype(np.int64) - prev
delta[first] = -1  # segment start marker (absolute value stored separately)

d = delta[~first]
monotone_violations = int((d < 0).sum())

vals, counts = np.unique(d, return_counts=True)
p = counts / counts.sum()
entropy = float(-(p * np.log2(p)).sum())

# varint cost: 7 bits per continuation byte, plus 1 byte for the base value
varint_bits = 0
for v, c in zip(vals, counts):
    need = max(1, math.ceil(math.log2(int(v) + 1) if v > 0 else 1))
    varint_bits += c * (1 + need // 7 + (1 if need % 7 else 0)) * 8
current_bits = entries * 16

print(f"entries={entries:,} objects={n_obj:,} mean_degree={entries/n_obj:.1f}")
print(f"monotone_violations={monotone_violations}")
print(f"delta entropy={entropy:.2f} bits/symbol")
print(f"current act bits={current_bits/8/1e6:.2f} MB (u16)")
print(f"entropy lower bound={entries*entropy/8/1e6:.2f} MB")
print(f"varint estimate={varint_bits/8/1e6:.2f} MB")
print("top deltas:", [(int(v), int(c)) for v, c in
                      sorted(zip(vals, counts), key=lambda x: -x[1])[:6]])

os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, "act-delta-stats.txt"), "w") as f:
    f.write(f"entries {entries}\nobjects {n_obj}\n")
    f.write(f"monotone_violations {monotone_violations}\n")
    f.write(f"delta_entropy_bits {entropy:.4f}\n")
    f.write(f"act_u16_mb {current_bits/8/1e6:.3f}\n")
    f.write(f"entropy_lower_bound_mb {entries*entropy/8/1e6:.3f}\n")
    f.write(f"varint_estimate_mb {varint_bits/8/1e6:.3f}\n")
print("wrote act-delta-stats.txt")
