#!/usr/bin/env python3
"""Summarise B1 blocks: per-config recall/QPS and paired per-block ratios with CI."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import numpy as np

EXPS = Path(__file__).resolve().parents[3] / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"
SPLITS = Path(__file__).resolve().parents[1] / "work/five_l2/splits"
K = 10


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--csv", type=Path, default=EXPS / "data/b1_blocks_20261020.csv")
    ap.add_argument("--blocks-dir", type=Path, default=EXPS / "experiments/b1/blocks")
    args = ap.parse_args()
    rows = list(csv.DictReader(args.csv.open()))
    for r in rows:
        r["qps"] = float(r["qps"]); r["wall_s"] = float(r["wall_s"])
    nq = int(rows[0]["nq"])
    gt = np.fromfile(SPLITS / "test/gt.bin", dtype=np.int32).reshape(nq, K)
    recall = {}
    for cfg in {r["config"] for r in rows}:
        vals = []
        for b in {r["block"] for r in rows}:
            p = args.blocks_dir / f"block{int(b):02d}" / cfg / "ids.bin"
            ids = np.fromfile(p, dtype=np.int32).reshape(nq, K)
            hits = 0
            for i in range(nq):
                hits += len(set(int(x) for x in ids[i] if x >= 0) &
                            set(int(x) for x in gt[i]))
            vals.append(hits / (nq * K))
        recall[cfg] = (min(vals), max(vals))
    print(f"{'config':16}{'recall(min/max)':>18}{'median qps':>12}{'IQR':>16}{'n':>4}")
    for cfg in sorted(recall):
        q = np.array([r["qps"] for r in rows if r["config"] == cfg])
        q1, q3 = np.percentile(q, [25, 75])
        print(f"{cfg:16}{recall[cfg][0]:.4f}/{recall[cfg][1]:.4f}{np.median(q):12.0f}"
              f"{q1:8.0f}-{q3:<8.0f}{len(q):>4}")
    pairs = [("les_pol_r95", "les_fixed12"), ("les_pol_r98", "les_adapt24"),
             ("les_pol_r98", "les_fixed14"),
             ("m12_pol_r95", "m12_fixed48"), ("m12_pol_r98", "m12_fixed64"),
             ("m16_pol_r95", "m16_fixed32"), ("m16_pol_r98", "m16_fixed48"),
             ("m12_fixed40", "les_fixed14"), ("m12_fixed48", "les_fixed12")]
    print("\npaired per-block ratios (median [95% CI by block bootstrap]):")
    rng = np.random.default_rng(20261020)
    for a, b in pairs:
        qa = {r["block"]: r["qps"] for r in rows if r["config"] == a}
        qb = {r["block"]: r["qps"] for r in rows if r["config"] == b}
        blocks = sorted(set(qa) & set(qb))
        if not blocks:
            continue
        ratio = np.array([qa[k] / qb[k] for k in blocks])
        boots = [np.median(ratio[rng.integers(0, len(ratio), len(ratio))]) for _ in range(2000)]
        lo, hi = np.percentile(boots, [2.5, 97.5])
        print(f"{a:14} / {b:14} median {np.median(ratio):.3f} "
              f"[{lo:.3f},{hi:.3f}] n={len(ratio)}")
    out = EXPS / "data/b1_blocks_summary_20261020.json"
    out.write_text(json.dumps({"recall": recall,
                               "median_qps": {c: float(np.median([r['qps'] for r in rows if r['config'] == c]))
                                              for c in recall}}, indent=2) + "\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
