#!/usr/bin/env python3
"""Eligibility-aware replay and NodeANN audit; serial C++ timing, no Python ANN.

Static: eight main variants at ef15/20, one exact control, two graph-only
NodeANN diagnostics, three rotations. Maintenance: original mixed prefix plus
an explicit compaction envelope; not permission for large-scale experiments.
All points are retained, including low-recall and slow ones.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import platform
import subprocess

from generate_dynamic_triad import sha256_file, write_json
from run_closure import (ROOT, SINGLE_THREAD_ENV, distribution, file_hashes,
                         maintenance_probe, metrics, put_trace, read_trace,
                         verify_run)
from run_closure_small_ef import group_rows, pair_rows, trace_groups

REPO = ROOT.parents[1]
MAIN = [('A-off', 'A', 'off'), ('A-aware', 'A', 'hybrid'),
        ('U-off', 'U', 'off'), ('U-cap', 'U', 'cap'), ('U-aware', 'U', 'hybrid'),
        ('H-off', 'H', 'off'), ('H-cap', 'H', 'cap'), ('H-aware', 'H', 'hybrid')]
EFS = (15, 20)
REPEATS = 3
EXTRA_FIELDS = ('eligibility_us', 'eligible_scan_us', 'effective_ef',
                'qualification_directory_visits', 'qualification_posting_words',
                'qualification_attribute_words', 'graph_calls', 'scalar_scan_group_calls',
                'scanned_live_objects', 'graph_distance_calls', 'scan_distance_calls',
                'merged_candidates')


def measured(rows):
    result = metrics(rows)
    result['extra'] = {key: distribution([float(r[key]) for r in rows if key in r])
                       for key in EXTRA_FIELDS}
    result['paths'] = {path: sum(r.get('execution_path') == path for r in rows)
                       for path in sorted({r.get('execution_path', 'missing') for r in rows})}
    return result


def validate_policy(rows, policy, ef, threshold):
    for r in rows:
        if r.get('eligibility_policy') != policy:
            raise ValueError('policy not executed as requested')
        if policy == 'off':
            continue
        e = int(r['eligible_card'])
        if not e:
            if int(r['distance_calls']) != 0:
                raise ValueError('empty qualified set performed vector distances')
            continue
        exact = policy == 'exact' or (policy == 'hybrid' and e <= max(10, ef, threshold))
        if exact:
            if r['result_ids'] != r['gt_ids'] or int(r['distance_calls']) != e:
                raise ValueError('exact eligible scan failed distance/result invariant')
        if int(r['effective_ef']) > e:
            raise ValueError('effective ef exceeds exact eligible cardinality')


def update_metrics(rows):
    result = {}
    for op in sorted({r['op'] for r in rows}):
        selected = [r for r in rows if r['op'] == op]
        result[op] = dict(events=len(selected), metrics={
            key: distribution([float(r[key]) for r in selected if key in r])
            for key in ('latency_us', 'geometry_calls',
                        'eligibility_update_us', 'rebuild_us', 'compaction_us')})
    return result


def run(args):
    previous = args.previous_raw.resolve(strict=True)
    old = json.loads((previous / 'manifest.json').read_text())
    if old.get('complete') is not True or old.get('stage') != 'static':
        raise ValueError('requires completed original run_closure static manifest')
    if bool(old.get('smoke_only')) != args.smoke:
        raise ValueError('synthetic and real evidence must not be mixed')
    names = args.dataset or sorted(old['input'])
    if len(names) != len(set(names)) or any(n not in old['input'] for n in names):
        raise ValueError('unknown or duplicate dataset')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    manifest = dict(complete=False, kind='eligibility_execution_and_baseline_audit',
                    stage=args.stage, smoke_only=args.smoke, k=10, efs=list(EFS),
                    repeats=REPEATS, eligible_scan_threshold=512, node_scan_threshold=512,
                    efConstruction=200, M=16, pool_size=64, selection_performed=False,
                    large_scale_admitted=False, commands=[], input={},
                    previous_manifest_sha256=sha256_file(previous / 'manifest.json'),
                    platform=platform.platform(), cpu=platform.processor(),
                    note='Same ef is not same recall. Exact count is timed index work, not GT.')
    write_json(out / 'manifest.json', manifest)
    records, comparisons = [], []
    env = dict(os.environ, **SINGLE_THREAD_ENV)

    def command(cmd, prefix):
        prefix.parent.mkdir(parents=True, exist_ok=True)
        manifest['commands'].append(list(map(str, cmd)))
        write_json(out / 'manifest.json', manifest)
        with Path(str(prefix) + '.log').open('w') as handle:
            subprocess.run(list(map(str, cmd)), cwd=REPO, env=env,
                           stdout=handle, stderr=subprocess.STDOUT, check=True)

    try:
        if args.cpu is not None:
            os.sched_setaffinity(0, {args.cpu})
        manifest['cpu_affinity'] = sorted(os.sched_getaffinity(0))
        tracked = [ROOT / 'src' / name for name in
                   ('eligibility_index.h', 'closure_index.h', 'closure_order.h',
                    'closure_research.cpp', 'neighborhood_filter.h',
                    'dynamic_triad.cpp', 'raw_l2_space.h')]
        tracked += [Path(__file__).resolve(), ROOT / 'build_closure.sh', ROOT / 'build_dynamic_triad.sh']
        tracked += [ROOT / 'scripts' / name for name in
                    ('run_closure.py', 'run_closure_small_ef.py', 'run_dynamic_triad.py',
                     'generate_dynamic_triad.py', 'analyze_closure_termination.py')]
        tracked += sorted((ROOT / 'deps/hnswlib/hnswlib').glob('*.h'))
        manifest['sources'] = {str(p.relative_to(REPO)): sha256_file(p) for p in tracked}
        manifest['git_head'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
        if not args.smoke:
            command(['bash', ROOT / 'build_closure.sh'], out / 'build-closure')
            command(['bash', ROOT / 'build_dynamic_triad.sh'], out / 'build-nodeann')
        candidate, baseline = ROOT / 'build/closure_research', ROOT / 'build/dynamic_triad'
        manifest['binaries'] = {str(p): sha256_file(p) for p in (candidate, baseline)}
        for name in names:
            if not name or any(not (c.isalnum() or c in '_-') for c in name):
                raise ValueError('unsafe dataset name')
            original = old['input'][name]
            data, source = Path(original['data']), Path(original['source_trace'])
            for filename, expected in original['files'].items():
                if sha256_file(data / filename) != expected:
                    raise ValueError('dataset SHA mismatch: ' + filename)
            if sha256_file(source) != original['source_trace_sha256']:
                raise ValueError('source trace SHA mismatch')
            source_events = read_trace(source)
            queries = [e for e in source_events if e[0] == 'Q'][:200]
            if not queries or (not args.smoke and len(queries) != 200):
                raise ValueError('real static contract requires first 200 original Qs')
            phases = trace_groups(previous, name, queries)
            live = sum(int(line.split()[3]) for line in (data / 'attrs.txt').read_text().splitlines())
            manifest['input'][name] = dict(original, qidx_groups={p: sorted(v) for p, v in phases.items()})
            traces = [('static', queries, None)]
            if args.stage == 'maintenance':
                mixed, qcount = [], 0
                for event in source_events:
                    mixed.append(event)
                    qcount += event[0] == 'Q'
                    if qcount == 200:
                        break
                envelope, description = maintenance_probe(data, source_events)
                # Explicit V/T, including no-op values, measure actual index update path.
                attrs = [line.split() for line in (data / 'attrs.txt').read_text().splitlines()]
                first = next((i for i, a in enumerate(attrs) if int(a[3])), None)
                if first is not None:
                    a = attrs[first]
                    envelope = [['V', str(first), a[0], str(int(a[1]) ^ 1)],
                                ['T', str(first), str(int(a[2]) ^ 1)], *queries[:8],
                                ['V', str(first), a[0], a[1]], ['T', str(first), a[2]], *envelope]
                traces = [('mixed', mixed, dict(kind='original_prefix_to_200_queries')),
                          ('envelope', envelope, description)]
            for trace_name, events, description in traces:
                trace = out / name / trace_name / 'events.trace'
                put_trace(trace, events)
                oracle = trace.parent / 'oracle'
                command([candidate, '--data', data, '--trace', trace, '--out', oracle,
                         '--oracle-only', '--k', 10], oracle)
                tape = Path(str(oracle) + '.queries.csv')
                if not json.loads(Path(str(oracle) + '.summary.json').read_text()).get('complete'):
                    raise ValueError('oracle incomplete')
                manifest['input'][name][trace_name] = dict(trace=str(trace), sha256=sha256_file(trace),
                    gt_sha256=sha256_file(tape), description=description)
                if args.stage == 'static':
                    grid = [(label, arm, policy, ef, 512) for ef in EFS for label, arm, policy in MAIN]
                    grid += [('Exact', 'U', 'exact', 20, 512)]
                    grid += [('A-graph-only', 'A', 'off', ef, 0) for ef in EFS]
                else:
                    grid = [(label, arm, policy, 20, 512) for label, arm, policy in MAIN
                            if label in ('A-off', 'A-aware', 'U-aware', 'H-aware')]
                cache = {}
                for repeat in range(REPEATS):
                    ordered = grid[repeat:] + grid[:repeat]
                    for label, arm, policy, ef, scan in ordered:
                        prefix = trace.parent / 'runs' / f'{label}-ef{ef}-rep{repeat}'
                        cmd = [baseline if arm == 'A' else candidate, '--data', data, '--trace', trace,
                               '--out', prefix, '--arm', arm, '--ef', ef, '--k', 10, '--gt-in', tape,
                               '--eligibility-policy', policy, '--eligible-scan-threshold', 512]
                        if arm == 'A':
                            cmd += ['--scan-threshold', scan]
                        command(cmd, prefix)
                        qr, ur, summary = verify_run(prefix, tape, events)
                        validate_policy(qr, policy, ef, 512)
                        groups = group_rows(qr, events, phases, live)
                        for band, pred in [('empty', lambda e: e == 0),
                                           ('E_lt_ef', lambda e: 0 < e < ef),
                                           ('E_le_512', lambda e: 0 < e <= 512),
                                           ('E_gt_512', lambda e: e > 512)]:
                            groups[band] = [r for r in qr if pred(int(r['eligible_card']))]
                        record = dict(dataset=name, trace=trace_name, variant=label, arm=arm,
                                      policy=policy, ef=ef, repeat=repeat, prefix=str(prefix),
                                      all_queries=measured(qr), groups={g: measured(r) for g, r in groups.items()},
                                      updates=update_metrics(ur), summary=summary, raw_sha256=file_hashes(prefix))
                        records.append(record)
                        cache[(repeat, ef, label)] = qr
                        write_json(out / 'runs.json', records)
                if args.stage == 'static':
                    for repeat in range(REPEATS):
                        for ef in EFS:
                            for left, right in [('H-aware', 'A-off'), ('H-aware', 'A-aware'),
                                                ('H-aware', 'U-aware'), ('H-cap', 'H-off'),
                                                ('H-aware', 'H-cap'), ('A-aware', 'A-off'),
                                                ('A-graph-only', 'A-off')]:
                                lr, rr = cache[(repeat, ef, left)], cache[(repeat, ef, right)]
                                pair_rows(lr, rr)
                                lm, rm = measured([r for r in lr if int(r['gt_count'])]), measured([r for r in rr if int(r['gt_count'])])
                                lq, rq = lm['quality'], rm['quality']
                                no_worse = lq['macro'] is not None and rq['macro'] is not None and all(
                                    lq[k] >= rq[k] for k in ('macro', 'pair'))
                                ratio = lm['e2e_us']['mean'] / rm['e2e_us']['mean'] if rm['e2e_us']['mean'] else None
                                comparisons.append(dict(dataset=name, repeat=repeat, ef=ef, left=left, right=right,
                                    left_metrics=lm, right_metrics=rm, same_budget_time_ratio=ratio,
                                    recall_no_worse=no_worse,
                                    observed_quality_matched_win=bool(no_worse and ratio is not None and ratio < 1),
                                    note='Within-sample paired evidence only; no significance or unseen-query claim.'))
                    write_json(out / 'comparisons.json', comparisons)
        columns = ['dataset', 'trace', 'variant', 'policy', 'ef', 'repeat', 'group',
                   'queries', 'nonempty', 'unique_qidx', 'macro', 'pair', 'mean_us',
                   'p50_us', 'p95_us', 'p99_us', 'distances', 'expanded', 'compile_us'] + list(EXTRA_FIELDS)
        with (out / 'points.csv').open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=columns)
            writer.writeheader()
            for r in records:
                for group, m in r['groups'].items():
                    q = m['quality']
                    row = {k: r[k] for k in ('dataset', 'trace', 'variant', 'policy', 'ef', 'repeat')}
                    row.update(group=group, queries=m['e2e_us']['count'], nonempty=q['nonempty'],
                        unique_qidx=q['unique_qidx'], macro=q['macro'], pair=q['pair'], mean_us=m['e2e_us']['mean'],
                        p50_us=m['e2e_us']['p50'], p95_us=m['e2e_us']['p95'], p99_us=m['e2e_us']['p99'],
                        distances=m['distance_calls']['mean'], expanded=m['expanded']['mean'],
                        compile_us=m['compile_us']['mean'])
                    row.update({k: v['mean'] for k, v in m['extra'].items()})
                    writer.writerow(row)
        manifest.update(complete=True, measured_runs=len(records))
        write_json(out / 'manifest.json', manifest)
        print(json.dumps(dict(complete=True, runs=len(records), out=str(out), stage=args.stage)))
    except Exception as error:
        manifest['error'] = str(error)
        write_json(out / 'manifest.json', manifest)
        raise


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--previous-raw', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--stage', choices=('static', 'maintenance'), default='static')
    p.add_argument('--dataset', action='append')
    p.add_argument('--cpu', type=int)
    p.add_argument('--smoke', action='store_true', help='synthetic only; binaries must already be built')
    run(p.parse_args())


if __name__ == '__main__':
    main()
