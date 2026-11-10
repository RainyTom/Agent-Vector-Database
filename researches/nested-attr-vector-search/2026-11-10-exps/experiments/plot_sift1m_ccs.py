#!/usr/bin/env python3
"""Draw the SIFT1M-CCS figures from results/sift1m-ccs/*.csv.

Nothing here runs a scheme; every curve is read back from the CSV files that
run_sift1m_ccs.py produced.
"""
import csv
import pathlib

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = pathlib.Path(__file__).resolve().parents[4]
DATA = ROOT / 'results/sift1m-ccs'
FIG = ROOT / 'researches/nested-attr-vector-search/2026-11-10-exps/figures'

COLORS = {
    'prefilter': '#1a7f37',
    'postfilter': '#bf8700',
    'acorn': '#cf222e',
    'directory-aware': '#0969da',
    'oracle-own': '#054da5',
    'oracle-subtree': '#8250df',
}


def rows(name):
    return [(float(r['recall']), float(r['qps']))
            for r in csv.DictReader(open(DATA / name))]


def save(fig, name):
    FIG.mkdir(parents=True, exist_ok=True)
    fig.savefig(FIG / f'{name}.svg', format='svg', bbox_inches='tight')
    fig.savefig(FIG / f'{name}.png', dpi=180, bbox_inches='tight')
    print('wrote', name)


def two_scheme(da, acorn, title, name, ylim):
    fig, ax = plt.subplots(figsize=(4.6, 3.6))
    for points, color, marker, label in (
            (rows(da), COLORS['directory-aware'], 'o', 'Directory-Aware'),
            (rows(acorn), COLORS['acorn'], 's', 'ACORN-hnswlib')):
        points.sort()
        ax.plot([p[0] for p in points], [p[1] for p in points], marker=marker,
                linestyle='-', color=color, markersize=4, linewidth=1.4, label=label)
    ax.set_yscale('log')
    ax.set_ylim(*ylim)
    ax.set_xlim(0.0, 1.05)
    ax.set_xlabel('Recall@10')
    ax.set_ylabel('QPS (8 threads, log scale)')
    ax.grid(True, which='both', alpha=0.22, linewidth=0.5)
    ax.legend(frameon=False, fontsize=8, loc='lower left')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.tick_params(labelsize=8)
    ax.set_title(title, fontsize=9, pad=6)
    fig.tight_layout()
    save(fig, name)


def main():
    two_scheme('directory-aware-recursive.csv', 'acorn-recursive.csv',
               'Recursive · k=10 · σ≈4.5%', 'directory-aware-recursive', (15, 60000))
    two_scheme('directory-aware-nonrecursive.csv', 'acorn-nonrecursive.csv',
               'Non-recursive · k=10 · σ≈0.05%', 'directory-aware-nonrecursive', (10, 60000))

    fig, ax = plt.subplots(figsize=(5.9, 4.0))
    ax.plot(*zip(*sorted(rows('prefilter.csv'))), 'D', color=COLORS['prefilter'],
            markersize=7, label='Prefilter (exact scan)')
    ax.plot(*zip(*sorted(rows('postfilter.csv'))), '^-', color=COLORS['postfilter'],
            markersize=4, linewidth=1.2, label='Postfilter (1/s oversampling)')
    ax.plot(*zip(*sorted(rows('acorn-recursive.csv'))), 's-', color=COLORS['acorn'],
            markersize=4, linewidth=1.3, label='ACORN-hnswlib')
    own = rows('oracle-partition-per-own.csv')
    ax.plot([p[0] for p in own], [p[1] for p in own], 'o', color=COLORS['oracle-own'],
            markersize=3, alpha=0.6, label='Oracle Partition · per-own (1×)')
    ax.plot(*zip(*sorted(rows('oracle-partition-subtree.csv'))), 'v-',
            color=COLORS['oracle-subtree'], markersize=4, linewidth=1.3,
            label='Oracle Partition · subtree (4.83×)')
    ax.set_yscale('log')
    ax.set_xlim(0.4, 1.02)
    ax.set_ylim(100, 80000)
    ax.set_xlabel('Recall@10')
    ax.set_ylabel('QPS (8 threads, log scale)')
    ax.grid(True, which='both', alpha=0.22, linewidth=0.5)
    ax.legend(frameon=False, fontsize=7.5, loc='lower left')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.tick_params(labelsize=8)
    ax.set_title('SIFT1M · CCS concepts · k=10 · σ≈4.5%', fontsize=9, pad=6)
    fig.tight_layout()
    save(fig, 'sift1m-ccs-qps-recall')

    # per-n sweep of the per-own oracle: recall and QPS against per_n
    per_n = {}
    for r in csv.DictReader(open(DATA / 'oracle-partition-per-own.csv')):
        per_n.setdefault(int(r['per_n']), {})[int(r['ef'])] = (float(r['qps']), float(r['recall']))
    ns = sorted(per_n)
    fig, (left, right) = plt.subplots(1, 2, figsize=(7.4, 3.1), sharex=True)
    cmap = plt.get_cmap('plasma')
    efs = [10, 50, 200, 800]
    for i, ef in enumerate(efs):
        color = cmap(i / max(1, len(efs) - 1))
        left.plot(ns, [per_n[n][ef][1] for n in ns], '-o', markersize=3.5, linewidth=1.2,
                  color=color, label=f'ef={ef}')
        right.plot(ns, [per_n[n][ef][0] for n in ns], '-o', markersize=3.5, linewidth=1.2,
                   color=color, label=f'ef={ef}')
    left.set_ylim(0.55, 1.03)
    left.set_xlabel('per-attribute n')
    left.set_ylabel('Recall@10')
    right.set_xlabel('per-attribute n')
    right.set_ylabel('QPS (8 threads)')
    right.set_yscale('log')
    for ax in (left, right):
        ax.grid(alpha=0.25, linewidth=0.5)
        ax.spines['top'].set_visible(False)
        ax.spines['right'].set_visible(False)
        ax.tick_params(labelsize=8)
        ax.legend(frameon=False, fontsize=7.5)
    fig.suptitle('Oracle Partition · SIFT1M-CCS · k=10', fontsize=9)
    fig.tight_layout()
    save(fig, 'oracle-partition-per-n')


if __name__ == '__main__':
    main()
