#!/usr/bin/env python3
"""S1 static analysis: can per-scope logical adjacency be interning-shared?

Reads a duplicated-vector PSG index (c_*.bin with embedded u8 vectors), extracts
each scope's ordered level-0 adjacency in global ids, and computes the exact
bytes of a reference+dictionary layout:
  scope_refs: u32 per (scope, local source)
  dict_offsets: u64 per unique list (+1)
  dict_targets: u32 per unique target
  plus per-scope gid (u32), upper adjacency, and one shared base_u8.bin.
No query engine is built; this is the gate from les-improvement-plan.md §3.1.
"""
from __future__ import annotations

import argparse
import struct
from collections import Counter
from pathlib import Path

import numpy as np


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--index", type=Path, required=True)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--dim", type=int, required=True)
    args = ap.parse_args()
    refs = 0
    gid_bytes = 0
    upper_bytes = 0
    header_bytes = 0
    lists = {}
    total_targets = 0
    lens = Counter()
    per_scope = []
    files = sorted(args.index.glob("c_*.bin"), key=lambda p: int(p.stem.split("_")[1]))
    for path in files:
        data = path.read_bytes()
        m, maxl, ent, cap, dim, stride, tsize = struct.unpack_from("7I", data, 0)
        if dim != args.dim:
            raise ValueError(f"{path}: dim {dim} != {args.dim}")
        off = 28
        gid = np.frombuffer(data, dtype=np.uint32, count=m, offset=off); off += 4 * m
        off += m  # lv
        off += 4 * m  # upper_off
        blocks = np.frombuffer(data, dtype=np.uint8, count=m * stride, offset=off)
        off += m * stride
        upn = struct.unpack_from("I", data, off)[0]; off += 4
        up = data[off:off + upn * tsize]
        refs += m
        gid_bytes += 4 * m
        upper_bytes += len(up)
        header_bytes += 28 + 4
        blocks = blocks.reshape(m, stride)
        slot_off = dim
        for t in range(m):
            slot = blocks[t, slot_off:].view(np.dtype("u2" if tsize == 2 else "u4"))
            cnt = int(slot[0])
            glob = tuple(int(gid[x]) for x in slot[1:1 + cnt])
            key = glob
            if key not in lists:
                lists[key] = len(lists)
                total_targets += len(key)
            lens[len(key)] += 1
        per_scope.append(m)
    unique = len(lists)
    ref_bytes = refs * 4
    off_bytes = (unique + 1) * 8
    target_bytes = total_targets * 4
    base = args.n * args.dim
    total = ref_bytes + off_bytes + target_bytes + gid_bytes + upper_bytes + header_bytes + base
    print(f"scopes={len(files)} refs={refs} unique_lists={unique} "
          f"unique_ratio={unique/refs:.3f}")
    print(f"list_len mean={total_targets/max(unique,1):.1f} "
          f"p50={np.percentile(list(lens.elements()),50):.0f} "
          f"p99={np.percentile(list(lens.elements()),99):.0f} max={max(lens)}")
    print(f"bytes: refs={ref_bytes/1e6:.2f}MB offsets={off_bytes/1e6:.2f}MB "
          f"targets={target_bytes/1e6:.2f}MB gid={gid_bytes/1e6:.2f}MB "
          f"upper={upper_bytes/1e6:.2f}MB headers={header_bytes/1e6:.2f}MB "
          f"base_u8={base/1e6:.2f}MB")
    print(f"TOTAL S1 deploy = {total/1e6:.2f} MB "
          f"(shared PSG ~62.34 MB, LES-v3 53.27 MB)")


if __name__ == "__main__":
    main()
