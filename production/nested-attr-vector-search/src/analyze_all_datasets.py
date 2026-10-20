#!/usr/bin/env python3
"""Summarise run_all_datasets CSV: r95/r98 throughput, index bytes, LES ratios."""
from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

EXPS = Path(__file__).resolve().parents[3] / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"


def best(rows, ds, method, t):
    c = [r for r in rows if r["dataset"] == ds and r["method"] == method and r["recall"] >= t]
    return max(c, key=lambda r: r["qps"]) if c else None


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--csv", type=Path, nargs="+", required=True)
    args = ap.parse_args()
    rows = []
    index = {}
    for path in args.csv:
        for r in csv.DictReader(path.open()):
            r["recall"] = float(r["recall"]); r["qps"] = float(r["qps"])
            r["index_bytes"] = int(r["index_bytes"]) if r.get("index_bytes") else 0
            if r["index_bytes"]:
                index[(r["dataset"], r["method"])] = r["index_bytes"]
            rows.append(r)
    for r in rows:
        if not r["index_bytes"]:
            r["index_bytes"] = index.get((r["dataset"], r["method"]), 0)
    datasets = list(dict.fromkeys(r["dataset"] for r in rows))
    out = []
    print(f"{'dataset':20}{'dim':>4}{'q':>2}  {'PSGsh r95/r98':>22} {'LES r95/r98':>22} "
          f"{'LES/PSG r95':>11} {'LES/PSG r98':>11} {'LES/PSG space':>13}")
    for ds in datasets:
        dim = next(r["dim"] for r in rows if r["dataset"] == ds)
        q = next(r["quantized"] for r in rows if r["dataset"] == ds)
        ps = next(r for r in rows if r["dataset"] == ds and r["method"] == "PSG-shared")
        a95, a98 = best(rows, ds, "PSG-shared", 0.95), best(rows, ds, "PSG-shared", 0.98)
        b95, b98 = best(rows, ds, "LES-v2", 0.95), best(rows, ds, "LES-v2", 0.98)
        space_ratio = b95["index_bytes"] / ps["index_bytes"] if b95 and ps["index_bytes"] else float("nan")
        def s(x):
            return f"{x['qps']:.0f}@{x['ef']}" if x else "-"
        r95 = b95["qps"] / a95["qps"] if a95 and b95 else float("nan")
        r98 = b98["qps"] / a98["qps"] if a98 and b98 else float("nan")
        print(f"{ds:20}{dim:>4}{'Y' if q=='True' else 'N':>2}  "
              f"{s(a95)+'/'+s(a98):>22} {s(b95)+'/'+s(b98):>22} "
              f"{r95:>11.2f} {r98:>11.2f} {space_ratio:>13.2f}")
        out.append({"dataset": ds, "dim": dim, "quantized": q,
                    "psg_r95_qps": a95["qps"] if a95 else "", "psg_r98_qps": a98["qps"] if a98 else "",
                    "les_r95_qps": b95["qps"] if b95 else "", "les_r98_qps": b98["qps"] if b98 else "",
                    "les_over_psg_r95": r95, "les_over_psg_r98": r98,
                    "les_over_psg_space": space_ratio,
                    "psg_index_bytes": ps["index_bytes"],
                    "les_index_bytes": b95["index_bytes"] if b95 else ""})
    dst = EXPS / "data" / (args.csv[0].stem + "_summary.csv")
    with dst.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out[0]))
        w.writeheader(); w.writerows(out)
    print("\nwrote", dst)


if __name__ == "__main__":
    main()
