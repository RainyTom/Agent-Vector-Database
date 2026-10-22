#!/usr/bin/env python3
"""Benchmark harness: time-windowed semantic episode retrieval.

Runs the time-partitioned point-ANN + exact DTW verification baseline on the
six-city Massive-STEPS workload. The primary metric is query throughput at
Recall@10 >= 0.95; the exact scan is reported for reference.

Environment overrides:
  EP_TOPN, EP_CAND, EP_EF, EP_THREADS
"""

from __future__ import annotations

import csv
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASEDIR = ROOT / "production" / "semantic-mobility-search"
SRC = BASEDIR / "src"
WORK = BASEDIR / "work"
DATA = WORK / "all6_8760h_s50"
BIN = WORK / "ann"

N_QUERIES = 500
K = 10
TARGET_RECALL = 0.95
SEED = 20261022

CITIES = [
    ("ny", ROOT / "datasets/massive-steps/ny_checkins.csv"),
    ("istanbul", ROOT / "datasets/massive-steps/istanbul_checkins.csv"),
    ("jakarta", ROOT / "datasets/massive-steps/jakarta_checkins.csv"),
    ("petaling_jaya", ROOT / "datasets/massive-steps/petaling_jaya_checkins.csv"),
    ("sao_paulo", ROOT / "datasets/massive-steps/sao_paulo_checkins.csv"),
    ("moscow", ROOT / "datasets/massive-steps/moscow_checkins.csv"),
]


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, text=True, **kw)


def prepare() -> None:
    if (DATA / "queries.bin").exists() and (DATA / "points.bin").exists() \
            and (DATA / "user_city.bin").exists():
        return
    DATA.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(SRC / "prep.py"), "--out", str(DATA),
           "--queries", str(N_QUERIES), "--window-hours", "8760", "--spatial-km", "50"]
    for city, path in CITIES:
        if not path.exists():
            raise SystemExit(f"missing dataset: {path}")
        cmd += ["--csv", str(path), "--city", city]
    run(cmd)


def compile_ann() -> None:
    BIN.parent.mkdir(parents=True, exist_ok=True)
    if BIN.exists() and BIN.stat().st_mtime >= (SRC / "ann.cpp").stat().st_mtime:
        return
    include = ROOT / "deps" / "hnswlib"
    run(["g++", "-O3", "-march=native", "-std=c++17", "-pthread",
         f"-I{include}", str(SRC / "ann.cpp"), "-o", str(BIN)])


def main() -> int:
    topn = int(os.environ.get("EP_TOPN", "700"))
    cand = int(os.environ.get("EP_CAND", "1100"))
    ef = int(os.environ.get("EP_EF", "150"))
    threads = int(os.environ.get("EP_THREADS", "4"))

    prepare()
    compile_ann()

    proc = run([str(BIN), "--dir", str(DATA), "--k", str(K),
                "--topn", str(topn), "--cand", str(cand),
                "--ef", str(ef), "--threads", str(threads)],
               capture_output=True)

    rows = list(csv.DictReader((DATA / "ann_results.csv").open()))
    if not rows:
        raise SystemExit("ann produced no rows")
    n = len(rows)
    ann_us = [float(r["ann_us"]) for r in rows]
    recall = [float(r["recall_ann"]) for r in rows]
    exact_us = [float(r["exact_us"]) for r in rows]
    mean_ann = sum(ann_us) / n
    mean_recall = sum(recall) / n
    p95 = sorted(ann_us)[min(n - 1, int(0.95 * n))]
    qps = 1000.0 / mean_ann if mean_ann > 0 else 0.0
    exact_qps = 1000.0 / (sum(exact_us) / n)

    build_s = 0.0
    m = re.search(r"build_ms_total ([0-9.]+)", proc.stderr)
    if m:
        build_s = float(m.group(1)) / 1000.0

    gated = qps if mean_recall >= TARGET_RECALL else 0.0
    print(f"METRIC qps_at_r95={gated:.3f}")
    print(f"METRIC recall_at_10={mean_recall:.4f}")
    print(f"METRIC qps_raw={qps:.3f}")
    print(f"METRIC exact_qps={exact_qps:.3f}")
    print(f"METRIC p95_ms={p95:.4f}")
    print(f"METRIC build_s={build_s:.3f}")
    print(f"ASI topn={topn} cand={cand} ef={ef} threads={threads}")
    print(f"ASI n_queries={n} k={K} window_h=8760 target_recall={TARGET_RECALL}")
    print(f"ASI difficulty=6 cities, 63k users, 1.84M points, 1-year window")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
