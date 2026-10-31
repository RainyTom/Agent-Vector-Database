#!/usr/bin/env python3
"""Plot saved eligibility static points only; no search or parameter selection."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

STYLE = {'A-graph': ('o', 'C0'), 'A-hybrid-reference': ('s', 'C1'), 'U-off': ('^', 'C2'),
         'U-cap': ('v', 'C3'), 'U-aware': ('D', 'C4'), 'H-off': ('X', 'C5'),
         'H-cap': ('P', 'C6'), 'H-aware': ('*', 'C7'), 'Exact': ('h', 'C8')}


def main(source, out):
    table = json.loads(Path(source).read_text())
    out.mkdir(parents=True, exist_ok=False)
    plt.rcParams.update({'font.size': 9, 'text.color': '0.5', 'axes.labelcolor': '0.5',
                         'xtick.color': '0.5', 'ytick.color': '0.5', 'axes.edgecolor': '0.5'})
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.6), dpi=100)
    for ax, dataset in zip(axes, ('wiki', 'sift')):
        for row in table:
            if row['dataset'] != dataset:
                continue
            marker, color = STYLE[row['variant']]
            ax.scatter(row['macro'][0], row['e2e_us_mean'] / 1000, marker=marker, color=color, s=46)
            ax.annotate(f"{row['variant']}/{row['ef']}", (row['macro'][0], row['e2e_us_mean'] / 1000),
                        xytext=(3, 2), textcoords='offset points', fontsize=6, color=color)
        ax.set(xscale='linear', yscale='log', xlabel='Nonempty macro recall (measured)',
               ylabel='Nonempty mean e2e latency (ms)', title=dataset.upper())
        ax.axvline(.95, linestyle=':', color='0.6', linewidth=1)
        ax.axvline(.99, linestyle=':', color='0.4', linewidth=1)
        ax.grid(True, alpha=.2)
    fig.suptitle('Measured points only (dotted: 0.95 / 0.99 targets). Lower-right is better.', fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, .94))
    fig.savefig(out / 'quality-time.png', transparent=True)
    plt.close(fig)
    print(f'plotted {len(table)} saved points to {out}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    main(args.source, args.out)
