#!/usr/bin/env python3
"""Plot the 2026-10-29 NodeANN-first hnswlib matrix results.

Reads ONLY the new-segment outputs under work/nodeann_matrix/
(summary_agg.csv, summary.csv, perquery.csv, manifest.json) and writes
figures into <exps-root>/figures/. Legacy FAISS numbers are never mixed in.

Figures:
  fig1_recall_latency.png      recall–latency curves, one facet per dataset
                               (WIKI + SIFT D4/D6/D12 x balanced/skewed);
                               B0/B1 over the ef grid, B3 exact shown as a
                               diagnostic marker excluded from the frontier.
  fig2_perquery_breakdown.png  per-query defect decomposition: candidate
                               nodes / scanned+HNSW objects / merge offers
                               vs end-to-end latency (median + P95).
  fig3_build_rss_table.png     build wall/CPU, peak RSS, serialized bytes,
                               latency and recall per dataset x method.

Every figure is annotated with the hnswlib revision, the unified RawL2Space
distance contract, T_scan, the ef grid and the thread configuration taken
from work/nodeann_matrix/manifest.json.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

EXPS_ROOT = Path(__file__).resolve().parents[1]           # .../2026-10-29-exps
DEFAULT_WORK_DIR = EXPS_ROOT / "work" / "nodeann_matrix"
DEFAULT_FIGURES_DIR = EXPS_ROOT / "figures"

DATASET_ORDER = (["wiki"] +
                 [f"{d}_{l}" for d in ("d4", "d6", "d12")
                  for l in ("balanced", "skewed")])
METHOD_STYLE = {"b0": {"color": "#1f77b4", "marker": "o", "label": "B0 hnswlib NodeANN"},
                "b1": {"color": "#d62728", "marker": "s", "label": "B1 small-node scan + HNSW"}}
METHOD_SHORT = {"b0": "B0", "b1": "B1", "b3": "B3 exact"}


def read_csv(path: Path) -> list[dict]:
    if not path.is_file():
        raise SystemExit(f"ERROR: missing input {path}. Run "
                         f"experiments/run_nodeann_matrix.py first.")
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def fnum(v, default=None):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def caption_text(manifest: dict) -> str:
    th = manifest.get("threads", {})
    ts = manifest.get("tscan", {})
    rev = manifest.get("hnswlib_pinned_revision", "?")
    checkout = manifest.get("hnswlib_checkout_revision", "?")
    if checkout and checkout != rev:
        rev = f"{rev} (checkout {checkout})"
    return (
        f"hnswlib rev {rev} | unified RawL2Space (L2Space::get_dist_func) | "
        f"T_scan: d128={ts.get('d128', '?')} d1024={ts.get('d1024', '?')} "
        f"(frozen={ts.get('frozen', '?')}) | "
        f"ef grid={manifest.get('ef_grid', '?')} | "
        f"search threads=1, build threads={th.get('build_per_process', '?')} "
        f"(worker procs <= {th.get('max_worker_processes', '?')}) | "
        f"seed={manifest.get('seed', '?')} | "
        f"new hnswlib segment only - no legacy FAISS numbers"
    )


def load_manifest(work_dir: Path) -> dict:
    p = work_dir / "manifest.json"
    if p.is_file():
        with open(p) as f:
            return json.load(f)
    return {}


# --------------------------------------------------------------------------
# Fig 1: recall–latency curves
# --------------------------------------------------------------------------
def fig_recall_latency(agg, manifest, out_path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    by_ds = {}
    for r in agg:
        by_ds.setdefault(r["dataset"], []).append(r)

    fig, axes = plt.subplots(2, 4, figsize=(22, 10))
    for ax, ds in zip(axes.flat, DATASET_ORDER):
        rows = by_ds.get(ds, [])
        for method in ("b0", "b1"):
            pts = [(fnum(r["median_mean_e2e_ms"]), fnum(r["median_macro_recall"]),
                    fnum(r["median_global_recall"]), int(r["ef"]))
                   for r in rows if r["method"] == method]
            pts = sorted(p for p in pts if p[0] and p[1] is not None)
            if not pts:
                continue
            st = METHOD_STYLE[method]
            ax.plot([p[0] for p in pts], [p[1] for p in pts],
                    color=st["color"], marker=st["marker"], label=st["label"])
            ax.plot([p[0] for p in pts], [p[2] for p in pts], linestyle="--",
                    color=st["color"], alpha=0.45,
                    label=f"{method} global recall")
        b3 = [r for r in rows if r["method"] == "b3"]
        if b3:
            x = fnum(b3[0]["median_mean_e2e_ms"])
            if x:
                ax.axvline(x, color="gray", linestyle=":", linewidth=2)
                ax.annotate("B3 exact (diagnostic,\nnot frontier)",
                            xy=(x, 0.05), xytext=(x * 1.15, 0.12),
                            fontsize=8, color="gray")
        ax.set_xscale("log")
        # guard against degenerate/non-positive ranges (e.g. constant latency
        # collapsing xlim to a point would explode the log transform)
        lo, hi = ax.get_xlim()
        if not (lo > 0 and hi > lo):
            lo, hi = (min(lo, hi * 0.5), hi * 1.5) if hi > 0 else (0.1, 1.0)
            if not lo > 0:
                lo = hi * 0.5 if hi > 0 else 0.1
            if not hi > lo:
                hi = lo * 2.0
        else:
            lo, hi = lo * 0.9, hi * 1.1
        ax.set_xlim(lo, hi)
        if hi / lo < 3.0:
            # narrow range: log auto-ticks degenerate into unreadable labels
            from matplotlib.ticker import MaxNLocator, ScalarFormatter
            ax.xaxis.set_major_locator(MaxNLocator(6))
            ax.xaxis.set_minor_locator(plt.NullLocator())
            fmt = ScalarFormatter()
            fmt.set_scientific(False)
            ax.xaxis.set_major_formatter(fmt)
        ax.set_title(ds, fontsize=11)
        ax.set_ylim(-0.02, 1.02)
        ax.grid(True, which="both", alpha=0.25)
        ax.set_xlabel("median mean e2e latency (ms, log)")
        ax.set_ylabel("recall@10")
    for ax in axes.flat[len(DATASET_ORDER):]:
        ax.axis("off")
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper right", fontsize=9)
    fig.suptitle("2026-10-29 NodeANN-first hnswlib matrix: recall–latency "
                 "(median of 3 process repeats)", fontsize=13)
    fig.text(0.01, 0.005, caption_text(manifest), fontsize=8, color="0.35")
    fig.tight_layout(rect=(0, 0.02, 1, 0.96))
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


# --------------------------------------------------------------------------
# Fig 2: per-query defect decomposition
# --------------------------------------------------------------------------
def fig_perquery_breakdown(perquery, manifest, out_path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    # aggregate over repeats: (dataset, method, ef) -> medians
    groups: dict[tuple, list[dict]] = {}
    for r in perquery:
        key = (r["dataset"], r["method"], r["ef"])
        if r["method"] in METHOD_STYLE:
            groups.setdefault(key, []).append(r)

    metrics = [
        ("candidate_nodes", "candidate nodes"),
        (None, "scanned + HNSW objects (scan_items + hnsw_items)"),
        ("heap_offers", "merge heap offers"),
    ]

    def med(rows, col):
        vals = sorted(fnum(r[col]) for r in rows if fnum(r[col]) is not None)
        n = len(vals)
        if n == 0:
            return None, None
        m = vals[n // 2] if n % 2 else (vals[n // 2 - 1] + vals[n // 2]) / 2
        p95 = vals[min(n - 1, int(0.95 * (n - 1)))]
        return m, p95

    fig, axes = plt.subplots(len(DATASET_ORDER), 3, figsize=(18, 3.2 * len(DATASET_ORDER)))
    for i, ds in enumerate(DATASET_ORDER):
        for j, (col, title) in enumerate(metrics):
            ax = axes[i][j]
            for method in ("b0", "b1"):
                pts = []
                for (d, mth, ef), rows in groups.items():
                    if d != ds or mth != method:
                        continue
                    if col is None:
                        rows = [dict(r, _objs=str(fnum(r["scan_items"], 0) +
                                                  fnum(r["hnsw_items"], 0)))
                                for r in rows]
                        m, p95 = med(rows, "_objs")
                    else:
                        m, p95 = med(rows, col)
                    x, _ = med(rows, "e2e_ms")
                    if x and m is not None:
                        pts.append((x, m, p95, int(ef)))
                pts.sort()
                if not pts:
                    continue
                st = METHOD_STYLE[method]
                ax.plot([p[0] for p in pts], [p[1] for p in pts],
                        color=st["color"], marker=st["marker"], label=METHOD_SHORT[method])
                ax.plot([p[0] for p in pts], [p[2] for p in pts], linestyle=":",
                        color=st["color"], alpha=0.5, label=f"{METHOD_SHORT[method]} P95")
            ax.set_xscale("log")
            lo, hi = ax.get_xlim()
            if not (lo > 0 and hi > lo):
                lo = 0.1 if hi <= 0 else min(x for x in (lo, hi) if x > 0) * 0.8
                hi = lo * 2.0 if hi <= lo else hi * 1.2
            else:
                lo, hi = lo * 0.9, hi * 1.1
            ax.set_xlim(lo, hi)
            if hi / lo < 3.0:
                from matplotlib.ticker import MaxNLocator, ScalarFormatter
                ax.xaxis.set_major_locator(MaxNLocator(6))
                ax.xaxis.set_minor_locator(plt.NullLocator())
                fmt = ScalarFormatter()
                fmt.set_scientific(False)
                ax.xaxis.set_major_formatter(fmt)
            if j == 0:
                ax.set_ylabel(ds, fontsize=10)
            if i == 0:
                ax.set_title(title, fontsize=10)
            if i == len(DATASET_ORDER) - 1:
                ax.set_xlabel("median e2e latency (ms, log)")
            ax.grid(True, which="both", alpha=0.25)
    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper right", fontsize=9)
    fig.suptitle("Per-query defect decomposition vs end-to-end latency "
                 "(median over queries and 3 repeats; P95 dotted)", fontsize=13)
    fig.text(0.01, 0.005, caption_text(manifest), fontsize=8, color="0.35")
    fig.tight_layout(rect=(0, 0.015, 1, 0.965))
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


# --------------------------------------------------------------------------
# Fig 3: build / RSS summary table
# --------------------------------------------------------------------------
def fig_build_rss_table(agg, manifest, out_path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    cols = ["build_wall_s", "build_cpu_s", "peak_rss_GiB", "serialized_MiB",
            "mean_e2e_ms", "macro_recall"]
    header = ["dataset", "method", "ef", "build wall (s)", "build CPU (s)",
              "peak RSS (GiB)", "serialized (MiB)", "mean e2e (ms)",
              "macro recall@10"]

    rows = []
    for r in sorted(agg, key=lambda r: (DATASET_ORDER.index(r["dataset"])
                                        if r["dataset"] in DATASET_ORDER else 99,
                                        r["method"], int(r["ef"]))):
        rss = fnum(r["median_peak_rss_bytes"])
        ser = fnum(r["median_serialized_bytes"])
        rows.append([
            r["dataset"], METHOD_SHORT.get(r["method"], r["method"]),
            r["ef"] if r["method"] != "b3" else "-",
            f"{fnum(r['median_build_wall_s'], float('nan')):.1f}",
            f"{fnum(r['median_build_cpu_s'], float('nan')):.1f}",
            f"{rss / 2**30:.2f}" if rss is not None else "n/a",
            f"{ser / 2**20:.1f}" if ser is not None else "n/a",
            f"{fnum(r['median_mean_e2e_ms'], float('nan')):.4f}",
            f"{fnum(r['median_macro_recall'], float('nan')):.4f}",
        ])

    nrows = len(rows)
    fig, ax = plt.subplots(figsize=(16, max(3, 0.32 * nrows + 1.6)))
    ax.axis("off")
    tbl = ax.table(cellText=rows, colLabels=header, loc="center",
                   cellLoc="right")
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(8)
    tbl.scale(1, 1.15)
    for j in range(len(header)):
        tbl[0, j].set_facecolor("#d9d9d9")
    ax.set_title("Build / resource / latency summary (medians over 3 repeats; "
                 "B3 exact is the diagnostic oracle, excluded from the frontier)",
                 fontsize=12, pad=14)
    fig.text(0.01, 0.005, caption_text(manifest), fontsize=8, color="0.35")
    fig.tight_layout(rect=(0, 0.02, 1, 1))
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


# --------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work-dir", default=str(DEFAULT_WORK_DIR),
                    help="nodeann_matrix output root (default: "
                         "<exps-root>/work/nodeann_matrix)")
    ap.add_argument("--figures-dir", default=str(DEFAULT_FIGURES_DIR),
                    help="figure output dir (default: <exps-root>/figures)")
    args = ap.parse_args()

    work_dir = Path(args.work_dir)
    figures_dir = Path(args.figures_dir)
    figures_dir.mkdir(parents=True, exist_ok=True)

    agg = read_csv(work_dir / "summary_agg.csv")
    perquery = read_csv(work_dir / "perquery.csv")
    manifest = load_manifest(work_dir)

    try:
        import matplotlib  # noqa: F401
    except ImportError:
        print("ERROR: matplotlib is required for plotting but is not "
              "installed. Install it (e.g. `pip install matplotlib`) and "
              "re-run.", file=sys.stderr)
        return 3

    jobs = [
        ("fig1_recall_latency.png", fig_recall_latency, agg),
        ("fig2_perquery_breakdown.png", fig_perquery_breakdown, perquery),
        ("fig3_build_rss_table.png", fig_build_rss_table, agg),
    ]
    for name, fn, data in jobs:
        out = figures_dir / name
        fn(data, manifest, out)
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
