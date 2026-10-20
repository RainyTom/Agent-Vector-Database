#!/usr/bin/env python3
"""Archive audit for the 225 B1 blocks: hashes, per-query hits/legality, provenance."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SRC = Path(__file__).resolve().parent
PROD = SRC.parent
EXPS = ROOT / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"
FIX = PROD / "work/five_l2/fixtures/sift100k"
SPLITS = PROD / "work/five_l2/splits"
BLOCKS = EXPS / "experiments/b1/blocks"
K = 10


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def dir_digest(path: Path) -> str:
    h = hashlib.sha256()
    for p in sorted(path.rglob("*")):
        if not p.is_file():
            continue
        h.update(str(p.relative_to(path)).encode())
        h.update(b"\0")
        h.update(p.read_bytes())
    return h.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out-prefix", default="b1_archive_20261020")
    args = ap.parse_args()
    nq = json.loads((SPLITS / "manifest.json").read_text())["splits"]["test"]["count"]
    n = int((FIX / "meta.txt").read_text().split()[0])
    width = (n + 7) // 8
    gt = np.fromfile(SPLITS / "test/gt.bin", dtype=np.int32).reshape(nq, K)
    bm = np.fromfile(SPLITS / "test/bitmaps.bv", dtype=np.uint8).reshape(nq, width)
    feasible = np.unpackbits(bm, axis=1, count=n, bitorder="little")
    gt_count = np.minimum(K, feasible.sum(axis=1))

    rows, perq = [], []
    for cfgdir in sorted(BLOCKS.glob("block*/")):
        block = int(cfgdir.name.replace("block", ""))
        for cd in sorted(cfgdir.iterdir()):
            ids_p, tim_p, log_p = cd / "ids.bin", cd / "timings.f64", cd / "log.txt"
            if not ids_p.exists():
                continue
            ids = np.fromfile(ids_p, dtype=np.int32).reshape(nq, K)
            hits = np.zeros(nq, dtype=np.int64)
            viol = dup = filled = empty = 0
            for i in range(nq):
                pred = [int(x) for x in ids[i] if x >= 0]
                seen = set()
                for x in pred:
                    if x < 0 or x >= n or not feasible[i, x]:
                        viol += 1
                    if x in seen:
                        dup += 1
                    seen.add(x)
                filled += len(seen)
                empty += int(not seen)
                hits[i] = len(seen & set(int(x) for x in gt[i]))
            rec = hits.sum() / (nq * K)
            rows.append({"block": block, "config": cd.name, "recall": rec,
                         "hits": int(hits.sum()), "violations": viol,
                         "duplicate_ids": dup, "filled_slots": filled,
                         "empty_queries": empty,
                         "ids_sha256": sha(ids_p), "timings_sha256": sha(tim_p),
                         "log_sha256": sha(log_p)})
            if block == 1:
                tim = np.fromfile(tim_p, dtype=np.float64)
                sizes = feasible.sum(axis=1)
                for i in range(nq):
                    bucket = 0 if sizes[i] == 0 else 1 if sizes[i] < 10 else 2 if sizes[i] < 500 else 3 if sizes[i] < 10000 else 4
                    perq.append({"config": cd.name, "qid": i, "hits": int(hits[i]),
                                 "gt_count": int(gt_count[i]),
                                 "filled": int(len(set(int(x) for x in ids[i] if x >= 0))),
                                 "violations": int(sum(1 for x in ids[i]
                                                       if x < 0 or x >= n or not feasible[i, x])),
                                 "duplicate_ids": int(len([x for x in ids[i] if x >= 0]) -
                                                      len(set(int(x) for x in ids[i] if x >= 0))),
                                 "size": int(sizes[i]), "bucket": int(bucket),
                                 "latency_ms": float(tim[i])})

    audit_csv = EXPS / "data" / f"{args.out_prefix}_runs.csv"
    with audit_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)
    perq_csv = EXPS / "data" / f"{args.out_prefix}_per_query_block01.csv"
    with perq_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(perq[0]))
        w.writeheader(); w.writerows(perq)

    def h(p: Path):
        return {"path": str(p.relative_to(ROOT)), "sha256": sha(p), "bytes": p.stat().st_size}

    bucket_rows = []
    for cfg in sorted({r["config"] for r in perq}):
        for b in range(5):
            sel = [r for r in perq if r["config"] == cfg and r["bucket"] == b]
            if not sel:
                continue
            sec = sum(r["latency_ms"] for r in sel) / 1000.0
            bucket_rows.append({"config": cfg, "bucket": b, "nq": len(sel),
                                "recall": sum(r["hits"] for r in sel) / (len(sel) * K),
                                "filled": sum(r["filled"] for r in sel),
                                "violations": sum(r["violations"] for r in sel),
                                "duplicates": sum(r["duplicate_ids"] for r in sel),
                                "qps_bucket": len(sel) / sec if sec > 0 else 0})
    bucket_csv = EXPS / "data" / f"{args.out_prefix}_buckets.csv"
    with bucket_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(bucket_rows[0]))
        w.writeheader(); w.writerows(bucket_rows)

    manifest = {
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                          text=True).strip(),
        "binaries": {name: h(PROD / "work/bin" / name)
                     for name in ("les_query", "candidate")},
        "indexes": {
            "les_var": {"path": "/tmp/les_var", "digest": dir_digest(Path("/tmp/les_var"))},
            "psg_m16_shared": {"path": "/tmp/psg_sh100", "digest": dir_digest(Path("/tmp/psg_sh100"))},
            "psg_m12_shared": {"path": "/tmp/psg_shared12", "digest": dir_digest(Path("/tmp/psg_shared12"))},
        },
        "inputs": {name: h(FIX / name) for name in
                   ("base.f32", "test.f32", "tree.txt", "labels.bin", "meta.txt",
                    "workload_sub.txt", "gt_sub.bin")},
        "pathseer": {name: h(PROD / "work/five_l2/pathseer/sift100k" / name)
                     for name in ("query/query.bin", "query/query_sub.bv")},
        "splits_manifest": h(SPLITS / "manifest.json"),
        "policies": {p.name: h(p) for p in sorted(
            (EXPS / "data/b1_policies/b1_20261020").glob("policy_*_r9*.txt"))},
        "runs_csv": h(audit_csv),
        "per_query_csv": h(perq_csv),
        "buckets_csv": h(bucket_csv),
        "blocks": len({r["block"] for r in rows}), "configs": sorted({r["config"] for r in rows}),
        "posthoc_excluded": ["m12_ef72", "m12_ef80", "m12_ef96", "m12_ef128",
                             "les_ef28", "les_ef32"],
        "note": "ids/timings are local-only; hashes above allow independent recall checks.",
    }
    out = EXPS / "data" / f"{args.out_prefix}_manifest.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n")
    print("wrote", audit_csv, perq_csv, out)


if __name__ == "__main__":
    main()
