#!/usr/bin/env python3
"""Read-only aggregation of the fixed ef=15/20 rerun; never searches or selects parameters."""
import argparse
import csv
import json
from pathlib import Path
import statistics

ARMS = ('A', 'U', 'R', 'H')
EFS = (15, 20)
GROUPS = ('all_nonempty', 'historical_calibration_nonempty', 'historical_confirmation_nonempty', 'wide_scope_multi_and')


def load(path):
    return json.loads(path.read_text())


def mean(values):
    values = [v for v in values if v is not None]
    return statistics.fmean(values) if values else None


def aggregate(runs, dataset, ef, arm, group):
    selected = [r for r in runs if r['dataset'] == dataset and r['ef'] == ef and r['arm'] == arm]
    assert len(selected) == 3, (dataset, ef, arm)
    values = [r['groups'][group] for r in selected]
    row = dict(dataset=dataset, ef=ef, arm=arm, group=group, repeats=len(values),
               nonempty=[v['quality']['nonempty'] for v in values],
               unique_qidx=[v['quality']['unique_qidx'] for v in values],
               macro=[v['quality']['macro'] for v in values],
               pair=[v['quality']['pair'] for v in values])
    for field in ('e2e_us', 'distance_calls', 'expanded', 'compile_us'):
        for stat in ('mean', 'p50', 'p95', 'p99'):
            row[f'{field}_{stat}'] = mean([v[field][stat] for v in values])
        row[f'{field}_repeat_means'] = [v[field]['mean'] for v in values]
    row['compile_fraction_of_e2e'] = (row['compile_us_mean'] / row['e2e_us_mean']
                                      if row['compile_us_mean'] is not None and row['e2e_us_mean'] else None)
    return row


def attribution(runs, dataset, ef, arm):
    selected = [r for r in runs if r['dataset'] == dataset and r['ef'] == ef and r['arm'] == arm]
    assert len(selected) == 3
    values = [r['termination_attribution'] for r in selected]
    out = dict(dataset=dataset, ef=ef, arm=arm)
    for key in ('forced_nonempty_query_fraction', 'forced_nonempty_expanded_fraction', 'nonempty_compile_fraction',
                'forced_empty_query_fraction', 'forced_empty_expanded_fraction'):
        out[key] = [v['shares'].get(key) for v in values]
        out[key + '_mean'] = mean(out[key])
    for key in ('queries', 'nonempty', 'not_forced_nonempty', 'forced_nonempty', 'full_scope_nonempty', 'empty', 'forced_empty'):
        out[key] = [v['counts'].get(key) for v in values]
    out['forced_property_violations'] = sum(len(v['forced_property_violations']) for v in values)
    return out


def main(raw, out):
    manifest = load(raw / 'manifest.json')
    assert manifest.get('complete') is True and manifest.get('smoke_only') is False
    assert manifest['k'] == 10 and manifest['efs'] == list(EFS) and manifest['arms'] == list(ARMS)
    assert manifest.get('selection_performed') is False and manifest.get('dynamic_admitted') is False
    runs = load(raw / 'runs.json')
    assert len(runs) == 2 * len(EFS) * len(ARMS) * 3 == 48, len(runs)
    out.mkdir(parents=True, exist_ok=False)
    tables = [aggregate(runs, d, ef, arm, g) for d in sorted({r['dataset'] for r in runs})
              for ef in EFS for arm in ARMS for g in GROUPS]
    with (out / 'table1-points.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=sorted({k for row in tables for k in row}))
        writer.writeheader()
        writer.writerows(tables)
    mechanisms = [attribution(runs, d, ef, arm) for d in sorted({r['dataset'] for r in runs})
                  for ef in EFS for arm in ARMS if arm != 'A']
    comparisons = load(raw / 'comparisons.json')
    same_budget, matched = [], []
    for row in comparisons:
        if 'same_budget_H_over_A' in row:
            same_budget.append(row)
        elif row.get('group') == 'matched_H_R_nonempty':
            matched.append(row)
    payload = dict(raw=str(raw), manifest_sha256=manifest['previous_manifest_sha256'],
                   measured_runs=len(runs), fixed_points=dict(k=10, efs=list(EFS), repeats=3, arms=list(ARMS)),
                   table1=tables, table2=mechanisms,
                   same_budget_H_over_A=same_budget, matched_H_R_nonempty=matched,
                   interpretation=('Fixed points, not a quality-gated selection. same_budget_H_over_A is a '
                                   'same-budget time ratio; it is only a same-quality comparison if the reported '
                                   'macro/pair actually match. Low-recall points are retained throughout.'))
    (out / 'aggregate.json').write_text(json.dumps(payload, indent=2) + '\n')
    print(json.dumps(dict(packaged=str(out), rows=len(tables), arms=len(mechanisms))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    main(args.raw.resolve(), args.out.resolve())
