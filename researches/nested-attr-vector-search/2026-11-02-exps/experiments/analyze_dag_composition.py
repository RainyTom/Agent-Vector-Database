#!/usr/bin/env python3
"""Summarise the DAG composition benchmark.

Inputs  (written by run_dag_composition.sh):
  variants.csv  per-variant x ef x query search cost and quality
  arms.csv      unified-ef arm comparison (merged / nodeann / global arms)
  kernels.csv   qualification-kernel microbenchmarks
  scope.csv     scope-resolution masks, subtree enumeration vs bitmap OR
Output: markdown tables on stdout; no files are written.
"""
import argparse
import csv
import statistics as st
from collections import defaultdict
from pathlib import Path


def rows(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def group(rs, *keys):
    out = defaultdict(list)
    for r in rs:
        out[tuple(r[k] for k in keys)].append(r)
    return out


def mean_per_query(rs, ef):
    """Average over non-empty queries of the per-query metric."""
    sel = [r for r in rs if int(r["ef"]) == ef]
    return sel


def recall_of(rs):
    return sum(int(r["hits"]) / int(r["truth"]) for r in rs) / len(rs)


def lat_of(rs):
    return sum(float(r["latency_us"]) for r in rs) / len(rs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="work/dag-composition")
    ap.add_argument("--ef", default=None, help="ef used for the detail tables")
    a = ap.parse_args()
    d = Path(a.dir)

    v = rows(d / "variants.csv")
    efs = sorted({int(r["ef"]) for r in v})
    print("## 1. Composition variants (merged graph, same ef axis)\n")
    print("| variant | degree | local / connector / jump | " +
          " | ".join(f"R@10 / µs (ef={e})" for e in efs) + " | gate µs | max-R |")
    print("|---|---:|---:|" + "---:|" * len(efs) + "---:|---:|")
    order = []
    for (name,), rs in group(v, "variant").items():
        order.append((name, rs))
    for name, rs in order:
        deg = float(rs[0]["degree_mean"])
        local, conn, jump = rs[0]["local_edges"], rs[0]["connector_edges"], rs[0]["jump_edges"]
        cells, best, maxr = [], float("inf"), 0.0
        for e in efs:
            sel = [r for r in rs if int(r["ef"]) == e]
            r_, l_ = recall_of(sel), lat_of(sel)
            maxr = max(maxr, r_)
            if r_ >= 0.95 and l_ < best:
                best = l_
            cells.append(f"{r_:.4f} / {l_:.1f}")
        gate = f"{best:.1f}" if best < float("inf") else "—"
        print(f"| {name} | {deg:.2f} | {local} / {conn} / {jump} | " +
              " | ".join(cells) + f" | {gate} | {maxr:.4f} |")

    print("\n### 1b. Measurement noise control\n")
    print("`baseline`, `order_size_asc` and `order_size_desc` build byte-identical graphs "
          "(same edge counts, same degree). Their spread is the timing noise band; "
          "differences inside it are not interpretable.\n")
    for name in ("baseline", "order_size_asc", "order_size_desc"):
        rs = [r for r in v if r["variant"] == name]
        gates = []
        for e in efs:
            sel = [r for r in rs if int(r["ef"]) == e]
            if recall_of(sel) >= 0.95:
                gates.append(lat_of(sel))
        print(f"- {name}: gate µs = {gates[0]:.1f}")

    print("\n## 2. Per-ef cost structure (baseline variant)\n")
    base = [r for r in v if r["variant"] == "baseline"]
    print("| ef | R@10 | µs | distances/q | expansions/q | rejected edges/q | duplicate edges/q |")
    print("|---:|---:|---:|---:|---:|---:|---:|")
    for e in efs:
        sel = [r for r in base if int(r["ef"]) == e]
        print(f"| {e} | {recall_of(sel):.4f} | {lat_of(sel):.1f} | "
              f"{sum(int(r['distances']) for r in sel)/len(sel):.1f} | "
              f"{sum(int(r['expansions']) for r in sel)/len(sel):.1f} | "
              f"{sum(int(r['rejected_edges']) for r in sel)/len(sel):.1f} | "
              f"{sum(int(r['duplicate_edges']) for r in sel)/len(sel):.1f} |")

    arms_path = d / "arms.csv"
    if arms_path.exists():
        ar = rows(arms_path)
        names = []
        for r in ar:
            if r["arm"] not in names:
                names.append(r["arm"])
        print("\n## 3. Unified-ef arms (one ef number per row)\n")
        print("| arm | ef | ef_local | R@10 | µs | distances/q | ns/distance | graphs/q | bitmap build µs |")
        print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
        for name in names:
            for e in efs:
                sel = [r for r in ar if r["arm"] == name and int(r["ef"]) == e]
                if not sel:
                    continue
                dist = sum(int(r["distances"]) for r in sel) / len(sel)
                lat = lat_of(sel)
                print(f"| {name} | {e} | {sel[0]['ef_local']} | {recall_of(sel):.4f} | "
                      f"{lat:.1f} | {dist:.1f} | {lat * 1000 / max(dist, 1e-9):.1f} | "
                      f"{st.mean(int(r['graphs']) for r in sel):.1f} | "
                      f"{st.mean(float(r.get('bitmap_build_us', 0)) for r in sel):.1f} |")
        print("\n### 3b. Matched-quality comparison (R >= 0.95)\n")
        print("| arm | ef | R@10 | µs | distances/q |")
        print("|---|---:|---:|---:|---:|")
        for name in names:
            best = None
            for e in efs:
                sel = [r for r in ar if r["arm"] == name and int(r["ef"]) == e]
                if not sel:
                    continue
                r_, l_ = recall_of(sel), lat_of(sel)
                if r_ >= 0.95 and (best is None or l_ < best[2]):
                    best = (e, r_, l_, sum(int(x["distances"]) for x in sel)/len(sel))
            if best:
                print(f"| {name} | {best[0]} | {best[1]:.4f} | {best[2]:.1f} | {best[3]:.1f} |")
            else:
                e = efs[-1]
                sel = [r for r in ar if r["arm"] == name and int(r["ef"]) == e]
                print(f"| {name} | — | max {recall_of(sel):.4f} | — | — |")

    k = d / "kernels.csv"
    if k.exists():
        print("\n## 4. Qualification kernels\n")
        print("| kernel | pattern | checks | build µs | ns/check |")
        print("|---|---|---:|---:|---:|")
        for r in rows(k):
            print(f"| {r['kernel']} | {r['pattern']} | {r['checks']} | {r['build_us']} | "
                  f"{float(r['ns_per_check']):.3f} |")

    s = d / "scope.csv"
    if s.exists():
        print("\n## 5. Scope masks: subtree enumeration vs bitmap OR\n")
        print("| query | dirs | enumerated writes | word ORs | enumerate µs | bitmap µs | mismatch |")
        print("|---:|---:|---:|---:|---:|---:|---:|")
        for r in rows(s):
            print(f"| {r['query']} | {r['dirs']} | {r['enumerated_writes']} | {r['word_ors']} | "
                  f"{float(r['enumerate_us']):.2f} | {float(r['bitmap_us']):.2f} | {r['mismatch']} |")


if __name__ == "__main__":
    main()
