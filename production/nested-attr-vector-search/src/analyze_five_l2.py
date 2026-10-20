#!/usr/bin/env python3
"""Summarise a five_l2 run: qps_at_r95 / qps_at_r98 and matched-recall pairs.

Merges the main results CSV with the sweep CSV (same run id) and writes
`five_l2_summary_<run>.csv`. Operating points are measured; qps_at_rX picks
the fastest measured point whose Recall@10 >= X (no interpolation).
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

EXPS = Path(__file__).resolve().parents[3] / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"
METHOD = {"pfs_subtree_scan": "PFS", "psg": "PSG", "les": "LES",
          "les_ef32": "LES", "bf_hnsw": "BF-HNSW", "triehi_hnsw": "TRIEHI+HNSW",
          "native_pathseer": "PathSeer"}


def load(path: Path) -> list[dict]:
    rows = []
    for r in csv.DictReader(path.open()):
        if r.get("status", "ok") != "ok":
            continue
        m = METHOD.get(r["method"])
        if not m:
            continue
        ef = r.get("ef")
        if not ef and r["method"].endswith("_ef32"):
            ef = "32"
        rows.append({"dataset": r["dataset"], "method": m, "ef": ef,
                     "recall": float(r["recall_at_10"]), "qps": float(r["qps"]),
                     "p95": float(r["p95_ms"]), "source": path.name})
    return rows


def best_at(rows, dataset, method, threshold):
    cand = [r for r in rows if r["dataset"] == dataset and r["method"] == method
            and r["recall"] >= threshold]
    return max(cand, key=lambda r: r["qps"]) if cand else None


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", required=True)
    args = ap.parse_args()
    main_csv = EXPS / "data" / f"five_l2_{args.run}.csv"
    sweep_csv = EXPS / "data" / f"five_l2_sweep_{args.run}.csv"
    rows = load(main_csv)
    if sweep_csv.is_file():
        # Sweep re-measures psg/les/hnsw at additional ef values. Keep main
        # rows (PFS/TRIEHI/PathSeer, plus ef21 PSG and ef384 BF) and let sweep
        # rows replace a point only when they share the same ef.
        merged = {(r["dataset"], r["method"], r["ef"]): r for r in rows}
        for r in load(sweep_csv):
            merged[(r["dataset"], r["method"], r["ef"])] = r
        rows = list(merged.values())
    out = EXPS / "data" / f"five_l2_summary_{args.run}.csv"
    fields = ["dataset", "method", "qps_at_r95", "r95_point", "p95_at_r95",
              "qps_at_r98", "r98_point", "p95_at_r98"]
    datasets = sorted({r["dataset"] for r in rows})
    with out.open("w", newline="") as stream:
        w = csv.DictWriter(stream, fieldnames=fields)
        w.writeheader()
        for ds in datasets:
            for method in ("PFS", "PSG", "LES", "BF-HNSW", "TRIEHI+HNSW", "PathSeer"):
                a = best_at(rows, ds, method, 0.95)
                b = best_at(rows, ds, method, 0.98)
                if not a and not b:
                    continue
                w.writerow({
                    "dataset": ds, "method": method,
                    "qps_at_r95": a["qps"] if a else None,
                    "r95_point": f"ef={a['ef']} recall={a['recall']:.4f}" if a else None,
                    "p95_at_r95": a["p95"] if a else None,
                    "qps_at_r98": b["qps"] if b else None,
                    "r98_point": f"ef={b['ef']} recall={b['recall']:.4f}" if b else None,
                    "p95_at_r98": b["p95"] if b else None,
                })
    print(out.read_text())


if __name__ == "__main__":
    main()
