#!/usr/bin/env python3
"""Add matched-recall operating points on an existing run_five_l2 run.

Reuses the built PSG/LES/FAISS artifacts of one run directory and only
re-runs query loops at additional search breadths. Writes a supplement CSV
next to the run's main CSV; the main CSV is untouched.

Example:
  run_five_l2_sweep.py --run 20261020T104139Z-68414a0f \
      --datasets sift100k --psg-ef 32,48 --les-ef 16,24 --faiss-ef 512,768
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_five_l2 import EXPS, NAMES, PRODUCTION, ROOT, THREAD_ENV, score  # noqa: E402

K = 10


def run(cmd, env, log):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as stream:
        proc = subprocess.run(cmd, cwd=ROOT, env={**os.environ, **THREAD_ENV, **env},
                              stdout=stream, stderr=subprocess.STDOUT, check=False)
    return proc.returncode


def seconds_from(log):
    for line in log.read_text().splitlines():
        if line.startswith("QUERY_SECONDS "):
            return float(line.split()[1])
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", required=True)
    ap.add_argument("--datasets", default="sift25k,sift50k,sift100k")
    ap.add_argument("--psg-ef", default="")
    ap.add_argument("--les-ef", default="")
    ap.add_argument("--faiss-ef", default="")
    ap.add_argument("--pathseer-ef", default="")
    args = ap.parse_args()

    run_root = PRODUCTION / "work/five_l2/runs" / args.run
    fixture_root = PRODUCTION / "work/five_l2/fixtures"
    ps_root = PRODUCTION / "work/five_l2/pathseer"
    bin_dir = PRODUCTION / "work/bin"
    log_root = EXPS / "experiments/five_l2" / args.run / "sweep"
    ld_path = str(ROOT / "deps/PathSeer/faiss/build/faiss")
    out_rows, commands = [], []

    for name in [d for d in args.datasets.split(",") if d]:
        n, dim = NAMES[name]
        fixture, ps = fixture_root / name, ps_root / name
        raw = run_root / name / "raw"
        work = log_root / name
        golden = np.fromfile(fixture / "gt_sub.bin", dtype=np.int32).reshape(-1, K)
        nq = golden.shape[0]
        width = (n + 7) // 8
        bitmap = np.memmap(ps / "query/query_sub.bv", dtype=np.uint8, mode="r",
                           shape=(nq, width))
        graph = run_root / name / "bf_hnsw_index.faiss"

        def append(method, ef, ids, times, log, extra=None, seconds=None):
            ids_path, times_path = work / f"{method}_ef{ef}_ids.bin", work / f"{method}_ef{ef}_t.f64"
            ids_path.write_bytes(ids)
            times_path.write_bytes(times)
            s = seconds if seconds is not None else seconds_from(log)
            if s is None or s <= 0:
                raise RuntimeError(f"no QUERY_SECONDS in {log}")
            row = {"dataset": name, "method": method, "ef": ef, "n": n, "dim": dim,
                   "nq": nq, "k": K, "query_seconds": s, "qps": nq / s}
            row.update(score(ids_path, times_path, golden, bitmap, n, nq, K))
            row.update(extra or {})
            out_rows.append(row)
            print(f"{name:17}{method:12} ef={ef:<5} recall={row['recall_at_10']:.4f} "
                  f"qps={row['qps']:.0f}")

        for ef in [int(x) for x in args.psg_ef.split(",") if x]:
            log = work / f"psg_ef{ef}.log"
            ids_path, t_path = work / f"psg_ef{ef}.ids", work / f"psg_ef{ef}.t"
            cmd = [str(bin_dir / "candidate"), "query",
                   "--base", str(fixture / "base.f32"), "--tree", str(fixture / "tree.txt"),
                   "--meta", str(fixture / "meta.txt"), "--index-dir", str(run_root / name / "psg"),
                   "--queries", str(fixture / "test.f32"),
                   "--workload", str(fixture / "workload_sub.txt"), "--mode", "subtree",
                   "--out", str(ids_path), "--timings", str(t_path), "--repeats", "2"]
            commands.append({"argv": cmd, "GRAPH_EF": ef})
            if run(cmd, {"GRAPH_EF": str(ef)}, log):
                raise RuntimeError(f"psg ef={ef} failed: {log}")
            append("psg", ef, ids_path.read_bytes(), t_path.read_bytes(), log)

        for ef in [int(x) for x in args.les_ef.split(",") if x]:
            log = work / f"les_ef{ef}.log"
            ids_path, t_path = work / f"les_ef{ef}.ids", work / f"les_ef{ef}.t"
            cmd = [str(bin_dir / "les_query"), "--out-dir", str(run_root / name / "les"),
                   "--queries", str(ps / "query/query.bin"),
                   "--workload", str(fixture / "workload_sub.txt"), "--mode", "subtree",
                   "--k", str(K), "--ef", str(ef), "--repeats", "2",
                   "--out", str(ids_path), "--timings", str(t_path)]
            commands.append({"argv": cmd})
            if run(cmd, {}, log):
                raise RuntimeError(f"les ef={ef} failed: {log}")
            append("les", ef, ids_path.read_bytes(), t_path.read_bytes(), log)

        for method, ef_list in (("hnsw", args.faiss_ef), ("pathseer", args.pathseer_ef)):
            for ef in [int(x) for x in ef_list.split(",") if x]:
                tag = "bf_hnsw" if method == "hnsw" else "native_pathseer"
                log = work / f"{tag}_ef{ef}.log"
                jpath, ids_path, t_path = (work / f"{tag}_ef{ef}.json",
                                           work / f"{tag}_ef{ef}.ids", work / f"{tag}_ef{ef}.t")
                cmd = [str(bin_dir / "faiss_filtered"), "--dataset", str(ps),
                       "--method", method, "--mode", "sub", "--ef", str(ef),
                       "--M", "32", "--efc", "200", "--k", str(K), "--repeats", "2",
                       "--out", str(jpath), "--ids", str(ids_path), "--timings", str(t_path)]
                if method == "hnsw" and graph.is_file():
                    cmd += ["--index-in", str(graph)]
                if method == "pathseer":
                    cmd += ["--M-exp", "64", "--M-exp-search", "16"]
                commands.append({"argv": cmd})
                if run(cmd, {"LD_LIBRARY_PATH": ld_path}, log):
                    raise RuntimeError(f"{tag} ef={ef} failed: {log}")
                meta = json.loads(jpath.read_text())
                append(tag, ef, ids_path.read_bytes(), t_path.read_bytes(), log,
                       {"index_bytes": meta["index_bytes"], "build_s": meta["build_s"]},
                       seconds=float(meta["query_seconds"]))

    out = EXPS / "data" / f"five_l2_sweep_{args.run}.csv"
    fields = ["dataset", "method", "ef", "n", "dim", "nq", "k", "recall_at_10",
              "violations", "feasibility_violations", "duplicate_ids", "invalid_ids",
              "full_k_queries", "empty_queries", "query_seconds", "qps", "p95_ms",
              "index_bytes", "build_s", "ids_path", "timings_path"]
    with out.open("w", newline="") as stream:
        w = csv.DictWriter(stream, fieldnames=fields)
        w.writeheader()
        for row in out_rows:
            w.writerow({k: row.get(k) for k in fields})
    (out.with_suffix(".json")).write_text(json.dumps(
        {"run": args.run, "commands": commands}, indent=2) + "\n")
    print("wrote", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
