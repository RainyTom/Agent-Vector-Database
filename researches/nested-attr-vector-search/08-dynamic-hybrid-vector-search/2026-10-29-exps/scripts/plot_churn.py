#!/usr/bin/env python3
"""Plot local-ANN churn curves: recall and dead-ratio vs churn fraction per arm."""
import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]


def load(paths):
    rows = []
    for p in paths:
        with open(p) as f:
            for r in csv.DictReader(f):
                r["alpha"] = float(r["alpha"])
                r["recall"] = float(r["recall"])
                r["dead_ratio"] = float(r["dead_ratio"])
                r["maint_ms"] = float(r["maint_ms"])
                r["rebuild_ms"] = float(r["rebuild_ms"])
                r["_file"] = Path(p).stem
                rows.append(r)
    return rows


def main():
    paths = sys.argv[1:]
    if not paths:
        paths = sorted(str(p) for p in (ROOT / "data").glob("churn_*.csv"))
    rows = load(paths)
    if not rows:
        raise SystemExit("no churn CSV found")
    out = ROOT / "figures"
    out.mkdir(exist_ok=True)

    groups = {}
    for r in rows:
        key = r["_file"].replace("churn_", "")
        groups.setdefault(key, []).append(r)

    fig, axes = plt.subplots(1, 3, figsize=(14.5, 3.8))
    colors = plt.cm.tab10.colors
    for i, (name, rs) in enumerate(sorted(groups.items())):
        rs = sorted(rs, key=lambda r: r["alpha"])
        axes[0].plot([r["alpha"] * 100 for r in rs], [r["recall"] for r in rs],
                     "o-", ms=4, lw=1.3, color=colors[i % 10], label=name)
        axes[1].plot([r["alpha"] * 100 for r in rs], [r["dead_ratio"] for r in rs],
                     "s--", ms=4, lw=1.1, color=colors[i % 10], label=name)
        axes[2].plot([r["alpha"] * 100 for r in rs],
                     [r["lat_us"] for r in rs],
                     "^-", ms=4, lw=1.2, color=colors[i % 10], label=name)
    axes[0].set_xlabel("moved items (% of indexed items)")
    axes[0].set_ylabel("per-node recall@10")
    axes[0].set_title("local-index quality under attribute churn", fontsize=9)
    axes[0].grid(alpha=0.25)
    axes[0].legend(fontsize=7)
    axes[1].set_xlabel("moved items (%)")
    axes[1].set_ylabel("dead-entry ratio")
    axes[1].set_title("tombstone accumulation", fontsize=9)
    axes[1].grid(alpha=0.25)
    axes[1].legend(fontsize=7)
    axes[2].set_xlabel("moved items (%)")
    axes[2].set_ylabel("per-node search latency (µs)")
    axes[2].set_title("latency tax of tombstones", fontsize=9)
    axes[2].grid(alpha=0.25)
    axes[2].legend(fontsize=7)
    fig.suptitle("hnswlib local indexes, M16/efC200, ef=64, single thread; "
                 "distance = shared RawL2Space", fontsize=9)
    fig.tight_layout(rect=[0, 0, 1, 0.94])
    fig.savefig(out / "fig5_churn.png", dpi=160)
    print(f"wrote {out / 'fig5_churn.png'} ({len(groups)} arms)")


if __name__ == "__main__":
    main()
