#!/usr/bin/env python3
"""Plot measured dynamic triad results; never silently discard quality failures."""
import argparse
import json
import statistics
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


COLORS = {"A": "#1565c0", "B": "#ef6c00", "C": "#2e7d32"}
LABELS = {"A": "A NodeANN", "B": "B PE-ONLINE", "C": "C PE-OFFLINE"}


def draw_series(ax, rows, xkey, yfn, xlabel, ylabel):
    for arm in "ABC":
        rs = sorted([r for r in rows if r["arm"] == arm],
                    key=lambda r: float(r["config"][xkey]))
        if not rs:
            continue
        xs = [float(r["config"][xkey]) for r in rs]
        ys = [yfn(r["summary"]) for r in rs]
        deviations = []
        for r in rs:
            values = [yfn(s) for s in r["_replicate_summaries"]]
            deviations.append(statistics.stdev(values) if len(values) > 1 else 0)
        ax.errorbar(xs, ys, yerr=deviations, fmt="o-", color=COLORS[arm],
                    label=LABELS[arm], ms=4, capsize=3)
        bad = [(x, y) for x, y, r in zip(xs, ys, rs) if not r["quality_pass"]]
        if bad:
            ax.scatter(*zip(*bad), marker="x", color="red", s=80, zorder=5,
                       label=f"{arm} quality failed")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if xkey == "actual_q_u":
        ax.set_xscale("log")
    ax.grid(alpha=.25)
    ax.legend(fontsize=7)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for result_path in args.results:
        payload = json.loads(result_path.read_text())
        rows = payload["aggregates"]
        for row in rows:
            row["_replicate_summaries"] = [
                run["summary"] for run in payload["runs"]
                if run["config_id"] == row["config_id"] and run["arm"] == row["arm"]
                and not run["validation_errors"]]
        datasets = sorted({r["config"]["dataset"] for r in rows})
        for dataset in datasets:
            ds = [r for r in rows if r["config"]["dataset"] == dataset]
            primary_kind = "mixed" if any(r["config"]["update_type"] == "mixed" for r in ds) else "item_move"
            primary = [r for r in ds if int(r["config"]["depth"]) == 3
                       and r["config"]["locality"] == "uniform"
                       and r["config"]["update_type"] == primary_kind]
            repeats = sorted({int(r["repeats"]) for r in ds})
            n_live = sorted({int(r["config"]["n_live"]) for r in ds})
            title = (f"{dataset}, N={n_live}: serialized replay; repeats={repeats}; mean +/- repeat SD\n"
                     "independent processes may overlap; red x = recall/invariant failure")
            for ratio in sorted({float(r["config"]["target_q_u"]) for r in primary}):
                subset = [r for r in primary if float(r["config"]["target_q_u"]) == ratio]
                if len({r["config"]["update_rate"] for r in subset}) < 2:
                    continue
                fig, axes = plt.subplots(1, 3, figsize=(13.8, 3.9))
                metrics = [(lambda s: s["query_p95_us"], "Query p95 (us)"),
                           (lambda s: s["update_p95_us"], "Update p95 / event (us)"),
                           (lambda s: s["total_workload_us"] / max(1, s["n_queries"]),
                            "Measured (query + update) / query (us)")]
                for ax, (fn, label) in zip(axes, metrics):
                    draw_series(ax, subset, "actual_unique_coverage", fn,
                                "Actual unique initial ITEM update coverage", label)
                fig.suptitle(title + f"\ntarget Q/U={ratio:g}; depth3 / uniform / {primary_kind}", fontsize=9)
                fig.tight_layout()
                dest = args.out / f"{result_path.parent.name}_{dataset}_coverage_qu{ratio:g}.png"
                fig.savefig(dest, dpi=150)
                plt.close(fig)
                print(dest)
            for rate in sorted({float(r["config"]["update_rate"]) for r in primary if r["config"]["update_rate"] > 0}):
                subset = [r for r in primary if float(r["config"]["update_rate"]) == rate]
                if len({r["config"]["target_q_u"] for r in subset}) < 2:
                    continue
                fig, axes = plt.subplots(1, 3, figsize=(13.8, 3.9))
                for ax, fn, label in zip(axes,
                        [lambda s: s["query_p95_us"], lambda s: s["update_p95_us"],
                         lambda s: s["total_workload_us"] / max(1, s["n_queries"])],
                        ["Query p95 (us)", "Update p95 / event (us)", "Measured total / query (us)"]):
                    draw_series(ax, subset, "actual_q_u", fn, "Actual query / update event ratio", label)
                fig.suptitle(title + f"\ntarget coverage={rate:g}; depth3 / uniform / {primary_kind}", fontsize=9)
                fig.tight_layout()
                dest = args.out / f"{result_path.parent.name}_{dataset}_qu_rate{rate:g}.png"
                fig.savefig(dest, dpi=150)
                plt.close(fig)
                print(dest)
            types = [r for r in ds if int(r["config"]["depth"]) == 3
                     and r["config"]["locality"] == "uniform"
                     and float(r["config"]["update_rate"]) == .5
                     and float(r["config"]["target_q_u"]) == 1]
            names = sorted({r["config"]["update_type"] for r in types})
            if len(names) > 1:
                fig, axes = plt.subplots(1, 2, figsize=(13.8, 4.5))
                for index, arm in enumerate("ABC"):
                    rs = {r["config"]["update_type"]: r for r in types if r["arm"] == arm}
                    xs = [i + (index - 1) * .25 for i in range(len(names))]
                    for ax, metric in zip(axes, ["update_p95_us", "query_p95_us"]):
                        ys = [rs[n]["summary"][metric] if n in rs else float("nan") for n in names]
                        ax.bar(xs, ys, width=.25, label=LABELS[arm], color=COLORS[arm])
                        for x, y, name in zip(xs, ys, names):
                            if name in rs and not rs[name]["quality_pass"]:
                                ax.scatter(x, y, marker="x", color="red", zorder=5)
                        ax.set_xticks(range(len(names)), names, rotation=25, ha="right", fontsize=8)
                        ax.set_yscale("symlog", linthresh=1)
                        ax.grid(axis="y", alpha=.25)
                        ax.legend(fontsize=7)
                axes[0].set_ylabel("Update p95 / event (us); batch != ITEM")
                axes[1].set_ylabel("End-to-end query p95 (us)")
                fig.suptitle(title + "\nSeparate update types, target coverage .5 / Q/U1", fontsize=9)
                fig.tight_layout()
                dest = args.out / f"{result_path.parent.name}_{dataset}_update_types.png"
                fig.savefig(dest, dpi=150)
                plt.close(fig)
                print(dest)


if __name__ == "__main__":
    main()
