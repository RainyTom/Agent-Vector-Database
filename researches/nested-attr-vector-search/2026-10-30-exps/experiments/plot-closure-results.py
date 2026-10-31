#!/usr/bin/env python3
"""Plot only saved calibration measurements; no new experiments or parameter selection."""
import argparse
import csv
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator, FormatStrFormatter


def load(path):
    return json.loads(path.read_text())


def query_rows(prefix):
    with Path(prefix + '.queries.csv').open(newline='') as handle:
        return list(csv.DictReader(handle))


def main(raw, package, out):
    out.mkdir(parents=True, exist_ok=False)
    configs = load(package / 'configurations.json')
    runs = load(raw / 'runs.json')
    datasets = list(load(raw / 'manifest.json')['results'])
    arms = ('A', 'U', 'G', 'R', 'H', 'H0')
    markers = ('o', 's', '^', 'D', 'P', 'X')
    plt.rcParams.update({'font.size': 10, 'text.color': '0.5', 'axes.labelcolor': '0.5',
                         'xtick.color': '0.5', 'ytick.color': '0.5', 'axes.edgecolor': '0.5'})
    fig, axes = plt.subplots(1, len(datasets), figsize=(10, 4.5), dpi=100, squeeze=False)
    for ax, dataset in zip(axes[0], datasets):
        for index, arm in enumerate(arms):
            selected = sorted((row for row in configs if row['dataset'] == dataset and row['arm'] == arm and
                               row['phase'] == 'calibration'), key=lambda row: row['ef'])
            ax.plot([r['macro'] for r in selected], [r['mean_e2e_us'] for r in selected],
                    marker=markers[index], color=f'C{index}', label=arm, alpha=.8)
        ax.axvline(.999, linestyle=':', color='0.5', linewidth=1)
        ax.set(yscale='log', xlabel='Nonempty macro Recall@10 (measured)',
               ylabel='Nonempty mean e2e latency (us)', title=dataset.upper())
        ax.xaxis.set_major_locator(MaxNLocator(5))
        ax.xaxis.set_major_formatter(FormatStrFormatter('%.3f'))
        ax.grid(True, alpha=.2)
    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='upper center', ncol=6)
    fig.suptitle('Calibration: three ef points (64/256/1024) per arm; dotted line = macro quality threshold', y=.92, fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, .88))
    fig.savefig(out / 'calibration-quality-cost.png', transparent=True)
    plt.close(fig)
    fig, axes = plt.subplots(1, len(datasets), figsize=(10, 4.5), dpi=100, squeeze=False)
    for ax, dataset in zip(axes[0], datasets):
        pair = {}
        for arm in ('R', 'H'):
            record = next(r for r in runs if r['dataset'] == dataset and r['arm'] == arm and
                          r['phase'] == 'calibration' and r['ef'] == 64)
            pair[arm] = [r for r in query_rows(record['prefix']) if int(r['gt_count'])]
        assert [(r['event'], r['qidx']) for r in pair['R']] == [(r['event'], r['qidx']) for r in pair['H']]
        x = [int(r['expanded']) for r in pair['R']]
        y = [int(r['expanded']) for r in pair['H']]
        high = max(x + y + [1]) * 1.1
        ax.plot([1, high], [1, high], linestyle=':', color='0.5')
        ax.scatter(x, y, alpha=.6, s=22)
        ax.set(xscale='log', yscale='log', xlim=(.8, high), ylim=(.8, high),
               xlabel='R expanded centers', ylabel='H expanded centers', title=f'{dataset.upper()}: ef=64, n={len(x)}')
        ax.grid(True, alpha=.2)
    fig.suptitle('Matched-ef calibration only: below diagonal = fewer H expansions; check quality separately', fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, .94))
    fig.savefig(out / 'matched-ef64-expansion.png', transparent=True)
    plt.close(fig)
    print(f'Plotted saved measurements to {out}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    main(args.raw.resolve(), args.package.resolve(), args.out.resolve())
