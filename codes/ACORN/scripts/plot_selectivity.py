#!/usr/bin/env python3
"""Analyse the ACORN fixed-selectivity sweep.

Reads, for every `sel-<S>pct` slice under `--data-root`:
  <data-root>/sel-<S>pct/gt_10.txt              exact filtered top-10
  <run-dir>/acorn-sel-<S>pct.jsonl              one JSON line per ef
  <run-dir>/acorn-sel-<S>pct-trace.jsonl        per query returned ids, per ef

Writes tidy CSVs and three vector figures:
  recall@10 vs QPS per selectivity
  visited / |E(U)| vs selectivity per ef (the degeneracy crossover is the 1.0 line)
  recall@1..@10 per selectivity at one ef

The trace must be produced with `--k 10`, otherwise recall@k below 10 cannot be
recovered from it.

Example:
  python3 codes/ACORN/scripts/plot_selectivity.py \
      --data-root datasets/sift1m \
      --run-dir researches/nested-attr-vector-search/2026-11-09-exps/data \
      --out-dir researches/nested-attr-vector-search/2026-11-09-exps
"""
import argparse
import csv
import json
import re
from pathlib import Path

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SLICE = re.compile(r'^sel-(\d+(?:\.\d+)?)pct$')


def load_jsonl(path):
    rows = [json.loads(line) for line in Path(path).read_text().splitlines() if line.strip().startswith('{')]
    return rows


def recall_at_k(trace_rows, ground_truth, k):
    """trace_rows: per (qi, ef) dict with the ranked `returned` ids."""
    depth = ground_truth.shape[1]
    take = min(k, depth)
    hits = total = 0
    for row in trace_rows:
        truth = ground_truth[row['qi'], :take]
        found = set(row['returned'][:take])
        hits += sum(1 for value in truth if value >= 0 and int(value) in found)
        total += take
    return hits / total if total else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--data-root', default=str(ROOT / 'datasets/sift1m'))
    parser.add_argument('--run-dir', required=True)
    parser.add_argument('--out-dir', default=str(ROOT / 'researches/nested-attr-vector-search/2026-11-09-exps'))
    parser.add_argument('--suffix', default='', help="run/file suffix, e.g. -top20 for the k=20 runs")
    parser.add_argument('--recall-k-ef', type=int, default=10, help='ef shown in the recall@k figure')
    parser.add_argument('--fixed-ef', type=int, default=0,
                        help='when >0, also draw recall and QPS against selectivity at this ef')
    parser.add_argument('--overlay', action='append', default=[],
                        help='label=jsonl of another implementation, records carry slice/recall/qps')
    parser.add_argument('--k', type=int, default=10)
    args = parser.parse_args()

    data_root = Path(args.data_root)
    run_dir = Path(args.run_dir)
    out_dir = Path(args.out_dir)
    figures = out_dir / 'figures'
    data_dir = out_dir / 'data'
    figures.mkdir(parents=True, exist_ok=True)
    data_dir.mkdir(parents=True, exist_ok=True)

    slices = []
    for directory in sorted(data_root.iterdir()):
        match = SLICE.match(directory.name)
        if not match:
            continue
        label = match.group(1)
        summary_path = run_dir / f'acorn-sel-{label}pct{args.suffix}.jsonl'
        trace_path = run_dir / f'acorn-sel-{label}pct{args.suffix}-trace.jsonl'
        if not summary_path.exists() or not trace_path.exists():
            continue
        summary = sorted(load_jsonl(summary_path), key=lambda row: row['ef'])
        trace = load_jsonl(trace_path)
        meta = json.loads((directory / 'meta.json').read_text())
        ground_truth = np.fromfile(directory / f'gt_{args.k}.txt', dtype=np.int32).reshape(-1, args.k)
        slices.append(dict(label=label, selectivity=float(label) / 100.0,
                           achieved=meta['selectivity_achieved_mean'],
                           summary=summary, trace=trace, ground_truth=ground_truth, ef_list=[r['ef'] for r in summary]))
    slices.sort(key=lambda item: item['selectivity'])
    if not slices:
        raise SystemExit(f'no sel-*pct slice with both a summary and a trace under {data_root} / {run_dir}')

    with (data_dir / f'acorn-selectivity{args.suffix}.csv').open('w', newline='') as sink:
        writer = csv.writer(sink)
        writer.writerow(['selectivity', 'achieved_selectivity', 'ef', f'recall_at_{args.k}', 'qps', 'per_query_ms',
                         'dists_per_q', 'visited_per_q', 'eligible_per_q', 'visited_over_eligible', 'avg_returned'])
        for item in slices:
            for row in item['summary']:
                writer.writerow([f"{item['selectivity']:.2f}", f"{item['achieved']:.6f}", row['ef'],
                                 f"{row['recall']:.6f}", f"{row['qps']:.1f}", f"{row['per_query_ms']:.4f}",
                                 f"{row['dists_per_q']:.1f}", f"{row['visited_per_q']:.1f}",
                                 f"{row['eligible_per_q']:.1f}",
                                 f"{row['visited_per_q'] / row['eligible_per_q']:.4f}", f"{row['avg_returned']:.2f}"])
    print(f'wrote {data_dir / f"acorn-selectivity{args.suffix}.csv"}')

    with (data_dir / f'acorn-recall-at-k{args.suffix}.csv').open('w', newline='') as sink:
        writer = csv.writer(sink)
        writer.writerow(['selectivity', 'ef', 'k', 'recall'])
        for item in slices:
            for ef in item['ef_list']:
                rows = [row for row in item['trace'] if row['ef'] == ef]
                for k in range(1, args.k + 1):
                    writer.writerow([f"{item['selectivity']:.2f}", ef, k,
                                     f"{recall_at_k(rows, item['ground_truth'], k):.6f}"])
    print(f'wrote {data_dir / f"acorn-recall-at-k{args.suffix}.csv"}')

    plt.rcParams.update({'font.size': 9, 'axes.grid': True, 'grid.alpha': 0.3,
                         'axes.spines.top': False, 'axes.spines.right': False})
    cmap = plt.get_cmap('viridis')

    fig, ax = plt.subplots(figsize=(4.4, 3.3))
    for index, item in enumerate(slices):
        color = cmap(index / max(len(slices) - 1, 1))
        qps = [row['qps'] for row in item['summary']]
        recall = [row['recall'] for row in item['summary']]
        ax.plot(qps, recall, marker='o', markersize=3, linewidth=1.3, color=color,
                label=f"{item['selectivity'] * 100:g}%")
    ax.set_xscale('log')
    ax.set_xlabel('QPS (100 queries, single core)')
    ax.set_ylabel(f'Recall@{args.k}')
    ax.set_ylim(0.55, 1.02)
    ax.legend(frameon=False, fontsize=6.5, ncol=2, title='selectivity', title_fontsize=6.5)
    fig.tight_layout()
    for fmt in ('pdf', 'svg'):
        fig.savefig(figures / f'acorn-selectivity-recall-qps{args.suffix}.{fmt}')
    plt.close(fig)

    ef_values = sorted({ef for item in slices for ef in item['ef_list']})
    fig, ax = plt.subplots(figsize=(4.4, 3.3))
    for index, ef in enumerate(ef_values):
        color = cmap(index / max(len(ef_values) - 1, 1))
        x, ratio = [], []
        for item in slices:
            for row in item['summary']:
                if row['ef'] == ef:
                    x.append(item['selectivity'])
                    ratio.append(row['visited_per_q'] / row['eligible_per_q'])
        ax.plot(x, ratio, marker='s', markersize=3, linewidth=1.3, color=color, label=f'ef{ef}')
    ax.axhline(1.0, color='black', linewidth=0.8, linestyle='--')
    ax.annotate('one exact pass over E(U)', (ax.get_xlim()[0], 1.05), fontsize=6.5)
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_xlabel('selectivity |E(U)| / n')
    ax.set_ylabel('visited nodes / |E(U)|')
    ax.legend(frameon=False, fontsize=6.5, ncol=2)
    fig.tight_layout()
    for fmt in ('pdf', 'svg'):
        fig.savefig(figures / f'acorn-selectivity-visited-ratio{args.suffix}.{fmt}')
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(4.4, 3.3))
    for index, item in enumerate(slices):
        if args.recall_k_ef not in item['ef_list']:
            continue
        color = cmap(index / max(len(slices) - 1, 1))
        rows = [row for row in item['trace'] if row['ef'] == args.recall_k_ef]
        ks = list(range(1, args.k + 1))
        ax.plot(ks, [recall_at_k(rows, item['ground_truth'], k) for k in ks],
                marker='o', markersize=2.5, linewidth=1.2, color=color, label=f"{item['selectivity'] * 100:g}%")
    ax.set_xlabel('k')
    ax.set_ylabel(f'recall@k (ef={args.recall_k_ef})')
    ax.set_xticks(range(1, args.k + 1))
    ax.set_ylim(0.55, 1.02)
    ax.legend(frameon=False, fontsize=6.5, ncol=2, title='selectivity', title_fontsize=6.5)
    fig.tight_layout()
    for fmt in ('pdf', 'svg'):
        fig.savefig(figures / f'acorn-recall-at-k{args.suffix}.{fmt}')
    plt.close(fig)
    if args.fixed_ef:
        series = [('ACORN-1 (codes/ACORN)', [item['selectivity'] * 100 for item in slices],
                   [next((record['recall'] for record in item['summary'] if record['ef'] == args.fixed_ef),
                         None) for item in slices],
                   [next((record['qps'] for record in item['summary'] if record['ef'] == args.fixed_ef),
                         None) for item in slices])]
        if None in series[0][2] or None in series[0][3]:
            raise SystemExit(f'ef={args.fixed_ef} missing from the codes/ACORN runs')
        for spec in args.overlay:
            label, path = spec.split('=', 1)
            records = []
            for row in load_jsonl(path):
                match = SLICE.search(str(row.get('slice', '')))
                if not match:
                    continue
                records.append((float(match.group(1)) / 100.0, row))
            records.sort(key=lambda pair: pair[0])
            series.append((label, [pair[0] * 100 for pair in records],
                           [pair[1]['recall'] for pair in records],
                           [pair[1]['qps'] for pair in records]))

        with (data_dir / f'acorn-selectivity-curves{args.suffix}.csv').open('w', newline='') as sink:
            writer = csv.writer(sink)
            writer.writerow(['implementation', 'selectivity', 'recall', 'qps'])
            for label, x, recall, qps in series:
                for xi, ri, qi in zip(x, recall, qps):
                    writer.writerow([label, f'{xi:g}', f'{ri:.6f}', f'{qi:.1f}'])
        markers = ['o', 's', '^', 'D', 'v']
        colors = ['#1f77b4', '#d62728', '#2ca02c', '#9467bd', '#8c564b']
        fig, (left, right) = plt.subplots(1, 2, figsize=(7.6, 3.1))
        for index, (label, x, recall, qps) in enumerate(series):
            marker = markers[index % len(markers)]
            color = colors[index % len(colors)]
            left.plot(x, recall, marker=marker, markersize=4, linewidth=1.4, color=color, label=label)
            right.plot(x, qps, marker=marker, markersize=4, linewidth=1.4, color=color, label=label)
        for ax, ylabel in ((left, f'recall@{args.k}'), (right, 'QPS')):
            ax.set_xlabel('selectivity (%)')
            ax.set_ylabel(ylabel)
            ax.set_xticks(sorted({xi for _, x, _, _ in series for xi in x}))
        low = min(min(recall) for _, _, recall, _ in series)
        left.set_ylim(low - 0.02, 1.01)
        right.set_ylim(0, max(max(qps) for _, _, _, qps in series) * 1.12)
        left.legend(frameon=False, fontsize=6.5)
        fig.suptitle(f'ef={args.fixed_ef}, k={args.k}, 100 queries, single core', fontsize=9)
        fig.tight_layout()
        for fmt in ('pdf', 'svg'):
            fig.savefig(figures / f'acorn-selectivity-curves{args.suffix}.{fmt}')
        plt.close(fig)

    print(f'wrote figures under {figures}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
