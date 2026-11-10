#!/usr/bin/env python3
"""Draw the recall-throughput Pareto chart from a measurements file.

Reads <experiment>/data/measurements.jsonl produced by run_experiments.py, keeps
the non-dominated point of every method, and writes <experiment>/figures/pareto.svg.
Only measured points are drawn: no interpolation, no extrapolation.
"""
import argparse
import collections
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from svg_labels import self_contained_svg

STYLE = {
    'NestedAttrVectorRetrieval': dict(color='#009E73', marker='D', label='NestedAttrVectorRetrieval'),
    'OraclePartition': dict(color='#0072B2', marker='o', label='OraclePartition'),
    'ACORN': dict(color='#D55E00', marker='s', label='ACORN'),
    'PreFilter': dict(color='#666666', marker='x', label='Pre-filter (exact)'),
}
ORDER = list(STYLE)


def pareto(points):
    """Points that are not dominated on (recall, qps); both larger is better."""
    kept = []
    for point in sorted(points, key=lambda p: (-p['recall'], -p['qps'])):
        if not any(other['recall'] >= point['recall'] and other['qps'] >= point['qps'] and other is not point
                   for other in points):
            kept.append(point)
    return sorted(kept, key=lambda p: p['recall'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--measurements', required=True)
    parser.add_argument('--out', required=True, help='figure path, .svg')
    parser.add_argument('--title', default='')
    args = parser.parse_args()

    records = [json.loads(line) for line in Path(args.measurements).read_text().splitlines() if line.strip()]
    grouped = collections.defaultdict(list)
    for record in records:
        if 'recall' not in record or 'qps' not in record:
            continue
        grouped[record['method']].append(record)
    if not grouped:
        raise SystemExit('no usable measurements')

    figure, axes = plt.subplots(figsize=(7.1, 3.9))
    summary = {}
    for method in ORDER + [m for m in grouped if m not in ORDER]:
        if method not in grouped:
            continue
        frontier = pareto(grouped[method])
        summary[method] = frontier
        style = STYLE.get(method, dict(color='#999999', marker='^', label=method))
        axes.plot([p['recall'] for p in frontier], [p['qps'] for p in frontier],
                  lw=1.5, ms=4.5, **style)
        for point in frontier:
            axes.annotate(str(point.get('ef', '')), (point['recall'], point['qps']),
                          textcoords='offset points', xytext=(4, 4), fontsize=6, color=style['color'])

    axes.set_xlabel('Recall@10 against exact FP32 ground truth')
    axes.set_ylabel('Throughput (queries/s)')
    axes.set_ylim(bottom=0)
    axes.grid(alpha=0.25)
    axes.legend(frameon=False, fontsize=8)
    if args.title:
        axes.set_title(args.title, fontsize=9)
    figure.tight_layout()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(out, bbox_inches='tight', pad_inches=0.08)
    plt.close(figure)
    if out.suffix.lower() == '.svg':
        # outline live text so the figure renders without any installed font
        self_contained_svg(out, args.title or 'Recall-throughput Pareto',
                           'Measured non-dominated configurations; lines connect only measured points.')

    for method, frontier in summary.items():
        print(f'{method}: ' + ', '.join(f"ef={p.get('ef')} R={p['recall']:.4f} QPS={p['qps']:.0f}"
                                        for p in frontier))
    print(f'wrote {out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
