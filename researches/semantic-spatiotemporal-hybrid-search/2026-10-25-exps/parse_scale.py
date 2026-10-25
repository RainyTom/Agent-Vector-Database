#!/usr/bin/env python3
"""Aggregate bench per-query CSVs for the window/space sensitivity experiment.

Reads work/bench_out_<workload>/{e1_time_first,b2_space_first,catalog}.csv and
writes data/scale/summary.csv. Purely derived from files the binary wrote.
"""
import csv, os, statistics, sys

WORK = os.path.join(os.path.dirname(__file__), "../../../production/semantic-mobility-search/work")
WORK = os.path.normpath(WORK)

FILES = {
    "gt(e1_time_first)": "e1_time_first.csv",
    "space(space_first)": "b2_space_first.csv",
    "catalog(topc300,cap48h)": "catalog.csv",
}
WORKLOADS = [
    ("all6_2160h_s50", "2160 h (90 d)", "50 km"),
    ("all6_8760h_s10", "8760 h (1 y)", "10 km"),
    ("all6_8760h", "8760 h (1 y)", "none"),
]


def p95(v):
    v = sorted(v)
    if not v:
        return 0.0
    i = min(len(v) - 1, int(0.95 * len(v)))
    return v[i]


def agg(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    us = [float(r["time_us"]) for r in rows]
    rec = [float(r["recall"]) for r in rows]
    cand = [float(r["cand_users"]) for r in rows]
    ver = [float(r["verified"]) for r in rows]
    dtw = [float(r["dtw"]) for r in rows]
    hits = [int(r["hits"]) for r in rows]
    mean_us = statistics.fmean(us) if us else 0.0
    return {
        "n": len(rows),
        "recall": statistics.fmean(rec) if rec else 0.0,
        "mean_ms": mean_us / 1000.0,
        "p95_ms": p95(us) / 1000.0,
        "qps": (1000.0 / mean_us) if mean_us else 0.0,
        "mean_cand": statistics.fmean(cand) if cand else 0.0,
        "mean_ver": statistics.fmean(ver) if ver else 0.0,
        "mean_dtw": statistics.fmean(dtw) if dtw else 0.0,
        "mean_hits": statistics.fmean(hits) if hits else 0.0,
        "empty": sum(1 for h in hits if h == 0),
    }


def main():
    out_csv = os.path.join(os.path.dirname(__file__), "data/scale/summary.csv")
    fields = ["workload", "window", "spatial", "method", "n", "recall_at_10",
              "mean_ms", "p95_ms", "qps", "mean_candidates", "mean_verified", "mean_dtw_computed",
              "mean_hits", "empty_queries", "source_csv"]
    rows = []
    for wl, window, spatial in WORKLOADS:
        d = os.path.join(WORK, "bench_out_" + wl)
        for method, fn in FILES.items():
            p = os.path.join(d, fn)
            if not os.path.exists(p):
                rows.append({"workload": wl, "window": window, "spatial": spatial,
                             "method": method, "n": 0, "recall_at_10": "",
                             "mean_ms": "", "p95_ms": "", "qps": "",
                             "mean_candidates": "", "mean_verified": "", "mean_dtw_computed": "",
                             "mean_hits": "", "empty_queries": "",
                             "source_csv": "TIMEOUT/no output: " + os.path.basename(d) + "/" + fn})
                continue
            a = agg(p)
            rows.append({
                "workload": wl, "window": window, "spatial": spatial, "method": method,
                "n": a["n"], "recall_at_10": f'{a["recall"]:.4f}',
                "mean_ms": f'{a["mean_ms"]:.3f}', "p95_ms": f'{a["p95_ms"]:.3f}',
                "qps": f'{a["qps"]:.2f}', "mean_candidates": f'{a["mean_cand"]:.1f}',
                "mean_verified": f'{a["mean_ver"]:.1f}',
                "mean_dtw_computed": f'{a["mean_dtw"]:.1f}',
                "mean_hits": f'{a["mean_hits"]:.2f}', "empty_queries": a["empty"],
                "source_csv": os.path.join(os.path.basename(d), fn),
            })
    os.makedirs(os.path.dirname(out_csv), exist_ok=True)
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    print("wrote", out_csv, "rows", len(rows))


if __name__ == "__main__":
    main()
