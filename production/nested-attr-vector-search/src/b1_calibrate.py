#!/usr/bin/env python3
"""B1: calibrate per-size-bucket ef tables on a disjoint fit split.

Sweeps ef per method on the fit split, computes per-bucket hits/time, picks a
table per target via exhaustive enumeration, validates candidates on val, and
freezes the fastest passing table. Test is untouched here.
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
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
LES = Path("/tmp/les_var")
PSG16 = Path("/tmp/psg_sh100")
PSG12 = Path("/tmp/psg_shared12")
K = 10
EFS = [10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 64, 96, 128]
BUCKETS = [(0, 0), (1, 9), (10, 499), (500, 9999), (10000, 10**12)]

METHODS = {
    "les": {"kind": "les", "index": LES},
    "psg_m16": {"kind": "psg", "index": PSG16},
    "psg_m12": {"kind": "psg", "index": PSG12},
}


def bucket_of(size: int) -> int:
    for b, (lo, hi) in enumerate(BUCKETS):
        if lo <= size <= hi:
            return b
    return 4


def run(method: str, split: str, ef: int, tag: str, logdir: Path, repeats: int = 1):
    cfg = METHODS[method]
    d = SPLITS / split
    ids, tim = logdir / f"{tag}_{ef}.ids", logdir / f"{tag}_{ef}.t"
    env = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1"}
    if cfg["kind"] == "les":
        cmd = [str(BIN / "les_query"), "--out-dir", str(cfg["index"]),
               "--queries", str(d / "query.bin"), "--workload", str(d / "workload.txt"),
               "--mode", "subtree", "--k", str(K), "--ef", str(ef),
               "--repeats", str(repeats), "--out", str(ids), "--timings", str(tim)]
    else:
        env.update(GRAPH_SHARE_VECTORS="1", GRAPH_QUERY_ORDER="original", GRAPH_EF=str(ef))
        cmd = [str(BIN / "candidate"), "query", "--base", str(FIX / "base.f32"),
               "--tree", str(FIX / "tree.txt"), "--meta", str(d / "meta.txt"),
               "--index-dir", str(cfg["index"]), "--queries", str(d / "test.f32"),
               "--workload", str(d / "workload.txt"), "--mode", "subtree",
               "--out", str(ids), "--timings", str(tim), "--repeats", str(repeats)]
    log = logdir / f"{tag}_{ef}.log"
    with log.open("w") as f:
        p = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"{method} {split} ef{ef} failed: {log}")
    return ids, tim


def load_split(split: str):
    d = SPLITS / split
    meta = json.loads((SPLITS / "manifest.json").read_text())
    nq = meta["splits"][split]["count"]
    n = int((FIX / "meta.txt").read_text().split()[0])
    width = (n + 7) // 8
    gt = np.fromfile(d / "gt.bin", dtype=np.int32).reshape(nq, K)
    bm = np.fromfile(d / "bitmaps.bv", dtype=np.uint8).reshape(nq, width)
    sizes = np.unpackbits(bm, axis=1, count=n, bitorder="little").sum(axis=1)
    return nq, n, gt, bm, sizes


def score_run(ids_path: Path, nq: int, n: int, gt: np.ndarray, width: int):
    ids = np.fromfile(ids_path, dtype=np.int32).reshape(nq, K)
    hits = np.zeros(nq, dtype=np.int64)
    viol = 0
    for i in range(nq):
        pred = set(int(x) for x in ids[i] if x >= 0)
        hits[i] = len(pred & set(int(x) for x in gt[i]))
        viol += sum(1 for x in pred if x < 0 or x >= n)
    return hits, viol


def write_policy(path: Path, method: str, ef_by_bucket, fallback: int):
    lines = [f"method {'les' if method == 'les' else 'psg'}", f"k {K}",
             f"fallback {fallback}"]
    for b, ef in enumerate(ef_by_bucket):
        lines.append(f"ef {b} {ef}")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", default="b1_20261020")
    ap.add_argument("--methods", default="les,psg_m16,psg_m12")
    args = ap.parse_args()
    outroot = EXPS / "experiments/b1" / args.run
    polroot = EXPS / "data/b1_policies" / args.run
    outroot.mkdir(parents=True, exist_ok=True)
    polroot.mkdir(parents=True, exist_ok=True)
    fit_nq, n, fit_gt, fit_bm, fit_sizes = load_split("fit")
    val_nq, _, val_gt, val_bm, _ = load_split("val")
    width = fit_bm.shape[1]
    report = {}
    for method in args.methods.split(","):
        tag = method
        d = outroot / method
        d.mkdir(exist_ok=True)
        # sweep on fit
        H = np.zeros((5, len(EFS)), dtype=np.int64)
        T = np.zeros((5, len(EFS)), dtype=np.float64)
        for j, ef in enumerate(EFS):
            ids, tim = run(method, "fit", ef, tag, d)
            hits, _ = score_run(ids, fit_nq, n, fit_gt, width)
            t = np.fromfile(tim, dtype=np.float64) / 1000.0
            for b in range(5):
                m = np.array([bucket_of(int(s)) for s in fit_sizes]) == b
                H[b, j] = hits[m].sum()
                T[b, j] = t[m].sum()
        nqk = fit_nq * K
        method_report = {"nq": fit_nq, "efs": EFS, "H": H.tolist(), "T": T.tolist(),
                         "frozen": {}}
        for group, targets in (("r95", (0.95, 0.952, 0.955)),
                               ("r98", (0.98, 0.982, 0.985))):
            candidates = []
            for tgt in targets:
                need = tgt * nqk
                best = None
                for combo in itertools.product(range(len(EFS)), repeat=5):
                    hits = sum(H[b, combo[b]] for b in range(5))
                    if hits < need:
                        continue
                    cost = sum(T[b, combo[b]] for b in range(5))
                    if best is None or cost < best[0]:
                        best = (cost, combo)
                if best is None:
                    continue
                cost, combo = best
                efs = [EFS[j] for j in combo]
                p = polroot / f"policy_{method}_{group}_cand{tgt}.txt"
                write_policy(p, method, efs, max(efs))
                candidates.append({"target": tgt, "efs": efs, "fit_cost": cost,
                                   "policy": str(p)})
            # validate candidates, choose fastest passing
            chosen = None
            for c in candidates:
                if METHODS[method]["kind"] == "les":
                    cmd = [str(BIN / "les_query"), "--out-dir", str(METHODS[method]["index"]),
                           "--queries", str(SPLITS / "val/query.bin"),
                           "--workload", str(SPLITS / "val/workload.txt"),
                           "--mode", "subtree", "--k", str(K), "--ef", "24",
                           "--ef-policy", c["policy"], "--repeats", "1",
                           "--out", str(d / f"val_{c['target']}.ids"),
                           "--timings", str(d / f"val_{c['target']}.t")]
                    env = {**os.environ, "OMP_NUM_THREADS": "1"}
                else:
                    cmd = [str(BIN / "candidate"), "query", "--base", str(FIX / "base.f32"),
                           "--tree", str(FIX / "tree.txt"), "--meta", str(SPLITS / "val/meta.txt"),
                           "--index-dir", str(METHODS[method]["index"]),
                           "--queries", str(SPLITS / "val/test.f32"),
                           "--workload", str(SPLITS / "val/workload.txt"), "--mode", "subtree",
                           "--out", str(d / f"val_{c['target']}.ids"),
                           "--timings", str(d / f"val_{c['target']}.t"), "--repeats", "1"]
                    env = {**os.environ, "OMP_NUM_THREADS": "1", "GRAPH_SHARE_VECTORS": "1",
                           "GRAPH_QUERY_ORDER": "original", "GRAPH_EF_POLICY": c["policy"]}
                log = d / f"val_{c['target']}.log"
                with log.open("w") as f:
                    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
                if r.returncode:
                    c["val"] = {"status": "failed"}
                    continue
                hits, viol = score_run(d / f"val_{c['target']}.ids", val_nq, n, val_gt, width)
                rec = hits.sum() / (val_nq * K)
                wall = float([l.split()[1] for l in log.read_text().splitlines()
                              if l.startswith("BATCH_WALL_SECONDS")][0])
                c["val"] = {"recall": rec, "qps": val_nq / wall, "violations": viol,
                            "status": "ok" if rec >= c["target"] - 1e-12 else "below_target"}
                if c["val"]["status"] == "ok" and (chosen is None or c["val"]["qps"] > chosen["val"]["qps"]):
                    chosen = c
            if chosen:
                frozen = polroot / f"policy_{method}_{group}.txt"
                frozen.write_text(Path(chosen["policy"]).read_text())
                method_report["frozen"][group] = {"policy": str(frozen), **chosen}
            else:
                method_report["frozen"][group] = {"status": "no_val_candidate"}
        report[method] = method_report
        print(method, "frozen", {g: v.get("policy", v.get("status"))
                                 for g, v in method_report["frozen"].items()})
    (outroot / "calibration.json").write_text(json.dumps(report, indent=2) + "\n")
    print("wrote", outroot / "calibration.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
