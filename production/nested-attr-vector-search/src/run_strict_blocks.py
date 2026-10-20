#!/usr/bin/env python3
"""Interleaved strict-threshold blocks: LES-v3 vs shared-vector PSG.

All runs use original query order, one thread, fixed seeds. Each block runs
every config once in a shuffled order; report median/IQR QPS and strict recall
from saved ids. Strict thresholds are >=0.95 and >=0.98 (no interpolation).
"""
from __future__ import annotations

import argparse
import csv
import os
import random
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SRC = Path(__file__).resolve().parent
PROD = SRC.parent
BIN = PROD / "work/bin"
EXPS = ROOT / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"
FIX = PROD / "work/five_l2/fixtures/sift100k"
PS = PROD / "work/five_l2/pathseer/sift100k"
LES = Path("/tmp/les_var")
PSG = Path("/tmp/psg_sh100")
K, NQ = 10, 1000

CONFIGS = {
    "les_var_fixed12": {"kind": "les", "ef": 12, "adaptive": False, "target": "r95"},
    "les_var_adapt24": {"kind": "les", "ef": 24, "adaptive": True, "target": "r98"},
    "psg_fixed32": {"kind": "psg", "ef": 32, "adaptive": False, "target": "r95"},
    "psg_fixed48": {"kind": "psg", "ef": 48, "adaptive": False, "target": "r98"},
    "psg_adapt32": {"kind": "psg", "ef": 32, "adaptive": True, "target": "r95"},
    "psg_adapt64": {"kind": "psg", "ef": 64, "adaptive": True, "target": "r98"},
}


def run(cfg, block, outdir):
    c = CONFIGS[cfg]
    ids = outdir / f"{cfg}.ids"
    tim = outdir / f"{cfg}.t"
    env = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1"}
    if c["kind"] == "les":
        cmd = [str(BIN / "les_query"), "--out-dir", str(LES),
               "--queries", str(PS / "query/query.bin"),
               "--workload", str(FIX / "workload_sub.txt"), "--mode", "subtree",
               "--k", str(K), "--ef", str(c["ef"]), "--repeats", "1",
               "--out", str(ids), "--timings", str(tim)]
        if c["adaptive"]:
            cmd.append("--adaptive")
    else:
        env["GRAPH_SHARE_VECTORS"] = "1"
        env["GRAPH_EF"] = str(c["ef"])
        env["GRAPH_QUERY_ORDER"] = "original"
        if c["adaptive"]:
            env["GRAPH_ADAPTIVE"] = "1"
        cmd = [str(BIN / "candidate"), "query", "--base", str(FIX / "base.f32"),
               "--tree", str(FIX / "tree.txt"), "--meta", str(FIX / "meta.txt"),
               "--index-dir", str(PSG), "--queries", str(FIX / "test.f32"),
               "--workload", str(FIX / "workload_sub.txt"), "--mode", "subtree",
               "--out", str(ids), "--timings", str(tim), "--repeats", "1"]
    log = outdir / f"block{block}_{cfg}.log"
    with log.open("w") as f:
        p = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"{cfg} failed; see {log}")
    sec = float([l.split()[1] for l in log.read_text().splitlines()
                 if l.startswith("QUERY_SECONDS")][0])
    return sec


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--blocks", type=int, default=7)
    ap.add_argument("--out", type=Path,
                    default=EXPS / "data/five_l2_strict_blocks_20261020.csv")
    args = ap.parse_args()
    outdir = EXPS / "experiments/five_l2/strict_blocks"
    outdir.mkdir(parents=True, exist_ok=True)
    records = []
    rng = random.Random(20261020)
    for b in range(1, args.blocks + 1):
        order = list(CONFIGS)
        rng.shuffle(order)
        for cfg in order:
            sec = run(cfg, b, outdir)
            records.append({"block": b, "config": cfg, "seconds": sec,
                            "qps": NQ / sec})
    with args.out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["block", "config", "seconds", "qps"])
        w.writeheader(); w.writerows(records)
    sys.path.insert(0, str(SRC))
    from run_five_l2 import score
    golden = np.fromfile(FIX / "gt_sub.bin", dtype=np.int32).reshape(NQ, K)
    bm = np.memmap(PS / "query/query_sub.bv", dtype=np.uint8, mode="r",
                   shape=(NQ, (100000 + 7) // 8))
    print(f"{'config':20}{'recall':>8}{'median':>10}{'IQR':>16}{'strict':>8}")
    for cfg in CONFIGS:
        c = CONFIGS[cfg]
        r = score(outdir / f"{cfg}.ids", outdir / f"{cfg}.t", golden, bm, 100000, NQ, K)
        q = np.array([x["qps"] for x in records if x["config"] == cfg])
        med = float(np.median(q))
        q1, q3 = np.percentile(q, [25, 75])
        threshold = 0.95 if c["target"] == "r95" else 0.98
        print(f"{cfg:20}{r['recall_at_10']:8.4f}{med:10.0f}"
              f"{q1:8.0f}-{q3:<8.0f}{'PASS' if r['recall_at_10'] >= threshold else 'FAIL':>8}")
    print("wrote", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
