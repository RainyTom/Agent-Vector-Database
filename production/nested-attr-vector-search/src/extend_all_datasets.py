#!/usr/bin/env python3
"""Extend run_all_datasets sweeps with larger ef on the already-built indexes."""
from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SRC = Path(__file__).resolve().parent
PROD = SRC.parent
EXPS = ROOT / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"
BIN = PROD / "work/bin"
K, NQ = 10, 1000


def sh(cmd, log, env=None):
    with log.open("w") as f:
        p = subprocess.run(cmd, cwd=ROOT, env={**os.environ, **(env or {})},
                           stdout=f, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"failed: {' '.join(map(str, cmd))} -> {log}")
    return float([l.split()[1] for l in log.read_text().splitlines()
                  if l.startswith("QUERY_SECONDS")][0])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--work-root", type=Path, default=PROD / "work/all_ds")
    ap.add_argument("--out", type=Path,
                    default=EXPS / "data/all_datasets_ext_20261020.csv")
    args = ap.parse_args()
    sys.path.insert(0, str(SRC))
    from run_five_l2 import score
    rows = []
    for name in sorted(p.name for p in (args.work_root / "fixtures").iterdir() if p.is_dir()):
        fix = args.work_root / "fixtures" / name
        ps = args.work_root / "pathseer" / name
        runs = args.work_root / "runs" / name
        raw = args.work_root / "logs" / name
        n, dim = map(int, (fix / "meta.txt").read_text().split()[:2])
        golden = np.fromfile(fix / "gt_sub.bin", dtype=np.int32).reshape(NQ, K)
        bm = np.memmap(ps / "query/query_sub.bv", dtype=np.uint8, mode="r",
                       shape=(NQ, (n + 7) // 8))
        common = ["--base", str(fix / "base.f32"), "--tree", str(fix / "tree.txt"),
                  "--labels", str(fix / "labels.bin"), "--meta", str(fix / "meta.txt")]
        for ef in (96, 128, 192, 256, 384, 512):
            ids, tim = raw / f"psg_shared_{ef}.ids", raw / f"psg_shared_{ef}.t"
            sec = sh([str(BIN / "candidate"), "query", *common,
                      "--index-dir", str(runs / "psg_shared"),
                      "--queries", str(fix / "test.f32"),
                      "--workload", str(fix / "workload_sub.txt"), "--mode", "subtree",
                      "--out", str(ids), "--timings", str(tim)], raw / f"psg_shared_{ef}.log",
                     {"GRAPH_EF": str(ef), "GRAPH_SHARE_VECTORS": "1", "OMP_NUM_THREADS": "1"})
            r = score(ids, tim, golden, bm, n, NQ, K)
            rows.append({"dataset": name, "method": "PSG-shared", "ef": ef,
                         "recall": r["recall_at_10"], "qps": NQ / sec})
        for ef in (64, 96, 128, 192):
            ids, tim = raw / f"les_{ef}.ids", raw / f"les_{ef}.t"
            sec = sh([str(BIN / "les_query"), "--out-dir", str(runs / "les"),
                      "--queries", str(ps / "query/query.bin"),
                      "--workload", str(fix / "workload_sub.txt"), "--mode", "subtree",
                      "--k", str(K), "--ef", str(ef), "--repeats", "2",
                      "--out", str(ids), "--timings", str(tim)], raw / f"les_{ef}.log")
            r = score(ids, tim, golden, bm, n, NQ, K)
            rows.append({"dataset": name, "method": "LES-v2", "ef": ef,
                         "recall": r["recall_at_10"], "qps": NQ / sec})
        print("extended", name)
    with args.out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["dataset", "method", "ef", "recall", "qps"])
        w.writeheader(); w.writerows(rows)
    print("wrote", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
