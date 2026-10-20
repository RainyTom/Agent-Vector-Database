#!/usr/bin/env python3
"""Convert a per-scope PSG index (vectors embedded per block) to the shared
vector layout used by `GRAPH_SHARE_VECTORS=1`, keeping the graph identical.

Writes <out>/base_u8.bin once and rewrites c_*.bin with the vector bytes
removed (header dim=0, stride reduced). Use with the query side's
GRAPH_SHARE_VECTORS=1. This is a verification/conversion tool; it does not
rebuild or alter the graph.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np


def convert(src: Path, dst: Path, base: np.ndarray) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    (dst / "base_u8.bin").write_bytes(base.astype(np.uint8).tobytes())
    files = sorted(src.glob("c_*.bin"), key=lambda p: int(p.stem.split("_")[1]))
    total = 0
    for path in files:
        data = path.read_bytes()
        m, maxl, ent, cap, dim, stride, tsize = struct.unpack_from("7I", data, 0)
        if dim == 0:
            raise ValueError(f"{path} already shared")
        off = 28
        gid = data[off:off + 4 * m]; off += 4 * m
        lv = data[off:off + m]; off += m
        uo = data[off:off + 4 * m]; off += 4 * m
        blocks = np.frombuffer(data, dtype=np.uint8, count=m * stride, offset=off)
        off += m * stride
        upn = struct.unpack_from("I", data, off)[0]; off += 4
        up = data[off:off + upn * tsize]
        new_stride = stride - dim
        shifted = np.empty((m, new_stride), dtype=np.uint8)
        blocks = blocks.reshape(m, stride)
        shifted[:] = blocks[:, dim:]
        # sanity: the embedded vector of block t must equal base[gid[t]]
        ids = np.frombuffer(gid, dtype=np.uint32)
        if not np.array_equal(blocks[:, :dim], base.reshape(-1, dim)[ids]):
            raise ValueError(f"{path}: embedded vectors differ from base_u8")
        out = bytearray()
        out += struct.pack("7I", m, maxl, ent, cap, 0, new_stride, tsize)
        out += gid + lv + uo + shifted.tobytes() + struct.pack("I", upn) + up
        (dst / path.name).write_bytes(out)
        total += len(out)
    print(f"converted {len(files)} scopes, {total} bytes + base_u8 {base.nbytes}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--base", required=True, type=Path,
                    help="fixture base.f32 (integer-valued float32)")
    args = ap.parse_args()
    base = np.fromfile(args.base, dtype=np.float32)
    convert(args.src, args.out, base)
    missing = [p.name for p in args.src.glob("*")
               if p.is_file() and not p.name.startswith("c_")
               and p.name != "base_u8.bin"]
    if missing:
        print("note: non-graph files not copied:", missing[:8])


if __name__ == "__main__":
    main()
