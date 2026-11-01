#!/usr/bin/env python3
"""Summarize single-graph vs even-partition runs at matched recall.

Reads the JSON emitted by partition_vs_single.cpp. All recall values are
recall@100 against exact SIFT1M ground truth. Latencies are per-query means.
"""
import argparse
import json
from pathlib import Path


def points(section, key):
    """Return [(recall, mean_us, distance_calls)] from {ef...} or {kN: {ef...}}."""
    out = []
    if key is None:
        arms = [('', section)]
    else:
        arms = [(key, section[key])]
    for label, arm in arms:
        for ef, row in arm.items():
            out.append({
                'label': label or 'single',
                'ef': int(ef.replace('ef', '')),
                'recall': row['recall_at_100'],
                'mean_us': row['mean_us'],
                'p95_us': row['p95_us'],
                'distance_calls': row['distance_calls_per_query'],
                'candidates': row['candidates_merged'],
            })
    return out


def at_recall(points_in, target):
    """Linear interpolation of latency at a recall level, if bracketed."""
    ordered = sorted(points_in, key=lambda p: p['recall'])
    for low, high in zip(ordered, ordered[1:]):
        if low['recall'] <= target <= high['recall'] and high['recall'] > low['recall']:
            span = high['recall'] - low['recall']
            t = (target - low['recall']) / span
            return low['mean_us'] + t * (high['mean_us'] - low['mean_us'])
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('result', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()

    data = json.loads(args.result.read_text())
    single = points(data['single'], None)
    summary = {
        'kind': 'matched_recall_summary',
        'source': str(args.result),
        'objects': data['objects'],
        'k': data['k'],
        'partitions': data['partitions'],
        'single': single,
        'partition_serial': {},
        'partition_parallel': {},
        'comparisons': [],
    }
    for key in data['partition_serial']:
        serial = points(data['partition_serial'], key)
        parallel = points(data['partition_parallel'], key)
        summary['partition_serial'][key] = serial
        summary['partition_parallel'][key] = parallel

        single_max = max(p['recall'] for p in single)
        partition_max = max(p['recall'] for p in serial)
        if partition_max < min(p['recall'] for p in single):
            # No common recall level: single graph is better on both axes.
            cheapest = min(single, key=lambda p: p['mean_us'])
            entry = {
                'per_partition_k': key,
                'dominated': True,
                'partition_recall_ceiling': partition_max,
                'single_recall_at_cheapest': cheapest['recall'],
                'single_mean_us_at_cheapest': cheapest['mean_us'],
                'note': 'partition arm never reaches the single graph recall range; '
                        'single is both faster and more accurate at its cheapest ef',
            }
            summary['comparisons'].append(entry)
            continue

        ceiling = min(partition_max, single_max)
        single_us = at_recall(single, ceiling)
        serial_us = at_recall(serial, ceiling)
        parallel_us = at_recall(parallel, ceiling)
        if None in (single_us, serial_us, parallel_us):
            raise SystemExit('interpolation failed at recall ' + str(ceiling))
        entry = {
            'per_partition_k': key,
            'matched_recall': ceiling,
            'partition_recall_ceiling': partition_max,
            'single_mean_us_at_matched_recall': single_us,
            'partition_serial_mean_us_at_matched_recall': serial_us,
            'partition_parallel_mean_us_at_matched_recall': parallel_us,
            'serial_slowdown_vs_single': serial_us / single_us,
            'parallel_latency_ratio_vs_single': parallel_us / single_us,
            'parallel_core_seconds_vs_single': (parallel_us * data['partitions']) / single_us,
            'candidates_examined_per_query': data['partitions'] * int(key[1:]),
        }
        summary['comparisons'].append(entry)

    text = json.dumps(summary, ensure_ascii=False, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text + '\n')
    print(text)


if __name__ == '__main__':
    main()
