#!/usr/bin/env python3
"""Whole-directory packed HNSW experiments; serial real C++ replay.

No proxy routes, no HNSW modification, no baseline scan fallback. Static keeps
all points. Maintenance charges final full repacking, not hidden migration debt.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess

from generate_dynamic_triad import sha256_file, write_json
from run_closure import (ROOT, SINGLE_THREAD_ENV, file_hashes, maintenance_probe,
                         pairs, put_trace, read_trace, verify_run)
from run_closure_small_ef import group_rows, pair_rows, trace_groups
from run_eligibility import measured, update_metrics, validate_policy

REPO = ROOT.parents[1]
EFS = (15, 20)
REPEATS = 3


def configurations(suite, stage):
    # name, arm, layout, block target, eligibility policy, ef
    if stage == 'maintenance':
        return [('A-graph', 'A', 'node', 4096, 'off', 20),
                ('P-node', 'P', 'node', 4096, 'cap', 20),
                ('P-shuffle', 'P', 'shuffle', 4096, 'cap', 20),
                ('P-dfs', 'P', 'dfs', 4096, 'cap', 20),
                ('P-dfs-aware', 'P', 'dfs', 4096, 'hybrid', 20)]
    arms = [('A-graph', 'A', 'node', 4096, 'off'),
            ('P-node', 'P', 'node', 4096, 'cap'),
            ('P-shuffle', 'P', 'shuffle', 4096, 'cap'),
            ('P-dfs', 'P', 'dfs', 4096, 'cap'),
            ('P-dfs-aware', 'P', 'dfs', 4096, 'hybrid')]
    if suite == 'diagnostic':
        arms += [('P-node-aware', 'P', 'node', 4096, 'hybrid'),
                 ('P-shuffle-aware', 'P', 'shuffle', 4096, 'hybrid'),
                 ('P-dfs-1024', 'P', 'dfs', 1024, 'cap'),
                 ('P-dfs-1024-aware', 'P', 'dfs', 1024, 'hybrid')]
    return [(*a, ef) for ef in EFS for a in arms] + [('Exact', 'P', 'dfs', 4096, 'exact', 20)]


def inputs(args):
    if args.previous_raw:
        if args.dataset or args.trace:
            raise ValueError('use previous-raw OR explicit dataset/trace pairs')
        previous = args.previous_raw.resolve(strict=True)
        manifest = json.loads((previous / 'manifest.json').read_text())
        if not manifest.get('complete') or manifest.get('stage') != 'static':
            raise ValueError('original completed static manifest required')
        if bool(manifest.get('smoke_only')) != args.smoke:
            raise ValueError('cannot mix synthetic and real manifests')
        return {name: (Path(v['data']), Path(v['source_trace']), v, previous)
                for name, v in manifest['input'].items()}
    data, traces = pairs(args.dataset), pairs(args.trace)
    if not data or data.keys() != traces.keys():
        raise ValueError('matching --dataset NAME=DIR and --trace NAME=FILE required')
    return {name: (directory, traces[name], None, None) for name, directory in data.items()}


def memory_estimate(data):
    """Conservative planning estimate, not a promise about allocator RSS."""
    n, dim, nq = map(int, (data / 'meta.txt').read_text().split())
    owners = [int(x) for x in (data / 'bindings.txt').read_text().split()]
    counts = {}
    with (data / 'attrs.txt').open() as handle:
        for owner, line in zip(owners, handle):
            if int(line.split()[3]):
                counts[owner] = counts.get(owner, 0) + 1
    slots = sum(max(16, 2 * x) for x in counts.values())
    # Includes raw vectors, reserved graph payload/links/locks, eligibility arrays,
    # and 64 bytes per label mutex (ABI-dependent, intentionally not called RSS).
    estimated = 4 * dim * (n + nq + slots) + slots * 256 + n * 160 + len(counts) * 65536 * 64
    available = None
    for line in Path('/proc/meminfo').read_text().splitlines():
        if line.startswith('MemAvailable:'):
            available = int(line.split()[1]) * 1024
    return dict(n=n, dim=dim, nq=nq, live=sum(counts.values()), nonempty_directories=len(counts),
                conservative_bytes=estimated, available_bytes=available,
                note='Planning estimate only; actual peak RSS is separately measured. No index tuning to force baseline slowdown.')


def run(args):
    sources = inputs(args)
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, **SINGLE_THREAD_ENV)
    manifest = dict(complete=False, stage=args.stage, suite=args.suite, smoke_only=args.smoke,
                    candidate='whole_directory_packed_hnsw', k=10, efs=EFS, repeats=REPEATS,
                    baseline='pure NodeANN, scan0, eligibility off', M=16, efConstruction=200,
                    selection_performed=False, commands=[], input={}, large_scale_claim=False)
    records, comparisons = [], []
    write_json(out / 'manifest.json', manifest)

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
        tracked = [ROOT / 'src' / n for n in ('dynamic_triad.cpp', 'packed_index.h',
                   'eligibility_index.h', 'neighborhood_filter.h', 'raw_l2_space.h',
                   'closure_research.cpp', 'closure_index.h', 'closure_order.h')]
        tracked += [ROOT / 'scripts' / n for n in ('run_packed.py', 'prepare_packed_sift.py',
                    'run_eligibility.py', 'run_closure.py', 'run_closure_small_ef.py',
                    'run_dynamic_triad.py', 'generate_dynamic_triad.py', 'analyze_closure_termination.py')]
        tracked += [ROOT / 'build_dynamic_triad.sh', ROOT / 'build_closure.sh']
        tracked += sorted((ROOT / 'deps/hnswlib/hnswlib').glob('*.h'))
        manifest['sources'] = {str(p.relative_to(REPO)): sha256_file(p) for p in tracked}
        for name, (data, trace, old, previous) in sources.items():
            estimate = memory_estimate(data)
            manifest['input'][name] = dict(data=str(data), trace=str(trace), memory_preflight=estimate)
            if estimate['available_bytes'] and estimate['conservative_bytes'] > .8 * estimate['available_bytes']:
                raise RuntimeError('memory preflight exceeds 80% available RAM; do not silently shrink dataset: ' + name)
        if not args.smoke:
            command(['bash', ROOT / 'build_dynamic_triad.sh'], out / 'build-packed-nodeann')
            command(['bash', ROOT / 'build_closure.sh'], out / 'build-oracle')
        binary, oracle_binary = ROOT / 'build/dynamic_triad', ROOT / 'build/closure_research'
        manifest['binaries'] = {str(p): sha256_file(p) for p in (binary, oracle_binary)}
        for name, (data, source, old, previous) in sources.items():
            all_events = read_trace(source)
            query_limit = 200 if previous else 256
            queries = [e for e in all_events if e[0] == 'Q'][:query_limit]
            if not queries or (not args.smoke and len(queries) != query_limit):
                raise ValueError('insufficient source queries: ' + name)
            if old:
                for filename, expected in old['files'].items():
                    if sha256_file(data / filename) != expected:
                        raise ValueError('original input changed: ' + filename)
                if sha256_file(source) != old['source_trace_sha256']:
                    raise ValueError('original source trace changed')
                phase_ids = trace_groups(previous, name, queries)
            else:
                phase_ids = {phase: {int(e[1]) for e in queries if int(e[1]) % 2 == rem}
                             for phase, rem in (('calibration', 0), ('confirmation', 1))}
            info = manifest['input'][name]
            info['files'] = {p.name: sha256_file(p) for p in (data / n for n in
                             ('meta.txt', 'base.f32', 'test.f32', 'bindings.txt', 'tree.txt', 'attrs.txt'))}
            info['source_trace_sha256'] = sha256_file(source)
            info['qidx_groups'] = {p: sorted(v) for p, v in phase_ids.items()}
            if (data / 'preparation.json').exists():
                info['preparation'] = json.loads((data / 'preparation.json').read_text())
                info['preparation_sha256'] = sha256_file(data / 'preparation.json')
            traces = [('static', queries, {})]
            if args.stage == 'maintenance':
                mixed, count = [], 0
                for e in all_events:
                    mixed.append(e); count += e[0] == 'Q'
                    if count == query_limit:
                        break
                envelope, description = maintenance_probe(data, all_events)
                traces = [('mixed', mixed, {'kind': 'original_mixed_prefix'}), ('envelope', envelope, description)]
            for trace_name, events, description in traces:
                directory = out / name / trace_name
                trace = directory / 'events.trace'
                put_trace(trace, events)
                oracle = directory / 'oracle'
                command([oracle_binary, '--data', data, '--trace', trace, '--out', oracle,
                         '--oracle-only', '--k', 10], oracle)
                tape = Path(str(oracle) + '.queries.csv')
                info[trace_name] = dict(trace_sha256=sha256_file(trace), gt_sha256=sha256_file(tape), description=description)
                grid = configurations(args.suite, args.stage)
                cache = {}
                for repeat in range(REPEATS):
                    for label, arm, layout, block, policy, ef in grid[repeat:] + grid[:repeat]:
                        prefix = directory / 'runs' / f'{label}-ef{ef}-rep{repeat}'
                        cmd = [binary, '--data', data, '--trace', trace, '--out', prefix, '--arm', arm,
                               '--ef', ef, '--k', 10, '--gt-in', tape, '--scan-threshold', 0,
                               '--eligibility-policy', policy, '--eligible-scan-threshold', 512]
                        if arm == 'P':
                            cmd += ['--pack-layout', layout, '--pack-size', block,
                                    '--pack-final-repack', int(args.stage == 'maintenance')]
                        command(cmd, prefix)
                        qr, ur, summary = verify_run(prefix, tape, events)
                        validate_policy(qr, policy, ef, 512)
                        if arm == 'A' and any(int(r['scan_distance_calls']) or int(r['scalar_scan_group_calls']) or
                                               int(r['graph_calls']) != int(r['fanout']) for r in qr):
                            raise ValueError('pure NodeANN contract violated')
                        groups = group_rows(qr, events, phase_ids, info['memory_preflight']['live'])
                        for phase in ('calibration', 'confirmation'):
                            groups[phase + '_nonempty'] = groups.pop('historical_' + phase + '_nonempty')
                        # Mutually exclusive bands; E<ef is an extra diagnostic, never summed with E<=512.
                        groups.update({band: [r for r in qr if pred(int(r['eligible_card']))]
                                       for band, pred in [('empty', lambda e: e == 0),
                                                          ('small', lambda e: 0 < e <= 512),
                                                          ('large', lambda e: e > 512)]})
                        record = dict(dataset=name, trace=trace_name, variant=label, arm=arm, layout=layout,
                                      block_size=block, policy=policy, ef=ef, repeat=repeat, prefix=str(prefix),
                                      all_queries=measured(qr), groups={g: measured(v) for g, v in groups.items()},
                                      updates=update_metrics(ur), summary=summary, raw_sha256=file_hashes(prefix))
                        records.append(record); cache[(repeat, label, ef)] = qr
                        write_json(out / 'runs.json', records)
                if args.stage == 'static':
                    for repeat in range(REPEATS):
                        for ef in EFS:
                            for left, right in [('P-dfs', 'A-graph'), ('P-dfs', 'P-node'),
                                                ('P-dfs', 'P-shuffle'), ('P-dfs-aware', 'P-dfs')]:
                                a, b = cache[(repeat, left, ef)], cache[(repeat, right, ef)]
                                pair_rows(a, b)
                                for r, s in zip(a, b):
                                    if right == 'A-graph' and int(r['graph_calls']) > int(s['graph_calls']):
                                        raise ValueError('initial whole-directory packing increased fanout')
                                for group in ('all_nonempty', 'confirmation_nonempty'):
                                    selected = lambda rows: [r for r in rows if int(r['gt_count']) and
                                        (group == 'all_nonempty' or int(r['qidx']) in phase_ids['confirmation'])]
                                    ma, mb = measured(selected(a)), measured(selected(b))
                                    qa, qb = ma['quality'], mb['quality']
                                    available = qa['macro'] is not None and qb['macro'] is not None
                                    comparisons.append(dict(dataset=name, repeat=repeat, ef=ef, group=group, left=left, right=right,
                                        left_metrics=ma, right_metrics=mb,
                                        same_budget_time_ratio=ma['e2e_us']['mean']/mb['e2e_us']['mean'] if mb['e2e_us']['mean'] else None,
                                        common_quality_targets={str(t): available and min(qa['macro'], qa['pair'], qb['macro'], qb['pair']) >= t
                                                                for t in (.95, .99)},
                                        quality_no_worse=available and qa['macro'] >= qb['macro'] and qa['pair'] >= qb['pair']))
                write_json(out / 'comparisons.json', comparisons)
        with (out / 'points.csv').open('w', newline='') as handle:
            columns = ['dataset', 'trace', 'variant', 'ef', 'repeat', 'group', 'nonempty',
                       'macro', 'pair', 'mean_us', 'p95_us', 'distance_mean', 'graph_calls_mean']
            writer = csv.DictWriter(handle, fieldnames=columns); writer.writeheader()
            for record in records:
                for group, m in record['groups'].items():
                    q = m['quality']
                    row = {k: record[k] for k in ('dataset', 'trace', 'variant', 'ef', 'repeat')}
                    row.update(group=group, nonempty=q['nonempty'], macro=q['macro'], pair=q['pair'],
                               mean_us=m['e2e_us']['mean'], p95_us=m['e2e_us']['p95'],
                               distance_mean=m['distance_calls']['mean'], graph_calls_mean=m['extra']['graph_calls']['mean'])
                    writer.writerow(row)
        manifest.update(complete=True, measured_runs=len(records))
        write_json(out / 'manifest.json', manifest)
        print(json.dumps(dict(complete=True, measured_runs=len(records), out=str(out), stage=args.stage)))
    except Exception as error:
        manifest['error'] = str(error); write_json(out / 'manifest.json', manifest); raise


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--previous-raw', type=Path)
    p.add_argument('--dataset', action='append', default=[])
    p.add_argument('--trace', action='append', default=[])
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--suite', choices=('diagnostic', 'scale'), default='diagnostic')
    p.add_argument('--stage', choices=('static', 'maintenance'), default='static')
    p.add_argument('--cpu', type=int)
    p.add_argument('--smoke', action='store_true', help='synthetic only; build binaries first')
    run(p.parse_args())


if __name__ == '__main__':
    main()
