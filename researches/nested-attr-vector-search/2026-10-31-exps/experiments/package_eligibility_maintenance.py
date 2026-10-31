#!/usr/bin/env python3
"""Read-only maintenance aggregation across the two update CSV schemas."""
import argparse
import csv
import json
from pathlib import Path
import statistics

OPS = ('V', 'T', 'M', 'B', 'R', 'D', 'I')


def load(path):
    return json.loads(Path(path).read_text())


def rows(path):
    with Path(path).open(newline='') as handle:
        return list(csv.DictReader(handle))


def dist(values):
    values = sorted(float(v) for v in values)
    if not values:
        return dict(count=0)
    pct = lambda p: values[max(0, -(-int(p * len(values)) // 1) - 1)]
    return dict(count=len(values), mean=statistics.fmean(values), p50=pct(.5), p95=pct(.95),
                p99=pct(.99), maximum=values[-1])


def columns_used(row):
    if 'pools_rebuilt' in row:
        return dict(kind='closure', geometry='geometry_calls', rebuild='rebuild_us',
                    compaction='compactions', inserts='graph_insertions', deletes='graph_deletions')
    return dict(kind='nodeann', compaction='compaction_count', inserts='ann_insertions',
                deletes='ann_deletions', rebuild='tombstone_compaction_us')


def main(raw, out):
    raw = Path(raw).resolve()
    manifest = load(raw / 'manifest.json')
    assert manifest.get('complete') is True and manifest.get('stage') == 'maintenance'
    records = load(raw / 'runs.json')
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    detail, totals = [], []
    for record in records:
        update_rows = rows(str(Path(record['prefix'])) + '.updates.csv')
        layout = columns_used(update_rows[0]) if update_rows else None
        identity = dict(dataset=record['dataset'], trace=record['trace'], variant=record['variant'],
                        ef=record['ef'], repeat=record['repeat'])
        for op in OPS:
            selected = [r for r in update_rows if r['op'] == op]
            if not selected:
                continue
            entry = dict(identity, op=op, events=len(selected), schema=layout['kind'],
                         latency_us=dist([r['latency_us'] for r in selected]),
                         eligibility_update_us=dist([r.get('eligibility_update_us', 0) for r in selected]))
            if 'geometry_calls' in layout:
                entry['geometry_calls'] = sum(int(r[layout['geometry']]) for r in selected)
                entry['rebuild_us'] = sum(float(r.get(layout['rebuild'], 0) or 0) for r in selected)
            else:
                entry['affected_items'] = sum(int(r.get('affected_items', 0)) for r in selected)
                entry['metadata_writes'] = sum(int(r.get('metadata_writes', 0)) for r in selected)
            entry['compactions'] = sum(int(r.get(layout['compaction'], 0) or 0) for r in selected)
            entry['insertions'] = sum(int(r.get(layout['inserts'], 0) or 0) for r in selected)
            entry['deletions'] = sum(int(r.get(layout['deletes'], 0) or 0) for r in selected)
            detail.append(entry)
        if update_rows:
            totals.append(dict(identity, events=len(update_rows),
                               update_us=sum(float(r['latency_us']) for r in update_rows),
                               eligibility_us=sum(float(r.get('eligibility_update_us', 0) or 0) for r in update_rows),
                               geometry_calls=sum(int(r.get('geometry_calls', 0) or 0) for r in update_rows),
                               compactions=sum(int(r.get(layout['compaction'], 0) or 0) for r in update_rows),
                               insertions=sum(int(r.get(layout['inserts'], 0) or 0) for r in update_rows),
                               deletions=sum(int(r.get(layout['deletes'], 0) or 0) for r in update_rows),
                               query_us=record['all_queries']['e2e_us']['mean'] * record['all_queries']['e2e_us']['count'],
                               workload_us=record['all_queries']['e2e_us']['mean'] * record['all_queries']['e2e_us']['count'] +
                                           sum(float(r['latency_us']) for r in update_rows),
                               build_s=record['summary'].get('build_s'),
                               index_bytes=record['summary'].get('index_bytes'),
                               peak_rss_bytes=record['summary'].get('peak_rss_bytes')))
    for name, value in (('maintenance-detail.json', detail), ('maintenance-totals.json', totals)):
        (out / name).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')
    fields = ['dataset', 'trace', 'variant', 'ef', 'repeat', 'op', 'events', 'schema', 'compactions',
              'insertions', 'deletions', 'geometry_calls', 'rebuild_us', 'latency_mean_us',
              'latency_p95_us', 'latency_max_us', 'eligibility_mean_us']
    with (out / 'maintenance-ops.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in detail:
            writer.writerow({k: row.get(k) for k in fields}
                            | {'latency_mean_us': row['latency_us'].get('mean'),
                               'latency_p95_us': row['latency_us'].get('p95'),
                               'latency_max_us': row['latency_us'].get('maximum'),
                               'eligibility_mean_us': row['eligibility_update_us'].get('mean')})
    summary = dict(raw=str(raw), measured_runs=len(records),
                   traces=sorted({r['trace'] for r in records}),
                   variants=sorted({r['variant'] for r in records}),
                   compactions_observed={f"{r['dataset']}/{r['trace']}/{r['variant']}":
                                         sum(x['compactions'] for x in totals
                                             if (x['dataset'], x['trace'], x['variant']) ==
                                             (r['dataset'], r['trace'], r['variant'])) // 3
                                         for r in records},
                   note='Computed from updates.csv only; compactions are observed counters, not plan claims.')
    (out / 'maintenance-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(dict(packaged=str(out), runs=len(records), detail=len(detail))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    main(args.raw, args.out)
