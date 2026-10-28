#!/usr/bin/env python3
"""Aggregate nodeann_research run dirs across any number of driver invocations.

Reads <run_root>/<dataset>/<label>/run.summary.json + run.perquery.csv from one
or more run roots, recomputes recall against each dataset's b3_exact GT (same
run root preferred, falls back to another root), and writes one median-per-cell
table. This makes the driver's own summary.csv (per-invocation, clobbered)
irrelevant: run dirs are the source of truth.

Labels: b3_exact, b0_ef<E>_rep<R>, b1_ef<E>_rep<R>.
"""
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

K = 10


def parse_ids(s):
    return [int(t) for t in s.replace(";", " ").split() if int(t) >= 0]


def load_gt(run_root, ds):
    p = run_root / ds / "b3_exact" / "run.perquery.csv"
    if not p.is_file():
        return None
    gt = {}
    with open(p) as f:
        for r in csv.DictReader(f):
            gt[int(r["query_index"])] = set(parse_ids(r["result_ids"]))
    return gt


def collect(run_roots):
    """rows keyed by (dataset, method, ef, rep)."""
    gts = {}
    cells = {}
    for root_idx, root in enumerate(run_roots):
        root = Path(root)
        runs = root  # root points at .../runs
        if not runs.is_dir():
            continue
        for ds_dir in sorted(runs.iterdir()):
            if not ds_dir.is_dir():
                continue
            ds = ds_dir.name
            gts.setdefault(ds, load_gt(root, ds))
            for label_dir in sorted(ds_dir.iterdir()):
                label = label_dir.name
                if label == "b3_exact":
                    method, ef, rep = "b3", 0, 0
                else:
                    try:
                        method = label.split("_")[0]
                        ef = int(label.split("_")[1][2:])
                        rep = int(label.split("_")[2][3:])
                    except (IndexError, ValueError):
                        continue
                sj = label_dir / "run.summary.json"
                pq = label_dir / "run.perquery.csv"
                if not (sj.is_file() and pq.is_file()):
                    continue
                try:
                    s = json.loads(sj.read_text())
                except json.JSONDecodeError:
                    continue
                rows = list(csv.DictReader(open(pq)))
                gt = gts.get(ds)
                macro_rec, macro_non = [], []
                hits_total = gt_total = 0
                if gt:
                    for r in rows:
                        qi = int(r["query_index"])
                        got = set(parse_ids(r["result_ids"]))
                        ref = gt.get(qi, set())
                        h = len(got & ref)
                        hits_total += h
                        gt_total += len(ref)
                        if ref:
                            (macro_rec if r["recursive"] == "1" else macro_non).append(h / len(ref))
                key = (ds, method, ef, rep)
                # later roots override earlier ones for the same cell
                cells[key] = {
                    "_src": root_idx,
                    "dataset": ds, "method": method, "ef": ef, "rep": rep,
                    "mean_e2e_ms": float(s.get("mean_e2e_ms", "nan")),
                    "p50_e2e_ms": float(s.get("p50_e2e_ms", "nan")),
                    "p95_e2e_ms": float(s.get("p95_e2e_ms", "nan")),
                    "p99_e2e_ms": float(s.get("p99_e2e_ms", "nan")),
                    "build_wall_s": float(s.get("build_wall_s", "nan")),
                    "build_cpu_s": float(s.get("build_cpu_s", "nan")),
                    "peak_rss_bytes": float(s.get("peak_rss_bytes", "nan")),
                    "serialized_bytes": s.get("serialized_bytes"),
                    "total_distance_calls": s.get("total_distance_calls"),
                    "total_hnsw_hops": s.get("total_hnsw_hops"),
                    "macro_recall": statistics.mean(macro_rec) if macro_rec else 1.0,
                    "macro_recall_nonrec": statistics.mean(macro_non) if macro_non else 1.0,
                    "global_recall": hits_total / gt_total if gt_total else 1.0,
                    "mtime": sj.stat().st_mtime,
                }
    # per (dataset,method,ef) cell: if any run comes from a later (preferred)
    # root, keep ONLY runs from the newest root that has this cell, so clean
    # single-rep measurements are never averaged with contended ones.
    best_src = {}
    for (ds, method, ef, rep), v in cells.items():
        ck = (ds, method, ef)
        best_src[ck] = max(best_src.get(ck, -1), v["_src"])
    cells = {k: v for k, v in cells.items()
             if v["_src"] == best_src[(k[0], k[1], k[2])]}
    return cells


def main():
    # args: out.csv run_root1 [run_root2 ...]
    out = Path(sys.argv[1])
    run_roots = [Path(p) for p in sys.argv[2:]]
    cells = collect(run_roots)
    groups = defaultdict(list)
    for (ds, method, ef, rep), v in cells.items():
        groups[(ds, method, ef)].append(v)
    out.parent.mkdir(parents=True, exist_ok=True)
    fields = ["dataset", "method", "ef", "n_reps", "source", "median_mean_e2e_ms",
              "median_p50_e2e_ms", "median_p95_e2e_ms", "median_p99_e2e_ms",
              "min_macro_recall", "median_macro_recall",
              "median_macro_recall_nonrec", "median_global_recall",
              "median_build_wall_s", "median_build_cpu_s",
              "median_peak_rss_bytes", "median_total_distance_calls",
              "median_total_hnsw_hops", "max_mean_e2e_ms", "rep_spread_x"]
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for (ds, method, ef), vs in sorted(groups.items(),
                                           key=lambda kv: (kv[0][0], kv[0][1], kv[0][2])):
            med = lambda k: statistics.median(v[k] for v in vs)
            means = [v["mean_e2e_ms"] for v in vs]
            spread = max(means) / min(means) if min(means) > 0 else float("inf")
            w.writerow({
                "dataset": ds, "method": method, "ef": ef, "n_reps": len(vs),
                "source": "clean" if vs[0]["_src"] > 0 else "pass1",
                "median_mean_e2e_ms": f"{med('mean_e2e_ms'):.6f}",
                "median_p50_e2e_ms": f"{med('p50_e2e_ms'):.6f}",
                "median_p95_e2e_ms": f"{med('p95_e2e_ms'):.6f}",
                "median_p99_e2e_ms": f"{med('p99_e2e_ms'):.6f}",
                "min_macro_recall": f"{min(v['macro_recall'] for v in vs):.6f}",
                "median_macro_recall": f"{med('macro_recall'):.6f}",
                "median_macro_recall_nonrec": f"{med('macro_recall_nonrec'):.6f}",
                "median_global_recall": f"{med('global_recall'):.6f}",
                "median_build_wall_s": f"{med('build_wall_s'):.3f}",
                "median_build_cpu_s": f"{med('build_cpu_s'):.3f}",
                "median_peak_rss_bytes": f"{med('peak_rss_bytes'):.0f}",
                "median_total_distance_calls": f"{statistics.median(float(v['total_distance_calls'] or 0) for v in vs):.0f}",
                "median_total_hnsw_hops": f"{statistics.median(float(v['total_hnsw_hops'] or 0) for v in vs):.0f}",
                "max_mean_e2e_ms": f"{max(means):.6f}",
                "rep_spread_x": f"{spread:.2f}",
            })
    print(f"wrote {out} ({len(groups)} cells from {len(cells)} runs)")


if __name__ == "__main__":
    main()
