#!/usr/bin/env python3
"""Portable PNGs from recorded evidence, no searches, retuning or resampling."""
import argparse
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from neighborhood_experiment import load, resolve


def figures(out):
    destination = out / 'figures'
    destination.mkdir()
    records = load(out / 'runs/records.json')
    plt.rcParams.update({'font.size': 11, 'text.color': '0.5', 'axes.labelcolor': '0.5',
                         'xtick.color': '0.5', 'ytick.color': '0.5', 'axes.edgecolor': '0.5'})
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.3), dpi=100)
    names = {'A': 'NodeANN (scan <=512)', 'B': 'Ordinary two-hop', 'C': 'Page-index two-hop'}
    for ax, dataset in zip(axes, ('sift', 'wiki')):
        for index, arm in enumerate('ABC'):
            rows = sorted((row for row in records if row['dataset'] == dataset and row['arm'] == arm and
                           row['phase'] == 'calibration'), key=lambda row: row['ef'])
            ax.plot([row['ef'] for row in rows], [row['metrics']['nonempty_mean_e2e_us'] for row in rows],
                    marker=('o', 's', '^')[index], label=names[arm], color=f'C{index}')
            for row in rows:
                if not row['metrics']['quality_pass']:
                    ax.scatter(row['ef'], row['metrics']['nonempty_mean_e2e_us'], marker='x', s=120, color=f'C{index}')
        ax.set(xscale='log', yscale='log', title=dataset.upper(), xlabel='ef (three frozen budgets)',
               ylabel='Nonempty query mean latency (us)')
        ax.set_xticks([64, 256, 1024], ['64', '256', '1024'])
        ax.grid(True, alpha=.25)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='upper center', ncol=3, fontsize=10)
    fig.suptitle('Calibration only: recorded quality-cost points; x = quality failure', y=.91, fontsize=11)
    fig.tight_layout(rect=(0, 0, 1, .87))
    fig.savefig(destination / 'calibration-nonempty.png', transparent=True)
    plt.close(fig)
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.3), dpi=100)
    manifest = load(out / 'manifest.json')
    for ax, dataset in zip(axes, ('sift', 'wiki')):
        record = next(row for row in records if row['dataset'] == dataset and row['arm'] == 'C' and
                      row['phase'] == 'confirmation' and row['repeat'] == 1)
        rows = load(record['prefix'] + '.normalized.json')
        live = manifest['datasets'][dataset]['shape']['n_live']
        for index, scope in enumerate(('wide', 'middle', 'narrow')):
            subset = [row for row in rows if row['scope_profile'] == scope]
            if subset:
                ax.scatter([row['initial_eligible_count'] for row in subset],
                           [float(row['raw_output']['expanded']) / live for row in subset],
                           marker=('o', '^', 's')[index], color=f'C{index}', alpha=.65,
                           label=f'{scope} (n={len(subset)})')
        ax.axvline(record['ef'], linestyle=':', color='0.5', label='ef=64')
        ax.set(xscale='symlog', xlim=(-.1, live), ylim=(-.03, 1.05), title=dataset.upper(),
               xlabel='Exact eligible count (offline diagnosis)', ylabel='Expanded centers / initial live objects')
        ax.legend(fontsize=9, loc='lower left')
        ax.grid(True, alpha=.25)
    fig.suptitle('Confirmation repeat 1: search work at frozen ef=64 (including empty queries)', fontsize=11)
    fig.tight_layout(rect=(0, 0, 1, .94))
    fig.savefig(destination / 'qualification-vs-expansion.png', transparent=True)
    plt.close(fig)
    print(f'Saved actual-data portable PNGs to {destination}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    figures(resolve(parser.parse_args().out))
