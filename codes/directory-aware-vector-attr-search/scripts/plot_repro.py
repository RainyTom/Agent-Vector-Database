#!/usr/bin/env python3
"""Figures for the Directory-Aware (arXiv:2606.16903) reproduction.

Reads the reproduction CSVs and writes figures next to the report:
  fig_repro_dir_latency.{png,svg}   — Table IV analog: directory-only latency
  fig_repro_ndcg_buckets.{png,svg}  — wiki nDCG by scope cardinality bucket
  fig_repro_dsm.{png,svg}           — Fig. 9 analog: DSM wall-clock latency
  fig_repro_dir_size.{png,svg}      — directory-module size vs PE-Online
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

DESIGNS = ["PE-Online", "PE-Offline", "TrieHI", "NODE"]
COLORS = {"PE-Online": "tab:blue", "PE-Offline": "tab:orange", "TrieHI": "tab:green",
          "NODE": "tab:purple"}


def load(path: Path) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def save(fig, out: Path, name: str):
    for ext in ("png", "svg"):
        fig.savefig(out / f"{name}.{ext}", bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out}/{name}.{{png,svg}}")


def fig_dir_latency(rows, out: Path):
    groups = [("wiki", "recursive"), ("wiki", "non-recursive"),
              ("arxiv", "recursive"), ("arxiv", "non-recursive")]
    fig, ax = plt.subplots(figsize=(8, 4.5))
    width = 0.26
    xticks, xlabels = [], []
    for gi, (ds, qt) in enumerate(groups):
        for di, d in enumerate(DESIGNS):
            r = next((x for x in rows
                      if x["dataset"] == ds and x["design"] == d
                      and x["query_type"] == qt and x["scope_bucket"] == "all"
                      and x["executor"].startswith("PG")), None)
            if r is None:
                continue
            v = float(r["dir_mean_us"])
            x = gi + (di - 1) * width
            ax.bar(x, v, width, color=COLORS[d], label=d if gi == 0 else None)
            ax.text(x, v * 1.15, f"{v:.0f}" if v >= 10 else f"{v:.1f}",
                    ha="center", fontsize=7)
        xticks.append(gi)
        xlabels.append(f"{ds}\n{qt}")
    ax.set_yscale("log")
    ax.set_xticks(xticks)
    ax.set_xticklabels(xlabels)
    ax.set_ylabel("directory-only latency (µs, mean, log)")
    ax.set_title("DSQ scope resolution: PE-Online expansion vs materialized sets")
    ax.legend()
    ax.grid(True, axis="y", which="both", alpha=0.3)
    save(fig, out, "fig_repro_dir_latency")


def fig_ndcg_buckets(rows, out: Path):
    buckets = ["card<10", "card10-1000", "card>=1000"]
    execs = ["PG-hnsw-M16", "IVF-nlist1024"]
    fig, ax = plt.subplots(figsize=(7, 4.5))
    width = 0.35
    for ei, ex in enumerate(execs):
        vals = []
        for b in buckets:
            r = next(x for x in rows
                     if x["dataset"] == "wiki" and x["design"] == "TrieHI"
                     and x["query_type"] == "recursive"
                     and x["scope_bucket"] == b and x["executor"] == ex)
            vals.append(float(r["ndcg10"]))
        xs = [i + (ei - 0.5) * width for i in range(len(buckets))]
        ax.bar(xs, vals, width, label=ex, color=["tab:blue", "tab:red"][ei])
        for x, v in zip(xs, vals):
            ax.text(x, v + 0.02, f"{v:.2f}", ha="center", fontsize=8)
    ax.set_ylim(0, 1.1)
    ax.set_xticks(range(len(buckets)))
    ax.set_xticklabels(buckets)
    ax.set_ylabel("nDCG@10 (wiki, recursive)")
    ax.set_title("Filtered-ANN quality collapses only at extreme low selectivity")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    save(fig, out, "fig_repro_ndcg_buckets")


def fig_dsm(rows, out: Path):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    width = 0.26
    for di, d in enumerate(DESIGNS):
        for oi, op in enumerate(["MOVE", "MERGE"]):
            r = next((x for x in rows if x["design"] == d and x["op"] == op), None)
            if r is None:
                continue
            x = oi + (di - 1) * width
            ax.bar(x, float(r["mean_ms"]), width, color=COLORS[d],
                   label=d if oi == 0 else None)
            ax.text(x, float(r["mean_ms"]) * 1.15,
                    f"{float(r['mean_ms']):.2f}", ha="center", fontsize=8)
    top = max((float(r["mean_ms"]) for r in rows), default=1.0) * 3
    ax.set_ylim(top=top)
    ax.set_yscale("log")
    ax.set_xticks([0, 1])
    ax.set_xticklabels(["MOVE", "MERGE"])
    ax.set_ylabel("wall-clock (ms, mean, log)")
    ax.set_title("DSM: TrieHI subtree relinking vs expansion-based rewriting")
    ax.legend()
    ax.grid(True, axis="y", which="both", alpha=0.3)
    save(fig, out, "fig_repro_dsm")


def fig_dir_size(rows, out: Path):
    fig, ax = plt.subplots(figsize=(6, 4))
    vals = []
    present = []
    for d in DESIGNS:
        # wiki module rows are the first three (largest)
        r = max((x for x in rows if x["design"] == d), key=lambda x: float(x["size_mb"]),
                default=None)
        if r is None:
            continue
        present.append(d)
        vals.append(float(r["size_mb"]))
    ax.bar(present, vals, color=[COLORS[d] for d in present])
    for i, v in enumerate(vals):
        ax.text(i, v + 0.05, f"{v:.2f}", ha="center", fontsize=9)
    ax.set_ylabel("directory module size (MB, serialized)")
    ax.set_title("Directory-module storage vs design (wiki namespace)")
    ax.grid(True, axis="y", alpha=0.3)
    save(fig, out, "fig_repro_dir_size")


# ----------------------------------------------------------- new figures ----
def fig7_tradeoff(rows, out: Path):
    """Fig 7 analog: recursive DSQ quality vs mean latency, 4 subplots."""
    panels = [("wiki", "PG-hnsw-M16"), ("wiki", "IVF-nlist1024"),
              ("arxiv", "PG-hnsw-M16"), ("arxiv", "IVF-nlist1024")]
    fig, axes = plt.subplots(1, 4, figsize=(20, 4.6))
    for ax, (ds, ex) in zip(axes, panels):
        metric = "ndcg10" if ds == "wiki" else "recall10"
        ylab = "nDCG@10" if ds == "wiki" else "Recall@100"
        any_data = False
        for d in DESIGNS:
            pts = [r for r in rows if r["dataset"] == ds and r["design"] == d
                   and r["executor"] == ex and r["query_type"] == "recursive"]
            pts.sort(key=lambda r: int(r["value"]))
            if not pts:
                continue
            any_data = True
            xs = [float(r["e2e_mean_ms"]) for r in pts]
            ys = [float(r[metric]) for r in pts]
            ax.plot(xs, ys, "-o", color=COLORS[d], label=d)
        if not any_data:
            ax.text(0.5, 0.5, "N/A\n(vectors unavailable:\nDrive quota / not released)",
                    ha="center", va="center", transform=ax.transAxes, fontsize=9,
                    color="gray")
        ax.set_xlabel("mean latency (ms)")
        ax.set_ylabel(ylab)
        ax.set_title(f"{ds}, {ex.split('-')[0]}")
        ax.grid(True, alpha=0.3)
    axes[0].legend()
    fig.suptitle("Fig 7 analog: recursive DSQ quality vs latency (ef/nprobe sweep)")
    fig.tight_layout()
    save(fig, out, "fig_repro_fig7_tradeoff")


def fig_pareto_frontier(rows, out: Path):
    """Pareto frontier, one figure per dataset (1x2: PG / IVF panels).

    x = Recall@10, y = mean latency (log). Three directory designs as lines;
    NodeANN drawn as a separate thicker dashed series (its own executor family,
    so it appears in both panels for reference). Only non-dominated points are
    connected (a point is dominated if another has >= recall and <= latency).
    """
    datasets = sorted({r["dataset"] for r in rows})
    for ds in datasets:
        panels = [("PG-hnsw-M16", "PG (HNSW)"), ("IVF-nlist1024", "IVF")]
        fig, axes = plt.subplots(1, 2, figsize=(13.5, 4.8), dpi=140)
        for ax, (ex, title) in zip(axes, panels):
            for d in DESIGNS:
                if d == "NODE":
                    if ds != "wiki":
                        continue
                    ex_name = "NODE-flat"
                    style = dict(linewidth=2.4, linestyle="--", marker="D", markersize=5)
                else:
                    ex_name = ex
                    style = dict(linewidth=1.8, marker="o", markersize=5)
                pts = [(float(r["recall10"]), float(r["e2e_mean_ms"]), r)
                       for r in rows
                       if r["dataset"] == ds and r["design"] == d
                       and r["executor"] == ex_name and r["query_type"] == "recursive"]
                if not pts:
                    continue
                frontier = [p for p in pts if not any(
                    q is not p and q[0] >= p[0] and q[1] <= p[1] for q in pts)]
                frontier.sort(key=lambda t: t[0])
                xs = [t[0] for t in frontier]
                ys = [t[1] for t in frontier]
                ax.plot(xs, ys, color=COLORS[d], label=d, zorder=3 if d != "NODE" else 4,
                        **style)
                # annotate the two extreme points with their knob value
                for idx in (0, len(frontier) - 1):
                    rec, lat, r = frontier[idx]
                    knob = r.get("value") or ""
                    knob2 = r.get("value2") or ""
                    txt = f"N={knob}" if d == "NODE" else f"{knob}"
                    if d == "NODE" and knob2:
                        txt += f"/L={knob2}"
                    ax.annotate(txt, (rec, lat), textcoords="offset points",
                                xytext=(4, 5), fontsize=8, color=COLORS[d])
            ax.set_xlabel("Recall@10", fontsize=11)
            ax.set_ylabel("mean end-to-end latency (ms, log)", fontsize=11)
            ax.set_yscale("log")
            ax.set_title(f"{ds} · {title}", fontsize=12)
            ax.grid(True, which="both", alpha=0.25)
            ax.tick_params(labelsize=10)
        axes[0].legend(fontsize=10, loc="best")
        note = ("NodeANN = per-node flat indexes (own executor family; shown for reference "
                "in both panels)")
        if ds == "arxiv":
            note = ("arxiv = second namespace of the synthetic fixture (subject + temporal, "
                    "same vectors); NodeANN v1 supports single-namespace datasets only")
        fig.text(0.5, -0.02, note, ha="center", fontsize=9, color="#666666")
        fig.tight_layout()
        save(fig, out, f"fig_pareto_{ds}_recall_latency")


def fig9_dsm_perop(rows, out: Path):
    """Fig 9 analog: per-operation MOVE/MERGE wall-clock latency."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.6))
    for ax, op in zip(axes, ["MOVE", "MERGE"]):
        for d in DESIGNS:
            pts = [(int(r["op_index"]), float(r["latency_ms"]))
                   for r in rows if r["design"] == d and r["op_kind"] == op]
            pts.sort()
            ax.plot([p[0] for p in pts], [p[1] for p in pts], ".", ms=3,
                    color=COLORS[d], label=d)
        ax.set_yscale("log")
        ax.set_xlabel("operation index")
        ax.set_ylabel("latency (ms, log)")
        ax.set_title(op)
        ax.grid(True, which="both", alpha=0.3)
    axes[0].legend()
    fig.suptitle("Fig 9 analog: DSM wall-clock latency per operation")
    fig.tight_layout()
    save(fig, out, "fig_repro_fig9_dsm_perop")


def fig10_structure(tree_dir: Path, out: Path):
    """Fig 10 analog: WIKI structural complexity by anchor path depth."""
    with open(tree_dir / "wiki_tree.txt", encoding="utf-8") as f:
        n = int(f.readline())
        parent = [0] * n
        for _ in range(n):
            v, p = map(int, f.readline().split())
            parent[v] = p
    children = [[] for _ in range(n)]
    depth = [0] * n
    for v in range(1, n):
        children[parent[v]].append(v)
        depth[v] = depth[parent[v]] + 1
    with open(tree_dir / "workload_wiki.txt", encoding="utf-8") as f:
        nq, _ = map(int, f.readline().split())
        anchors = [int(f.readline().split()[-1]) for _ in range(nq)]
    depths = sorted({depth[a] for a in anchors})
    n_anchor, m_exp, c_child = [], [], []
    for dpt in depths:
        ans = [a for a in anchors if depth[a] == dpt]
        n_anchor.append(len(ans))
        m_exp.append(sum(len(_subtree(a, children)) for a in ans) / len(ans))
        c_child.append(sum(len(children[a]) for a in ans) / len(ans))
    fig, ax = plt.subplots(figsize=(7, 4.5))
    w = 0.26
    xs = list(range(len(depths)))
    ax.bar([x - w for x in xs], n_anchor, w, label="input path")
    ax.bar(xs, m_exp, w, label="expanded sub-path (m)")
    ax.bar([x + w for x in xs], c_child, w, label="direct child-path (c)")
    ax.set_xticks(xs)
    ax.set_xticklabels([str(d) for d in depths])
    ax.set_xlabel("path depth")
    ax.set_ylabel("count")
    ax.set_title("Fig 10 analog: directory structural complexity by depth")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    save(fig, out, "fig_repro_fig10_structure")


def _subtree(u, children):
    out, st = [], [u]
    while st:
        x = st.pop()
        out.append(x)
        st.extend(children[x])
    return out


def fig11_depth(rows, out: Path):
    """Fig 11 analog: recursive DSQ latency and nDCG by anchor depth (wiki)."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.6))
    for ax, ex in zip(axes, ["PG-hnsw-M16", "IVF-nlist1024"]):
        ax2 = ax.twinx()
        for d in DESIGNS:
            agg = {}
            for r in rows:
                if not (r["dataset"] == "wiki" and r["design"] == d
                        and r["executor"] == ex and r["query_type"] == "recursive"):
                    continue
                agg.setdefault(int(r["anchor_depth"]), []).append(r)
            ds_pts = sorted(agg)
            lat = [sum(float(r["e2e_ms"]) for r in agg[x]) / len(agg[x]) for x in ds_pts]
            ndc = [sum(float(r["ndcg10"]) for r in agg[x]) / len(agg[x]) for x in ds_pts]
            ax.plot(ds_pts, lat, "-o", color=COLORS[d], label=f"{d} latency")
            ax2.plot(ds_pts, ndc, "--s", color=COLORS[d], alpha=0.45,
                     label=f"{d} nDCG")
        ax.set_xlabel("path depth")
        ax.set_ylabel("mean e2e latency (ms)")
        ax2.set_ylabel("nDCG@10")
        ax.set_title(f"wiki, {ex.split('-')[0]}")
        ax.grid(True, alpha=0.3)
    axes[0].legend(loc="upper left", fontsize=8)
    fig.suptitle("Fig 11 analog: depth sensitivity of recursive DSQ (wiki)")
    fig.tight_layout()
    save(fig, out, "fig_repro_fig11_depth")


def fig12_breakdown(rows, out: Path):
    """Fig 12 analog: directory-only latency decomposition by depth (wiki)."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.6))
    for ax, qt in zip(axes, ["recursive", "non-recursive"]):
        depths = sorted({int(r["anchor_depth"]) for r in rows
                         if r["dataset"] == "wiki" and r["query_type"] == qt})
        w = 0.26
        for di, d in enumerate(DESIGNS):
            enum_pct, bmp_pct = [], []
            for dp in depths:
                sel = [r for r in rows if r["dataset"] == "wiki" and r["design"] == d
                       and r["query_type"] == qt and int(r["anchor_depth"]) == dp]
                te = sum(float(r["enum_us"]) for r in sel)
                tb = sum(float(r["bitmap_us"]) for r in sel)
                tot = te + tb
                enum_pct.append(100 * te / tot if tot > 0 else 0)
                bmp_pct.append(100 * tb / tot if tot > 0 else 0)
            xs = [x + (di - 1) * w for x in range(len(depths))]
            ax.bar(xs, enum_pct, w, color=COLORS[d], alpha=0.55,
                   label=f"{d} enum" if qt == "recursive" else None)
            ax.bar(xs, bmp_pct, w, bottom=enum_pct, color=COLORS[d],
                   label=f"{d} bitmap" if qt == "recursive" else None)
        ax.set_xticks(range(len(depths)))
        ax.set_xticklabels([str(d) for d in depths])
        ax.set_xlabel("path depth")
        ax.set_ylabel("% of directory-only latency")
        ax.set_ylim(0, 100)
        ax.set_title(qt)
        ax.grid(True, axis="y", alpha=0.3)
    axes[0].legend(fontsize=8)
    fig.suptitle("Fig 12 analog: directory-only latency decomposition (wiki)")
    fig.tight_layout()
    save(fig, out, "fig_repro_fig12_breakdown")


def main():
    ap = argparse.ArgumentParser()
    repro = (Path(__file__).resolve().parents[3] / "researches" /
             "nested-attr-vector-search" / "01-directory-aware-acceleration" /
             "2026-10-26-triehi-reproduction")
    ap.add_argument("--data", type=Path, default=repro / "data")
    ap.add_argument("--out", type=Path, default=repro / "figures")
    ap.add_argument("--prefix", default="results_",
                    help="CSV filename prefix (e.g. real_wiki1m_ for real-data runs)")
    ap.add_argument("--prefix-dsm", default=None,
                    help="CSV prefix for DSM/perop files if different (scope-mode runs)")
    ap.add_argument("--tree-dir", type=Path,
                    default=Path(__file__).resolve().parents[1] / "data" / "sift100k")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    p = args.prefix
    pd = args.prefix_dsm or p
    dsq = load(args.data / f"{p}dsq.csv")
    dsm = load(args.data / f"{pd}dsm.csv")
    size = load(args.data / f"{p}size.csv")
    perop = load(args.data / f"{pd}dsm_perop.csv")
    perquery = load(args.data / f"{p}dsq_perquery.csv")
    scan = load(args.data / f"{p}dsq_scan.csv")
    fig_dir_latency(dsq, args.out)
    fig_ndcg_buckets(dsq, args.out)
    fig_dsm(dsm, args.out)
    fig_dir_size(size, args.out)
    if scan:
        fig7_tradeoff(scan, args.out)
        fig_pareto_frontier(scan, args.out)
    if perop:
        fig9_dsm_perop(perop, args.out)
    fig10_structure(args.tree_dir, args.out)
    if perquery:
        fig11_depth(perquery, args.out)
        fig12_breakdown(perquery, args.out)


if __name__ == "__main__":
    main()
