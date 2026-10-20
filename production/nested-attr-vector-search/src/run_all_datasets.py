#!/usr/bin/env python3
"""Run the LES-v2 vs shared-vector PSG comparison on every dense HDF5 corpus.

Datasets with native squared-L2 u8-valued data (SIFT, MNIST, Fashion-MNIST)
are used as-is. All other dense float corpora (angular/dot) are min-max
quantized to [0,255] and evaluated with squared L2; this is an internal,
like-for-like index comparison, NOT their native metric. Sparse jaccard
datasets are skipped (1-D sparse rows, no dense vector to index).

For each corpus at n=25000:
  PFS (exact) | PSG dup-vector M16 | PSG shared-vector M16 | LES-v2 (M16, cap16)
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SRC = Path(__file__).resolve().parent
PROD = SRC.parent
EXPS = ROOT / "researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps"
BIN = PROD / "work/bin"
K = 10
NQ = 1000

# name -> (hdf5, skip quantization?, take n)
SPECS = {
    "sift25k": ("sift-128-euclidean.hdf5", 25_000, True),
    "mnist25k": ("mnist-784-euclidean.hdf5", 25_000, True),
    "fashion-mnist25k": ("fashion-mnist-784-euclidean.hdf5", 25_000, True),
    "glove25": ("glove-25-angular.hdf5", 25_000, False),
    "glove50": ("glove-50-angular.hdf5", 25_000, False),
    "glove100": ("glove-100-angular.hdf5", 25_000, False),
    "glove200": ("glove-200-angular.hdf5", 25_000, False),
    "coco-i2i": ("coco-i2i-512-angular.hdf5", 25_000, False),
    "coco-t2i": ("coco-t2i-512-angular.hdf5", 25_000, False),
    "nytimes": ("nytimes-256-angular.hdf5", 25_000, False),
    "lastfm": ("lastfm-64-dot.hdf5", 25_000, False),
}


def sh(cmd, log, env=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as f:
        p = subprocess.run(cmd, cwd=ROOT, env={**os.environ, **(env or {})},
                           stdout=f, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"{cmd[0]} failed; see {log}")
    return log


def export(name, out):
    file, n, native = SPECS[name]
    src = ROOT / "datasets" / file
    out.mkdir(parents=True, exist_ok=True)
    with h5py.File(src, "r") as h:
        tr = np.asarray(h["train"][:n], dtype=np.float32)
        te = np.asarray(h["test"], dtype=np.float32)
        dim = tr.shape[1]
        if native:
            qtr, qte, meta = tr, te, {"quantized": False}
        else:
            lo, hi = float(tr.min()), float(tr.max())
            span = max(hi - lo, 1e-9)

            def q(x):
                return np.clip(np.rint((x - lo) / span * 255.0), 0, 255).astype(np.float32)
            qtr, qte = q(tr), q(te)
            meta = {"quantized": True, "min": lo, "max": hi,
                    "transform": "round(clip((x-min)/(max-min)*255))"}
        qtr.tofile(out / "base.f32")
        qte.tofile(out / "test.f32")
        (out / "meta.json").write_text(json.dumps({
            "name": name, "source": str(src.relative_to(ROOT)), "n": n, "dim": dim,
            "test_n": int(te.shape[0]), "native_distance": str(h.attrs.get("distance", "unknown")),
            "eval_distance": "squared_l2_on_u8", **meta}, indent=2) + "\n")
    return dim, meta


def query_ids(qbin, timings, log):
    return Path(qbin), Path(timings)


def score_one(ids, tim, golden, bitmap, n):
    sys.path.insert(0, str(SRC))
    from run_five_l2 import score
    return score(Path(ids), Path(tim), golden, bitmap, n, NQ, K)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--only", choices=tuple(SPECS))
    ap.add_argument("--work-root", type=Path, default=PROD / "work/all_ds")
    ap.add_argument("--out-csv", type=Path,
                    default=EXPS / "data" / f"all_datasets_{time.strftime('%Y%m%dT%H%M%SZ')}.csv")
    args = ap.parse_args()
    work = args.work_root.resolve()
    rows = []
    for name in ([args.only] if args.only else SPECS):
        dim, meta = export(name, work / "inputs" / name)
        fix = work / "fixtures" / name
        ps = work / "pathseer" / name
        runs = work / "runs" / name
        for d in (fix, ps, runs):
            if d.exists():
                shutil.rmtree(d)
        runs.mkdir(parents=True)
        n = SPECS[name][1]
        raw = work / "logs" / name
        sh([sys.executable, str(SRC / "prep_ccs.py"), "--fixtures", str(work / "inputs" / name),
            "--dim", str(dim), "--n", str(n), "--nq", str(NQ), "--out", str(fix)],
           raw / "prep.log")
        sh([sys.executable, str(SRC / "to_pathseer.py"), "--fixtures", str(fix),
            "--out", str(ps)], raw / "convert.log")
        golden = np.fromfile(fix / "gt_sub.bin", dtype=np.int32).reshape(NQ, K)
        width = (n + 7) // 8
        bitmap = np.memmap(ps / "query/query_sub.bv", dtype=np.uint8, mode="r",
                           shape=(NQ, width))
        common = ["--base", str(fix / "base.f32"), "--tree", str(fix / "tree.txt"),
                  "--labels", str(fix / "labels.bin"), "--meta", str(fix / "meta.txt")]

        # Exact scan
        sh([str(BIN / "candidate"), "query", "--mode", "subtree_scan", *common,
            "--queries", str(fix / "test.f32"), "--workload", str(fix / "workload_sub.txt"),
            "--out", str(raw / "pfs.ids"), "--timings", str(raw / "pfs.t"),
            "--repeats", "2"], raw / "pfs.log", {"OMP_NUM_THREADS": "1"})
        sec = float([l.split()[1] for l in (raw / "pfs.log").read_text().splitlines()
                     if l.startswith("QUERY_SECONDS")][0])
        r = score_one(raw / "pfs.ids", raw / "pfs.t", golden, bitmap, n)
        rows.append({"dataset": name, "dim": dim, "method": "PFS", "ef": "",
                     "recall": r["recall_at_10"], "qps": NQ / sec, "index_bytes": 0,
                     "quantized": meta.get("quantized", False)})

        # PSG duplicated vectors, and shared vectors, both M=16
        for tag, env in (("psg_dup", {}), ("psg_shared", {"GRAPH_SHARE_VECTORS": "1"})):
            (runs / tag).mkdir(parents=True, exist_ok=True)
            sh([str(BIN / "candidate"), "build", *common, "--index-dir", str(runs / tag)],
               raw / f"{tag}_build.log", {"GRAPH_M": "16", "GRAPH_EFC": "200",
                                          "OMP_NUM_THREADS": "1", **env})
            if tag == "psg_dup":
                sh([str(BIN / "les_build"), "--fixtures", str(fix),
                    "--index-dir", str(runs / tag), "--out", str(runs / "les"),
                    "--drop-aux", "--max-bucket-degree", "16"], raw / "les_build.log")
            for ef in (16, 24, 32, 48, 64):
                ids, tim = raw / f"{tag}_{ef}.ids", raw / f"{tag}_{ef}.t"
                sh([str(BIN / "candidate"), "query", *common, "--index-dir", str(runs / tag),
                    "--queries", str(fix / "test.f32"),
                    "--workload", str(fix / "workload_sub.txt"), "--mode", "subtree",
                    "--out", str(ids), "--timings", str(tim), "--repeats", "2"],
                   raw / f"{tag}_{ef}.log", {"GRAPH_EF": str(ef), "OMP_NUM_THREADS": "1", **env})
                sec = float([l.split()[1] for l in (raw / f"{tag}_{ef}.log").read_text().splitlines()
                             if l.startswith("QUERY_SECONDS")][0])
                rr = score_one(ids, tim, golden, bitmap, n)
                rows.append({"dataset": name, "dim": dim,
                             "method": "PSG-shared" if tag == "psg_shared" else "PSG-dup",
                             "ef": ef, "recall": rr["recall_at_10"], "qps": NQ / sec,
                             "index_bytes": sum(p.stat().st_size for p in (runs / tag).rglob("*") if p.is_file()),
                             "quantized": meta.get("quantized", False)})

        # LES-v2 on the duplicated-vector base graph (needs embedded vectors)
        for ef in (12, 16, 24, 32, 48):
            ids, tim = raw / f"les_{ef}.ids", raw / f"les_{ef}.t"
            sh([str(BIN / "les_query"), "--out-dir", str(runs / "les"),
                "--queries", str(ps / "query/query.bin"),
                "--workload", str(fix / "workload_sub.txt"), "--mode", "subtree",
                "--k", str(K), "--ef", str(ef), "--repeats", "2",
                "--out", str(ids), "--timings", str(tim)], raw / f"les_{ef}.log")
            sec = float([l.split()[1] for l in (raw / f"les_{ef}.log").read_text().splitlines()
                         if l.startswith("QUERY_SECONDS")][0])
            rr = score_one(ids, tim, golden, bitmap, n)
            rows.append({"dataset": name, "dim": dim, "method": "LES-v2", "ef": ef,
                         "recall": rr["recall_at_10"], "qps": NQ / sec,
                         "index_bytes": sum(p.stat().st_size for p in (runs / "les").rglob("*") if p.is_file()),
                         "quantized": meta.get("quantized", False)})
        print(f"done {name}")

    fields = ["dataset", "dim", "method", "ef", "recall", "qps", "index_bytes", "quantized"]
    args.out_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    print("wrote", args.out_csv)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
