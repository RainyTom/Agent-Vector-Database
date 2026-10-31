#!/usr/bin/env python3
"""Fixed-budget, resident static query comparison (two index builds, nine passes).

Examples:
  python scripts/run_packed_query.py --dataset toy=DIR --trace toy=FILE --out NEW --smoke
  python scripts/run_packed_query.py --from-static EXISTING --name toy --out NEW --smoke

No data generation, maintenance, parameter selection, or historical timing reuse.
--smoke skips compilation, permits small synthetic traces, and makes no real-data claim.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import shutil
import subprocess

from generate_dynamic_triad import sha256_file, write_json
from run_closure import (ROOT, SINGLE_THREAD_ENV, file_hashes, pairs, put_trace,
                         read_trace, rows, verify_run)
from run_closure_small_ef import group_rows, pair_rows
from run_eligibility import measured, validate_policy
from run_packed import memory_estimate

REPO = ROOT.parents[1]
DATA_FILES = ('meta.txt', 'base.f32', 'test.f32', 'bindings.txt', 'tree.txt', 'attrs.txt')
REPEATS = 3
EF = 20
PROTOCOL = (
    'Arm-blocked resident repeats: A runs three passes, then P runs cap/hybrid '
    'alternately for three repeats. Warm caches and process-high-water RSS; '
    'not independent index builds, not cold-start, not the prior rotation protocol. '
    'Indexes are built from existing input data, not restored from a serialized disk graph. '
    'Historical timings are never reused; source hashes bind this invocation only.'
)


def choose_input(args):
    """Select exactly one unit, with hash-bound GT reuse only via --from-static."""
    if args.from_static:
        if args.dataset or args.trace:
            raise ValueError('use --from-static OR matching --dataset/--trace pairs')
        previous = args.from_static.resolve(strict=True)
        manifest_path = previous / 'manifest.json'
        old = json.loads(manifest_path.read_text())
        if old.get('stage') != 'static' or old.get('k', 10) != 10:
            raise ValueError('requires a static k=10 manifest from run_packed or run_packed_query')
        if bool(old.get('smoke_only')) != args.smoke:
            raise ValueError('cannot mix synthetic and real evidence')
        available = old['input']
        name = args.name
        if name is None:
            if len(available) != 1:
                raise ValueError('--name required when the manifest contains multiple datasets')
            name = next(iter(available))
        if name not in available:
            raise ValueError('unknown dataset name: ' + name)
        # Apply the same filesystem-safe name validation as explicit inputs.
        pairs([name + '=' + str(previous)])
        info = available[name]
        data = Path(info['data']).resolve(strict=True)
        source = Path(info.get('trace') or info.get('source_trace')).resolve(strict=True)
        actual = {filename: sha256_file(data / filename) for filename in DATA_FILES}
        for filename, digest in actual.items():
            if info.get('files', {}).get(filename) != digest:
                raise ValueError('source data hash missing or changed: ' + filename)
        if info.get('source_trace_sha256') != sha256_file(source):
            raise ValueError('source trace hash missing or changed')
        static = info.get('static', {})
        trace = previous / name / 'static/events.trace'
        tape = previous / name / 'static/oracle.queries.csv'
        for path, key in ((trace, 'trace_sha256'), (tape, 'gt_sha256')):
            if not static.get(key) or sha256_file(path) != static[key]:
                raise ValueError('static artifact hash missing or changed: ' + str(path))
        events = read_trace(trace)
        queries = [e for e in read_trace(source) if e[0] == 'Q']
        if (not events or any(e[0] != 'Q' for e in events) or
                len(events) not in (200, 256) and not args.smoke):
            raise ValueError('existing static trace must contain the first 200 or 256 Qs')
        if events != queries[:len(events)]:
            raise ValueError('existing static trace differs from original source query prefix')
        provenance = dict(directory=str(previous), manifest_sha256=sha256_file(manifest_path),
                          manifest_complete=old.get('complete') is True,
                          trace=str(trace), tape=str(tape), data_hashes_verified=True,
                          trace_hashes_verified=True, gt_hash_verified=True)
        return name, data, source, events, trace, tape, info, provenance
    data, traces = pairs(args.dataset), pairs(args.trace)
    if not data or data.keys() != traces.keys():
        raise ValueError('matching --dataset NAME=DIR and --trace NAME=FILE required')
    name = args.name
    if name is None:
        if len(data) != 1:
            raise ValueError('--name required when multiple explicit datasets are supplied')
        name = next(iter(data))
    if name not in data:
        raise ValueError('unknown dataset name: ' + name)
    queries = [e for e in read_trace(traces[name]) if e[0] == 'Q']
    count = 256 if len(queries) >= 256 else 200 if len(queries) >= 200 else len(queries)
    if not count or count < 200 and not args.smoke:
        raise ValueError('requires 200 source queries (small synthetic traces need --smoke)')
    return name, data[name], traces[name], queries[:count], None, None, None, None


def phase_groups(events, old):
    qidx = {int(e[1]) for e in events}
    saved = old.get('qidx_groups') if old else None
    if saved is not None:
        groups = {phase: {int(q) for q in saved[phase]}
                  for phase in ('calibration', 'confirmation')}
        if groups['calibration'] & groups['confirmation'] or set.union(*groups.values()) != qidx:
            raise ValueError('source calibration/confirmation qidx groups are not a partition')
        return groups
    return {phase: {q for q in qidx if q % 2 == rem}
            for phase, rem in (('calibration', 0), ('confirmation', 1))}


def validate_tape(tape, events):
    """Check frozen static identity before building either measured index.

    Data/trace hashes bind reused oracle content; C++ additionally verifies actual
    scope/eligibility cardinalities against initial metadata on every replay.
    """
    oracle = rows(tape)
    if len(oracle) != len(events):
        raise ValueError('GT tape/trace query count mismatch')
    for i, (row, event) in enumerate(zip(oracle, events)):
        expected = dict(event=i + 1, version=0, query_index=i, qidx=int(event[1]),
                        recursive=int(event[2]), anchor=int(event[3]))
        if any(int(row[key]) != value for key, value in expected.items()):
            raise ValueError('GT tape static query identity mismatch at query ' + str(i))
        truth = [int(value) for value in row['gt_ids'].split(';') if value]
        eligible, scope = int(row['eligible_card']), int(row['scope_card'])
        if (not 0 <= eligible <= scope or int(row['anchor_depth']) < 0 or
                len(truth) != min(10, eligible) or int(row['gt_count']) != len(truth) or
                len(set(truth)) != len(truth) or any(value < 0 for value in truth)):
            raise ValueError('GT tape cardinality/ID mismatch at query ' + str(i))


def grouped(query_rows, events, phase_ids, live):
    groups = group_rows(query_rows, events, phase_ids, live)
    for phase in ('calibration', 'confirmation'):
        groups[phase + '_nonempty'] = groups.pop('historical_' + phase + '_nonempty')
    groups['all_queries'] = list(query_rows)
    groups.update({band: [r for r in query_rows if pred(int(r['eligible_card']))]
                   for band, pred in (('empty', lambda e: e == 0),
                                      ('small', lambda e: 0 < e <= 512),
                                      ('large', lambda e: e > 512))})
    return groups


def comparison(left, right):
    a, b = measured(left), measured(right)
    qa, qb = a['quality'], b['quality']
    available = all(q[key] is not None for q in (qa, qb) for key in ('macro', 'pair'))
    no_worse = available and qa['macro'] >= qb['macro'] and qa['pair'] >= qb['pair']
    ratio = a['e2e_us']['mean'] / b['e2e_us']['mean'] if b['e2e_us']['mean'] else None
    common = {str(t): available and min(qa['macro'], qa['pair'], qb['macro'], qb['pair']) >= t
              for t in (.95, .99)}
    return dict(left_metrics=a, right_metrics=b, same_budget_time_ratio=ratio,
                quality_no_worse=no_worse,
                pareto_query_mean_faster=no_worse and ratio is not None and ratio < 1,
                query_mean_faster_at_targets={target: passed and ratio is not None and ratio < 1
                                             for target, passed in common.items()},
                common_quality_targets=common)


def validate_batch_summary(summary, index, count, first, arm):
    expected = dict(static_batch_pass=index, static_batch_passes=count,
                    static_index_reused=index > 0, build_charged_this_pass=int(index == 0),
                    n_updates=0, pack_final_repack=0)
    for key, value in expected.items():
        if summary.get(key) != value:
            raise ValueError('C++ batch marker mismatch: ' + key)
    if summary.get('arm') != arm:
        raise ValueError('C++ arm mismatch')
    session_build = summary.get('session_build_s')
    if not isinstance(session_build, (int, float)) or not math.isfinite(session_build) or session_build < 0:
        raise ValueError('missing/invalid C++ session build marker')
    if summary.get('build_s') != session_build:
        raise ValueError('build_s is not session provenance')
    if first is not None:
        for key in ('session_build_s', 'build_s', 'build_distance_calls', 'initial_packed_fingerprint',
                    'final_packed_fingerprint', 'canonical_fingerprint'):
            if key in first and summary.get(key) != first[key]:
                raise ValueError('resident session build provenance changed: ' + key)
    if any(summary.get(key, 0) != 0 for key in
           ('final_repack_us', 'final_repack_distance_calls', 'update_total_us')):
        raise ValueError('static batch performed maintenance')


def tracked_sources():
    paths = list((ROOT / 'src').glob('*.cpp')) + list((ROOT / 'src').glob('*.h'))
    paths += list((ROOT / 'scripts').glob('*.py')) + list(ROOT.glob('build*.sh'))
    paths += list((ROOT / 'deps/hnswlib/hnswlib').glob('*.h'))
    return {str(path.relative_to(REPO)): sha256_file(path) for path in sorted(set(paths))}


def run(args):
    name, data, source, events, reused_trace, reused_tape, old, provenance = choose_input(args)
    phase_ids = phase_groups(events, old)
    out = args.out.resolve()
    if any(ord(c) < 32 or ord(c) == 127 for c in str(out)):
        raise ValueError('output path must not contain control characters')
    out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, **SINGLE_THREAD_ENV)
    manifest = dict(complete=False, stage='static', kind='resident_packed_query', smoke_only=args.smoke,
                    k=10, ef=EF, M=16, efConstruction=200, block_size=4096, repeats=REPEATS,
                    arms=['A-graph', 'P-dfs', 'P-dfs-aware'], scan_threshold=0,
                    eligible_scan_threshold=512, selection_performed=False,
                    commands=[], input={}, protocol=PROTOCOL,
                    interpretation='First goal: same-quality query mean below pure A. Common .95/.99 '
                                   'targets are interpretation flags, never execution gates. No .80 or scale gate.',
                    historical_timing_reuse=False, disk_graph_reuse=False,
                    expected_index_builds=2, expected_query_passes=9)
    write_json(out / 'manifest.json', manifest)
    records, comparisons, cache = [], [], {}

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
        manifest['sources'] = tracked_sources()
        estimate = memory_estimate(data)
        info = dict(data=str(data), trace=str(source), source_trace_sha256=sha256_file(source),
                    files={filename: sha256_file(data / filename) for filename in DATA_FILES},
                    qidx_groups={phase: sorted(ids) for phase, ids in phase_ids.items()},
                    grouping_origin='preserved_source_manifest' if old and 'qidx_groups' in old else 'qidx_parity',
                    memory_preflight=estimate)
        manifest['input'][name] = info
        if provenance:
            manifest['source_static'] = provenance
        directory = out / name / 'static'
        directory.mkdir(parents=True)
        trace, tape = directory / 'events.trace', directory / 'oracle.queries.csv'
        if reused_trace:
            shutil.copyfile(reused_trace, trace)
            shutil.copyfile(reused_tape, tape)
            validate_tape(tape, events)
        else:
            put_trace(trace, events)
        if not args.smoke:
            command(['bash', ROOT / 'build_dynamic_triad.sh'], out / 'build-packed-nodeann')
            if not reused_tape:
                command(['bash', ROOT / 'build_closure.sh'], out / 'build-oracle')
        binary = ROOT / 'build/dynamic_triad'
        manifest['binaries'] = {str(binary): sha256_file(binary)}
        if not reused_tape:
            oracle_binary = ROOT / 'build/closure_research'
            manifest['binaries'][str(oracle_binary)] = sha256_file(oracle_binary)
            command([oracle_binary, '--data', data, '--trace', trace, '--out', directory / 'oracle',
                     '--oracle-only', '--k', 10], directory / 'oracle')
            validate_tape(tape, events)
        info['static'] = dict(trace_sha256=sha256_file(trace), gt_sha256=sha256_file(tape),
                              gt_reused=bool(reused_tape), description={'kind': 'initial_static_query_prefix'})
        write_json(out / 'manifest.json', manifest)
        sessions = [('A', [('A-graph', 'off', repeat) for repeat in range(REPEATS)]),
                    ('P', [(label, policy, repeat) for repeat in range(REPEATS)
                           for label, policy in (('P-dfs', 'cap'), ('P-dfs-aware', 'hybrid'))])]
        build_evidence = []
        for arm, points in sessions:
            schedule = directory / (arm + '.batch')
            prefixes = [directory / 'runs' / f'{label}-ef{EF}-rep{repeat}' for label, _, repeat in points]
            schedule.write_text(str(len(points)) + '\n' + ''.join(
                f'{EF} {policy} {json.dumps(str(prefix), ensure_ascii=False)}\n'
                for (_, policy, _), prefix in zip(points, prefixes)))
            cmd = [binary, '--data', data, '--trace', trace, '--arm', arm, '--static-batch', schedule,
                   '--ef', EF, '--k', 10, '--gt-in', tape, '--scan-threshold', 0,
                   '--eligibility-policy', 'off' if arm == 'A' else 'cap', '--eligible-scan-threshold', 512]
            if arm == 'P':
                cmd += ['--pack-layout', 'dfs', '--pack-size', 4096, '--pack-final-repack', 0]
            command(cmd, directory / ('batch-' + arm))
            first = None
            evidence = dict(arm=arm, schedule=str(schedule), schedule_sha256=sha256_file(schedule),
                            log=str(directory / ('batch-' + arm + '.log')), passes=[], charged_builds=0)
            for index, ((label, policy, repeat), prefix) in enumerate(zip(points, prefixes)):
                qr, updates, summary = verify_run(prefix, tape, events)
                if updates:
                    raise ValueError('static replay emitted updates')
                validate_policy(qr, policy, EF, 512)
                validate_batch_summary(summary, index, len(points), first, arm)
                if first is None:
                    first = summary
                if arm == 'A' and any(int(r['scan_distance_calls']) or int(r['scalar_scan_group_calls']) or
                                      int(r['graph_calls']) != int(r['fanout']) for r in qr):
                    raise ValueError('pure NodeANN scan0 contract violated')
                if summary.get('scan_threshold') != 0 or summary.get('eligibility_policy') != policy:
                    raise ValueError('C++ scan/policy summary mismatch')
                groups = grouped(qr, events, phase_ids, estimate['live'])
                record = dict(dataset=name, trace='static', variant=label, arm=arm,
                              layout='node' if arm == 'A' else 'dfs', block_size=4096,
                              policy=policy, ef=EF, repeat=repeat, prefix=str(prefix),
                              all_queries=measured(qr), groups={g: measured(v) for g, v in groups.items()},
                              summary=summary, raw_sha256=file_hashes(prefix), protocol=PROTOCOL)
                records.append(record)
                cache[(repeat, label)] = qr
                evidence['passes'].append({key: summary[key] for key in
                    ('static_batch_pass', 'static_batch_passes', 'static_index_reused',
                     'session_build_s', 'build_charged_this_pass', 'build_distance_calls')})
                evidence['charged_builds'] += summary['build_charged_this_pass']
                write_json(out / 'runs.json', records)
            evidence['log_sha256'] = sha256_file(Path(evidence['log']))
            build_evidence.append(evidence)
        pooled = {}
        for label in ('A-graph', 'P-dfs', 'P-dfs-aware'):
            pooled[label] = grouped([row for repeat in range(REPEATS) for row in cache[(repeat, label)]],
                                    events, phase_ids, estimate['live'])
        for repeat in range(REPEATS):
            for left in ('P-dfs', 'P-dfs-aware'):
                a, b = cache[(repeat, left)], cache[(repeat, 'A-graph')]
                pair_rows(a, b)
                if any(int(r['graph_calls']) > int(s['graph_calls']) for r, s in zip(a, b)):
                    raise ValueError('initial whole-directory packing increased fanout')
                ga, gb = (grouped(qr, events, phase_ids, estimate['live']) for qr in (a, b))
                for group in ga:
                    comparisons.append(dict(dataset=name, repeat=repeat, ef=EF, group=group,
                                            left=left, right='A-graph', **comparison(ga[group], gb[group])))
        pooled_comparisons = [dict(dataset=name, ef=EF, group=group, left=label, right='A-graph',
                                   **comparison(pooled[label][group], pooled['A-graph'][group]))
                              for label in ('P-dfs', 'P-dfs-aware') for group in pooled[label]]
        write_json(out / 'comparisons.json', comparisons)
        with (out / 'points.csv').open('w', newline='') as handle:
            columns = ['dataset', 'trace', 'variant', 'ef', 'repeat', 'group', 'queries', 'nonempty',
                       'macro', 'pair', 'mean_us', 'p50_us', 'p95_us', 'p99_us',
                       'distance_mean', 'graph_calls_mean']
            writer = csv.DictWriter(handle, fieldnames=columns)
            writer.writeheader()
            for record in records:
                for group, m in record['groups'].items():
                    row = {key: record[key] for key in ('dataset', 'trace', 'variant', 'ef', 'repeat')}
                    row.update(group=group, queries=m['e2e_us']['count'], nonempty=m['quality']['nonempty'],
                               macro=m['quality']['macro'], pair=m['quality']['pair'],
                               **{key + '_us': m['e2e_us'][key] for key in ('mean', 'p50', 'p95', 'p99')},
                               distance_mean=m['distance_calls']['mean'],
                               graph_calls_mean=m['extra']['graph_calls']['mean'])
                    writer.writerow(row)
        builds = sum(e['charged_builds'] for e in build_evidence)
        if builds != 2 or len(records) != 9:
            raise ValueError('expected exactly two measured index builds and nine passes')
        summary = dict(complete=True, dataset=name, smoke_only=args.smoke, protocol=PROTOCOL,
                       index_builds=builds, query_passes=len(records), gt_reused=bool(reused_tape),
                       build_evidence=build_evidence,
                       pooled_points={label: {group: measured(qr) for group, qr in groups.items()}
                                      for label, groups in pooled.items()},
                       pooled_comparisons=pooled_comparisons,
                       interpretation=manifest['interpretation'])
        write_json(out / 'summary.json', summary)
        manifest.update(complete=True, index_builds=builds, measured_runs=len(records),
                        build_evidence=build_evidence,
                        output_sha256={filename: sha256_file(out / filename) for filename in
                                       ('runs.json', 'comparisons.json', 'points.csv', 'summary.json')})
        write_json(out / 'manifest.json', manifest)
        print(json.dumps(dict(complete=True, out=str(out), index_builds=builds, query_passes=len(records),
                              result='2 index builds, 9 query passes', gt_reused=bool(reused_tape))))
    except Exception as error:
        manifest['error'] = str(error)
        write_json(out / 'manifest.json', manifest)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dataset', action='append', default=[], metavar='NAME=DIR')
    parser.add_argument('--trace', action='append', default=[], metavar='NAME=FILE')
    parser.add_argument('--from-static', type=Path, help='reuse hash-verified static trace/GT; complete flag not required')
    parser.add_argument('--name', help='select one dataset; required if multiple are present')
    parser.add_argument('--out', type=Path, required=True, help='new output directory only')
    parser.add_argument('--cpu', type=int, help='optional CPU affinity for serial runner processes')
    parser.add_argument('--smoke', action='store_true', help='skip builds; allow small synthetic query prefixes')
    run(parser.parse_args())


if __name__ == '__main__':
    main()
