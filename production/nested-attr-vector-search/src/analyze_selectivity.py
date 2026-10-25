#!/usr/bin/env python3
"""Render the selectivity CSV as markdown tables for the research report."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "production" / "nested-attr-vector-search" / "work"


def load(path: Path) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def find(rows, mode, bucket, method, variant=None):
    out = []
    for r in rows:
        if r["mode"] != mode or r["bucket"] != bucket or r["method"] != method:
            continue
        if variant and r["variant"] != variant:
            continue
        out.append(r)
    return out


def best(rows, mode, bucket, method, variant=None):
    cand = [r for r in find(rows, mode, bucket, method, variant) if r["ok"] == "1"]
    if not cand:
        return None
    return max(cand, key=lambda r: float(r["qps"]))


def fmt(v, nd=1):
    return "—" if v is None else f"{v:,.{nd}f}"


def fmt_attr(row, key, nd=2):
    return "—" if not row else f"{float(row[key]):,.{nd}f}"


def random_table(rows):
    buckets = sorted({r["bucket"] for r in rows if r["mode"] == "random"},
                     key=float)
    print("| s | exact bitmap QPS | exact list QPS | list gain | "
          "BF-HNSW ef | BF-HNSW QPS | recall | post QPS | post recall |")
    print("|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for b in buckets:
        ex = best(rows, "random", b, "exact", "precomputed")
        el = best(rows, "random", b, "exact_list", "precomputed")
        hn = best(rows, "random", b, "hnsw", "precomputed")
        po = best(rows, "random", b, "post", "precomputed")
        gain = (float(el["qps"]) / float(ex["qps"])
                if ex and el and float(ex["qps"]) else None)
        s = (el or ex or hn)
        print(
            f"| {float(s['s']):.4f} | {fmt(float(ex['qps']) if ex else None)} | "
            f"{fmt(float(el['qps']) if el else None)} | "
            f"{fmt(gain, 2) if gain else '—'}× | "
            f"{hn['ef'] if hn else '—'} | {fmt(float(hn['qps']) if hn else None)} | "
            f"{fmt(float(hn['recall']) * 100, 2) if hn else '—'}% | "
            f"{fmt(float(po['qps']) if po else None)} | "
            f"{fmt(float(po['recall']) * 100, 2) if po else '—'}% |"
        )


def directory_table(rows):
    print("| scope | bucket | s | TrieHI resolve µs | PE-online resolve µs | "
          "exact-list QPS | BF-HNSW ef | BF-HNSW QPS | recall |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    keys = sorted({(r["mode"], r["bucket"]) for r in rows if r["mode"] != "random"},
                  key=lambda x: (x[0], float(x[1])))
    for mode, b in keys:
        tri = find(rows, mode, b, "hnsw", "triehi")
        pe = find(rows, mode, b, "hnsw", "peonline")
        el = best(rows, mode, b, "exact_list", "precomputed")
        hn = best(rows, mode, b, "hnsw", "triehi")
        s = float((el or hn or (tri[0] if tri else {"s": 0}))["s"])
        print(
            f"| {mode} | {b} | {s:.4f} | "
            f"{fmt_attr(tri[0] if tri else None, 'resolve_us')} | "
            f"{fmt_attr(pe[0] if pe else None, 'resolve_us')} | "
            f"{fmt(float(el['qps']) if el else None)} | "
            f"{hn['ef'] if hn else '—'} | "
            f"{fmt(float(hn['qps']) if hn else None)} | "
            f"{fmt(float(hn['recall']) * 100, 2) if hn else '—'}% |"
        )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", type=Path,
                    default=WORK / "selectivity" / "selectivity.csv")
    args = ap.parse_args()
    rows = load(args.csv)
    print("## Controlled random predicates\n")
    random_table(rows)
    print("\n## Directory predicates\n")
    directory_table(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
