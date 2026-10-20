#!/usr/bin/env python3
"""Run B1/fixed/adaptive configurations in interleaved blocks on the frozen test split."""
from __future__ import annotations

import argparse
import csv
import json
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
SPLITS = PROD / "work/five_l2/splits"
POL = EXPS / "data/b1_policies/b1_20261020"
LES = Path("/tmp/les_var")
M16 = Path("/tmp/psg_sh100")
M12 = Path("/tmp/psg_shared12")
K = 10

CONFIGS = {
    "les_fixed12": {"kind": "les", "index": LES, "ef": 12},
    "les_fixed14": {"kind": "les", "index": LES, "ef": 14},
    "les_adapt24": {"kind": "les", "index": LES, "ef": 24, "adaptive": True},
    "les_pol_r95": {"kind": "les", "index": LES, "ef": 24, "policy": POL / "policy_les_r95.txt"},
    "les_pol_r98": {"kind": "les", "index": LES, "ef": 24, "policy": POL / "policy_les_r98.txt"},
    "m12_fixed40": {"kind": "psg", "index": M12, "ef": 40, "nq": 300},
    "m12_fixed48": {"kind": "psg", "index": M12, "ef": 48, "nq": 300},
    "m12_fixed64": {"kind": "psg", "index": M12, "ef": 64, "nq": 300},
    "m12_pol_r95": {"kind": "psg", "index": M12, "ef": 48, "policy": POL / "policy_psg_m12_r95.txt"},
    "m12_pol_r98": {"kind": "psg", "index": M12, "ef": 64, "policy": POL / "policy_psg_m12_r98.txt"},
    "m16_fixed32": {"kind": "psg", "index": M16, "ef": 32},
    "m16_fixed48": {"kind": "psg", "index": M16, "ef": 48},
    "m16_adapt64": {"kind": "psg", "index": M16, "ef": 64, "adaptive": True},
    "m16_pol_r95": {"kind": "psg", "index": M16, "ef": 48, "policy": POL / "policy_psg_m16_r95.txt"},
    "m16_pol_r98": {"kind": "psg", "index": M16, "ef": 64, "policy": POL / "policy_psg_m16_r98.txt"},
}


def run_cfg(name, block, outroot):
    c = CONFIGS[name]
    d = outroot / f"block{block:02d}" / name
    d.mkdir(parents=True, exist_ok=True)
    ids, tim, log = d / "ids.bin", d / "timings.f64", d / "log.txt"
    env = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1"}
    split = SPLITS / "test"
    nq = json.loads((SPLITS / "manifest.json").read_text())["splits"]["test"]["count"]
    if c["kind"] == "les":
        cmd = [str(BIN / "les_query"), "--out-dir", str(c["index"]),
               "--queries", str(split / "query.bin"), "--workload", str(split / "workload.txt"),
               "--mode", "subtree", "--k", str(K), "--ef", str(c["ef"]),
               "--repeats", "1", "--out", str(ids), "--timings", str(tim)]
        if c.get("adaptive"):
            cmd.append("--adaptive")
        if c.get("policy"):
            cmd += ["--ef-policy", str(c["policy"])]
    else:
        env.update(GRAPH_SHARE_VECTORS="1", GRAPH_QUERY_ORDER="original")
        if c.get("policy"):
            env["GRAPH_EF_POLICY"] = str(c["policy"])
        else:
            env["GRAPH_EF"] = str(c["ef"])
        if c.get("adaptive"):
            env["GRAPH_ADAPTIVE"] = "1"
        cmd = [str(BIN / "candidate"), "query", "--base", str(FIX / "base.f32"),
               "--tree", str(FIX / "tree.txt"), "--meta", str(split / "meta.txt"),
               "--index-dir", str(c["index"]), "--queries", str(split / "test.f32"),
               "--workload", str(split / "workload.txt"), "--mode", "subtree",
               "--out", str(ids), "--timings", str(tim), "--repeats", "1"]
    with log.open("w") as f:
        p = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"{name} failed: {log}")
    wall = float([l.split()[1] for l in log.read_text().splitlines()
                  if l.startswith("BATCH_WALL_SECONDS")][0])
    return wall, nq


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--blocks", type=int, default=15)
    ap.add_argument("--out", type=Path,
                    default=EXPS / "data/b1_blocks_20261020.csv")
    args = ap.parse_args()
    outroot = EXPS / "experiments/b1/blocks"
    available = {k: v for k, v in CONFIGS.items()
                 if not v.get("policy") or v["policy"].exists()}
    print("configs:", ", ".join(available))
    rows = []
    rng = random.Random(20261020)
    for b in range(1, args.blocks + 1):
        names = list(available)
        rng.shuffle(names)
        for name in names:
            wall, nq = run_cfg(name, b, outroot)
            rows.append({"block": b, "config": name, "wall_s": wall,
                         "qps": nq / wall, "nq": nq})
    with args.out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["block", "config", "wall_s", "qps", "nq"])
        w.writeheader(); w.writerows(rows)
    print("wrote", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
