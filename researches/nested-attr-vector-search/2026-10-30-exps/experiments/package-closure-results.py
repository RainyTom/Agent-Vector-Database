#!/usr/bin/env python3
"""Package frozen scheduler evidence; no search, tuning, stage authorization or algorithm changes."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'codes/directory-aware-vector-attr-search/scripts'))
from run_closure import ARMS, distribution, good, ids, metrics, read_trace, rows


def load(path):
    return json.loads(path.read_text())


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def csv_out(path, values):
    fields = sorted({key for row in values for key in row})
    with path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fields)
        writer.writeheader()
        writer.writerows(values)


def group_rows(query_rows, events, kind, live):
    selected = []
    for row in query_rows:
        if not int(row['gt_count']):
            continue
        event = events[int(row['event']) - 1]
        enabled = int(event[4] != '-1') + int(event[5] != '-1' or event[6] != '-1') + int(event[7] != '0')
        if kind == 'nonempty' or (int(row['scope_card']) >= .25 * live and enabled >= 2 and event[9] == '0'):
            selected.append(row)
    return selected


def package(raw, out):
    manifest = load(raw / 'manifest.json')
    if not manifest.get('complete') or manifest['stage'] != 'static':
        raise ValueError('requires a completed static workflow; partial failures stay in raw work directory')
    out.mkdir(parents=True, exist_ok=False)
    for name in ('manifest.json', 'runs.json'):
        shutil.copyfile(raw / name, out / name)
    runs = load(raw / 'runs.json')
    configurations, group_runs, mechanisms, pairwise, examples = [], [], {}, [], []
    raw_cache = {}
    for name, result in manifest['results'].items():
        folder = out / name
        folder.mkdir()
        for filename in ('result.json', 'selection.json', 'calibration.trace', 'confirmation.trace'):
            shutil.copyfile(raw / name / filename, folder / filename)
        audit_path = raw / name / 'audit.audit.csv'
        audit = rows(audit_path)
        mechanism = dict(rows=len(audit), unique_queries=len({r['event'] for r in audit}),
                         unique_sources=len({r['source'] for r in audit}),
                         recursive_checks=sum(int(r['heredity_applicable']) for r in audit),
                         recursive_mismatches=sum(int(r['heredity_applicable']) and int(r['heredity_mismatch']) for r in audit),
                         direct_mismatches_not_theorem_errors=sum(not int(r['heredity_applicable']) and int(r['heredity_mismatch']) for r in audit),
                         raw_path=str(audit_path), raw_sha256=sha(audit_path))
        for control, column in (('R', 'R_edges'), ('G', 'G_edges'), ('H0', 'H0_edges')):
            changed = [r for r in audit if ids(r['local_H_edges']) != ids(r[column])]
            assert len(changed) == sum(int(r['H_vs_' + control + '_diff']) for r in audit)
            mechanism['H_vs_' + control] = dict(changed_source_query_rows=len(changed),
                unique_changed_queries=len({r['event'] for r in changed}),
                symmetric_edge_difference=distribution([len(set(ids(r['local_H_edges'])) ^ set(ids(r[column]))) for r in audit]))
            if changed:
                examples.append(dict(dataset=name, control=control, **changed[0]))
        assert not mechanism['recursive_mismatches']
        mechanisms[name] = mechanism
    for run in runs:
        prefix = Path(run['prefix'])
        for filename, expected in run['raw_sha256'].items():
            assert sha(prefix.parent / filename) == expected, filename
        query_rows = rows(Path(str(prefix) + '.queries.csv'))
        raw_cache[(run['dataset'], run['phase'], run['repeat'], run['ef'], run['arm'])] = query_rows
        events = read_trace(raw / run['dataset'] / (run['phase'] + '.trace'))
        live = sum(int(line.split()[3]) for line in (Path(manifest['input'][run['dataset']]['data']) / 'attrs.txt').read_text().splitlines())
        identity = {key: run[key] for key in ('dataset', 'phase', 'repeat', 'ef', 'arm')}
        for kind in ('nonempty', 'wide_scope_multi_and'):
            selected = group_rows(query_rows, events, kind, live)
            measured = metrics(selected)
            value = dict(identity, group=kind, n=len(selected), unique_qidx=measured['quality']['unique_qidx'],
                         macro=measured['quality']['macro'], pair=measured['quality']['pair'], quality_pass=good(measured['quality']))
            for field in ('e2e_us', 'distance_calls', 'expanded', 'compile_us'):
                for stat, number in measured[field].items():
                    value[field + '_' + stat] = number
            for field in ('dir_us', 'search_us', 'pool_candidates', 'pruned', 'retained', 'witness_tests', 'hierarchy_steps', 'qualification_tests', 'backbone_ids'):
                values = [float(r[field]) for r in selected if field in r]
                value[field + '_mean'] = statistics.fmean(values) if values else None
            group_runs.append(value)
        value = dict(identity, **run['nonempty_metrics']['quality'],
                     empty_count=run['metrics']['quality']['empty'],
                     quality_pass=good(run['nonempty_metrics']['quality']),
                     mean_e2e_us=run['nonempty_metrics']['e2e_us']['mean'],
                     mean_distance_calls=run['nonempty_metrics']['distance_calls']['mean'],
                     mean_expanded=run['nonempty_metrics']['expanded']['mean'],
                     mean_compile_us=run['nonempty_metrics']['compile_us']['mean'],
                     build_s=run['summary']['build_s'], index_bytes=run['summary']['index_bytes'],
                     index_bytes_kind=run['summary']['index_bytes_kind'], peak_rss_bytes=run['summary']['peak_rss_bytes'])
        for field in ('build_graph_s', 'build_pools_s', 'build_geometry_calls', 'metadata_bytes_estimate', 'hnsw_serialized_bytes'):
            value[field] = run['summary'].get(field)
        configurations.append(value)
    # Matched-ef calibration comparisons isolate edge-rule effects from selected-ef effects.
    for name in manifest['results']:
        for ef in manifest['efs']:
            hrows = raw_cache[(name, 'calibration', 0, ef, 'H')]
            for control in ('U', 'G', 'R', 'H0'):
                other = raw_cache[(name, 'calibration', 0, ef, control)]
                assert len(hrows) == len(other)
                selected = []
                for left, right in zip(hrows, other):
                    assert (left['qidx'], left['gt_ids']) == (right['qidx'], right['gt_ids'])
                    if int(left['gt_count']):
                        selected.append((left, right))
                hm, cm = metrics([r for r, _ in selected]), metrics([r for _, r in selected])
                pairwise.append(dict(dataset=name, ef=ef, control=control, nonempty_queries=len(selected),
                    H_quality=hm['quality'], control_quality=cm['quality'], same_quality_pass=good(hm['quality']) and good(cm['quality']),
                    H_mean_expanded=hm['expanded']['mean'], control_mean_expanded=cm['expanded']['mean'],
                    H_mean_e2e_us=hm['e2e_us']['mean'], control_mean_e2e_us=cm['e2e_us']['mean'],
                    H_fewer_expanded_queries=sum(int(h['expanded']) < int(c['expanded']) for h, c in selected),
                    H_equal_expanded_queries=sum(int(h['expanded']) == int(c['expanded']) for h, c in selected)))
    aggregates = []
    for name in manifest['results']:
        for kind in ('nonempty', 'wide_scope_multi_and'):
            for arm in ARMS:
                selected = [r for r in group_runs if r['dataset'] == name and r['group'] == kind and r['arm'] == arm and r['phase'] == 'confirmation']
                entry = dict(dataset=name, group=kind, arm=arm, repeats=len(selected), ef=[r['ef'] for r in selected],
                             n=[r['n'] for r in selected], unique_qidx=[r['unique_qidx'] for r in selected],
                             quality_pass=all(r['quality_pass'] for r in selected),
                             macro=[r['macro'] for r in selected], pair=[r['pair'] for r in selected],
                             repeat_mean_e2e_us=[r['e2e_us_mean'] for r in selected])
                for field in ('e2e_us_mean', 'e2e_us_p50', 'e2e_us_p95', 'e2e_us_p99', 'expanded_mean', 'distance_calls_mean', 'compile_us_mean', 'pruned_mean', 'retained_mean', 'qualification_tests_mean', 'hierarchy_steps_mean'):
                    values = [r[field] for r in selected if r[field] is not None]
                    entry[field] = statistics.fmean(values) if len(values) == manifest['repeats'] else None
                aggregates.append(entry)
    inventory = [dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path)) for path in sorted(raw.rglob('*')) if path.is_file()]
    dump(out / 'raw-inventory.json', inventory)
    dump(out / 'mechanism.json', mechanisms)
    dump(out / 'mechanism-examples.json', examples)
    dump(out / 'configurations.json', configurations)
    csv_out(out / 'configurations.csv', configurations)
    dump(out / 'group-runs.json', group_runs)
    csv_out(out / 'group-runs.csv', group_runs)
    dump(out / 'confirmation.json', aggregates)
    csv_out(out / 'confirmation.csv', aggregates)
    dump(out / 'matched-ef-calibration.json', pairwise)
    dump(out / 'quality-failures.json', [row for row in configurations if not row['quality_pass']])
    decision = {name: dict(dynamic_admitted=result['dynamic_admitted'], quality_pass=result['quality_pass'],
                          chosen=result['chosen'], speed_groups=result['speed_groups'],
                          group_coverage={kind: {arm: [value['quality']['unique_qidx'] for value in values] for arm, values in arms.items()}
                                          for kind, arms in result['groups'].items()},
                          reason_boundary='Only supplied scheduler admits dynamic. Insufficient qidx coverage is not a performance failure.')
                for name, result in manifest['results'].items()}
    dump(out / 'stage-decision.json', decision)
    dump(out / 'packaging.json', dict(raw=str(raw), raw_files=len(inventory), measured_runs=len(runs),
                                    all_recorded_raw_hashes_verified=True, raw_csv_copied=False,
                                    statistic='mean of repeat-specific nonempty means/quantiles; matched-ef comparisons use calibration only',
                                    nodeann_ann_hops_not_compared_to_expanded=True))
    print(json.dumps(dict(packaged=str(out), measured_runs=len(runs), dynamic_admitted={name: value['dynamic_admitted'] for name, value in decision.items()})))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    package(args.raw.resolve(), args.out.resolve())
