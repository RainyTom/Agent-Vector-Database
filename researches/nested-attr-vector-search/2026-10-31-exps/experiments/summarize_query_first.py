#!/usr/bin/env python3
"""Extract the three-arm resident-query table from run_packed_query outputs.

Usage: python3 summarize_query_first.py RAW_DIR [RAW_DIR ...]
Writes one CSV row per (unit, arm, group) plus a markdown table on stdout.
"""
import json
import sys
from pathlib import Path

GROUPS = ('all_nonempty', 'all_queries', 'confirmation_nonempty')
ARMS = ('A-graph', 'P-dfs', 'P-dfs-aware')


def main(paths):
    rows = []
    for raw in map(Path, paths):
        summary = json.loads((raw / 'summary.json').read_text())
        points = summary['pooled_points']
        unit = summary['dataset']
        for group in GROUPS:
            if group not in points['A-graph']:
                continue
            base = points['A-graph'][group]['e2e_us']['mean']
            for arm in ARMS:
                cell = points[arm][group]
                rows.append({
                    'unit': unit,
                    'group': group,
                    'arm': arm,
                    'e2e_us_mean': round(cell['e2e_us']['mean'], 3),
                    'e2e_us_p95': round(cell['e2e_us']['p95'], 3),
                    'ratio_vs_A': round(cell['e2e_us']['mean'] / base, 4),
                    'macro': round(cell['quality']['macro'], 6),
                    'pair': round(cell['quality']['pair'], 6),
                    'distance_calls_mean': round(cell['distance_calls']['mean'], 1),
                    'queries': cell['quality']['nonempty'],
                })
        print('##', unit, '| builds', summary['index_builds'], 'passes', summary['query_passes'],
              '| gt_reused', summary['gt_reused'], file=sys.stderr)
    header = list(rows[0])
    print(','.join(header))
    for row in rows:
        print(','.join(str(row[k]) for k in header))
    return rows


if __name__ == '__main__':
    main(sys.argv[1:])
