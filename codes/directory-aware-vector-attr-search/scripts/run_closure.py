#!/usr/bin/env python3
"""Serial, hash-bound closure experiments. Python does no ANN or vector GT.

Static: frozen initial metadata, qidx-disjoint calibration/confirmation.
Dynamic: exact original event prefix, admitted only by a completed static gate.
All raw outputs and failed quality points are retained; output directories must
be new. --smoke runs the same pipeline but cannot authorize a research gate.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import sys
from collections import Counter

from generate_dynamic_triad import sha256_file, write_json
from run_dynamic_triad import QUERY_COLUMNS, SINGLE_THREAD_ENV, ids

ROOT = Path(__file__).resolve().parents[1]
ARMS = ('A', 'U', 'G', 'R', 'H', 'H0')
QUALITY = 0.999


def pairs(values):
    result = {}
    for value in values:
        name, sep, path = value.partition('=')
        if not sep or not name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in name):
            raise ValueError('expected NAME=PATH with a filesystem-safe NAME')
        if name in result:
            raise ValueError('duplicate dataset name: ' + name)
        result[name] = Path(path).resolve(strict=True)
    return result


def read_trace(path):
    lines = path.read_text().splitlines()
    if not lines or len(lines) != int(lines[0]) + 1:
        raise ValueError('trace event count mismatch: ' + str(path))
    events = []
    counts = {'Q': 9, 'V': 3, 'T': 2, 'M': 2, 'B': 2, 'R': 2, 'D': 1, 'I': 5}
    for line in lines[1:]:
        fields = line.split()
        if not fields or fields[0] not in counts or len(fields) != counts[fields[0]] + 1:
            raise ValueError('invalid event: ' + line)
        tuple(map(int, fields[1:]))
        events.append(fields)
    return events


def maintenance_probe(data, source):
    """Small metadata-derived envelope, not a sampled application workload."""
    tree = (data / 'tree.txt').read_text().splitlines()
    parent = [None] * int(tree[0])
    for row in tree[1:]:
        node, p = map(int, row.split())
        parent[node] = p
    root = parent.index(-1)
    owners = list(map(int, (data / 'bindings.txt').read_text().split()))
    attrs = [list(map(int, row.split())) for row in (data / 'attrs.txt').read_text().splitlines()]
    live = [i for i, a in enumerate(attrs) if a[3]]
    children = [[] for _ in parent]
    for node, p in enumerate(parent):
        if p >= 0:
            children[p].append(node)
    counts, enter, leave = [0] * len(parent), {}, {}
    for i in live:
        counts[owners[i]] += 1
    stack = [(root, False)]
    while stack:
        node, done = stack.pop()
        if done:
            leave[node] = len(enter)
            if parent[node] >= 0:
                counts[parent[node]] += counts[node]
        else:
            enter[node] = len(enter)
            stack.append((node, True))
            stack.extend((child, False) for child in reversed(children[node]))
    inside = lambda node, ancestor: enter[ancestor] <= enter[node] < leave[ancestor]
    relocation = None
    for node in sorted((i for i in range(len(parent)) if i != root and counts[i]),
                       key=lambda i: (abs(counts[i] - .1 * len(live)), i)):
        target = next((i for i in range(len(parent)) if i != parent[node] and not inside(i, node)), None)
        if target is not None:
            relocation = (node, target)
            break
    templates = {}
    for event in source:
        if event[0] == 'Q':
            templates.setdefault(event[1], event)
            if len(templates) == 8:
                break
    anchors = [root] + (list(relocation) if relocation else [])
    queries = []
    for event in templates.values():
        for anchor in anchors:
            q = event.copy()
            q[2], q[3] = '1', str(anchor)
            queries.append(q)
    events = queries.copy()
    if relocation:
        node, target = relocation
        events += [['R', str(node), str(target)]] + queries
        events += [['R', str(node), str(parent[node])]] + queries
        moved = [i for i in live if inside(owners[i], node)]
        events += [['B', str(node), str(target)]] + queries
        events += [['M', str(i), str(owners[i])] for i in moved] + queries
    # Reach the core's fixed 1/3 tombstone threshold rather than hiding debt.
    deleted = live[:math.ceil(len(live) / 3)]
    events += [['D', str(i)] for i in deleted] + queries
    if deleted:
        reserve = next((i for i, a in enumerate(attrs) if not a[3]), deleted[0])
        a = attrs[reserve]
        events += [['I', str(reserve), str(owners[reserve]), *map(str, a[:3])]] + queries
    return events, dict(kind='synthetic_maintenance_envelope', relocation=relocation,
                        initial_live=len(live), deleted_to_compaction=len(deleted),
                        topology_unavailable=relocation is None,
                        note='B restored by charged M events; D prefix reaches one global compaction. No frequency realism claim.')


def put_trace(path, events):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(str(len(events)) + '\n' + ''.join(' '.join(e) + '\n' for e in events))


def rows(path):
    with path.open(newline='') as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None or len(set(reader.fieldnames)) != len(reader.fieldnames):
            raise ValueError('missing/duplicate CSV columns: ' + str(path))
        result = list(reader)
        if any(None in row or any(v is None for v in row.values()) for row in result):
            raise ValueError('malformed CSV rows: ' + str(path))
        return result


def number(row, key):
    value = float(row[key])
    if not math.isfinite(value) or value < 0:
        raise ValueError('invalid nonnegative statistic: ' + key)
    return value


def integer(row, key):
    value = int(row[key])
    if value < 0:
        raise ValueError('negative integer: ' + key)
    return value


def percentile(values, p):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(p * len(ordered)) - 1)]


def distribution(values):
    return dict(count=len(values), mean=statistics.fmean(values) if values else None,
                p50=percentile(values, .50), p95=percentile(values, .95), p99=percentile(values, .99),
                maximum=max(values) if values else None)


def quality(query_rows):
    nonempty = [r for r in query_rows if int(r['gt_count'])]
    total = sum(int(r['gt_count']) for r in nonempty)
    return dict(nonempty=len(nonempty), empty=len(query_rows) - len(nonempty),
                unique_qidx=len({r['qidx'] for r in nonempty}),
                macro=statistics.fmean(float(r['recall']) for r in nonempty) if nonempty else None,
                pair=sum(int(r['hit_count']) for r in nonempty) / total if total else None)


def good(q):
    return q['macro'] is not None and min(q['macro'], q['pair']) >= QUALITY


def metrics(query_rows):
    return dict(quality=quality(query_rows),
                e2e_us=distribution([float(r['e2e_us']) for r in query_rows]),
                distance_calls=distribution([int(r['distance_calls']) for r in query_rows]),
                expanded=distribution([int(r['expanded']) for r in query_rows if 'expanded' in r]),
                compile_us=distribution([float(r['compile_us']) for r in query_rows if 'compile_us' in r]))


def file_hashes(prefix):
    return {p.name: sha256_file(p) for p in sorted(prefix.parent.glob(prefix.name + '.*')) if p.is_file()}


def verify_run(prefix, tape, events):
    summary = json.loads(Path(str(prefix) + '.summary.json').read_text())
    if summary.get('complete') is not True:
        raise ValueError('incomplete C++ replay: ' + str(prefix))
    result = rows(Path(str(prefix) + '.queries.csv'))
    oracle = rows(tape)
    expected = [(i, e) for i, e in enumerate(events, 1) if e[0] == 'Q']
    if len(result) != len(oracle) or len(result) != len(expected):
        raise ValueError('query/tape length mismatch')
    for r, gt, (event, fields) in zip(result, oracle, expected):
        if any(key not in r for key in QUERY_COLUMNS):
            raise ValueError('incomplete query schema')
        for key in QUERY_COLUMNS:
            if key not in ('gt_ids', 'result_ids'):
                number(r, key)
        if int(r['event']) != event or int(r['qidx']) != int(fields[1]):
            raise ValueError('trace/query identity mismatch')
        for key in QUERY_COLUMNS[:9] + ('gt_count', 'gt_ids'):
            if r[key] != gt[key]:
                raise ValueError('GT tape mismatch in ' + key)
        actual, truth = ids(r['result_ids']), ids(r['gt_ids'])
        if len(actual) != len(set(actual)) or len(truth) != len(set(truth)):
            raise ValueError('duplicate result/GT IDs')
        hits = len(set(actual) & set(truth))
        recall = hits / len(truth) if truth else float(not actual)
        if int(r['hit_count']) != hits or abs(float(r['recall']) - recall) > 1e-9:
            raise ValueError('hit/recall does not match IDs')
        if len(truth) != int(r['gt_count']) or len(actual) != int(r['valid_count']):
            raise ValueError('result cardinality mismatch')
        for key in ('invalid_count', 'duplicate_count', 'state_errors', 'gt_distance_calls'):
            if key in r and integer(r, key):
                raise ValueError('nonzero ' + key + ' in measured run')
    updates = rows(Path(str(prefix) + '.updates.csv'))
    expected_updates = [(i, e[0]) for i, e in enumerate(events, 1) if e[0] != 'Q']
    if [(int(r['event']), r['op']) for r in updates] != expected_updates:
        raise ValueError('update replay mismatch')
    for r in updates:
        number(r, 'latency_us')
    if summary.get('gt_mode') != 'precomputed':
        raise ValueError('measured replay did not use precomputed GT')
    return result, updates, summary


class Experiment:
    def __init__(self, args):
        self.args = args
        self.out = args.out.resolve()
        self.out.mkdir(parents=True, exist_ok=False)
        self.env = dict(os.environ, **SINGLE_THREAD_ENV)
        if args.cpu is not None:
            os.sched_setaffinity(0, {args.cpu})
        self.commands = []
        self.records = []
        self.manifest = dict(complete=False, stage=args.stage, smoke_only=args.smoke,
                             platform=platform.platform(), cpu_affinity=sorted(os.sched_getaffinity(0)),
                             binaries={str(p): sha256_file(p) for p in (args.binary, args.baseline)},
                             efs=args.efs, repeats=args.repeats, quality=QUALITY, pool_size=64,
                             k=args.k, scan_threshold=512, input={}, commands=self.commands)
        write_json(self.out / 'manifest.json', self.manifest)

    def run(self, command, prefix):
        prefix.parent.mkdir(parents=True, exist_ok=True)
        self.commands.append([str(s) for s in command])
        write_json(self.out / 'manifest.json', self.manifest)
        with Path(str(prefix) + '.log').open('w') as log:
            log.write(shlex.join(map(str, command)) + '\n')
            log.flush()
            subprocess.run(list(map(str, command)), env=self.env, stdout=log, stderr=subprocess.STDOUT, check=True)

    def oracle(self, data, trace, prefix):
        self.run([self.args.binary, '--data', data, '--trace', trace, '--out', prefix,
                  '--oracle-only', '--k', self.args.k], prefix)
        summary = json.loads(Path(str(prefix) + '.summary.json').read_text())
        if summary.get('complete') is not True:
            raise ValueError('incomplete canonical oracle')
        tape = Path(str(prefix) + '.queries.csv')
        return tape

    def measure(self, name, data, trace, events, tape, arm, ef, phase, rep):
        prefix = self.out / name / phase / f'{arm}-ef{ef}-rep{rep}'
        command = [self.args.baseline if arm == 'A' else self.args.binary,
                   '--data', data, '--trace', trace, '--out', prefix, '--arm', arm,
                   '--ef', ef, '--k', self.args.k, '--gt-in', tape]
        if arm == 'A':
            command += ['--scan-threshold', 512]
        self.run(command, prefix)
        qr, ur, summary = verify_run(prefix, tape, events)
        record = dict(dataset=name, phase=phase, arm=arm, ef=ef, repeat=rep,
                      prefix=str(prefix), metrics=metrics(qr), summary=summary,
                      nonempty_metrics=metrics([r for r in qr if int(r['gt_count'])]),
                      update_us={op: distribution([float(r['latency_us']) for r in ur if r['op'] == op])
                                 for op in sorted({r['op'] for r in ur})},
                      workload_us=sum(float(r['e2e_us']) for r in qr) + sum(float(r['latency_us']) for r in ur),
                      raw_sha256=file_hashes(prefix))
        record['workload_with_build_us'] = record['workload_us'] + summary['build_s'] * 1e6
        self.records.append(record)
        write_json(self.out / 'runs.json', self.records)
        return record, qr

    def input(self, name, data, trace):
        hashes = {p.name: sha256_file(p) for p in sorted(data.iterdir())
                  if p.name in ('base.f32', 'test.f32', 'bindings.txt', 'attrs.txt', 'tree.txt', 'meta.txt')}
        self.manifest['input'][name] = dict(data=str(data), source_trace=str(trace),
                                           source_trace_sha256=sha256_file(trace), files=hashes)
        write_json(self.out / 'manifest.json', self.manifest)
        return sum(int(line.split()[3]) for line in (data / 'attrs.txt').read_text().splitlines())

    def static(self, name, data, source_trace):
        live = self.input(name, data, source_trace)
        source = read_trace(source_trace)
        queries = [e for e in source if e[0] == 'Q'][:self.args.max_queries]
        unique = sorted({int(e[1]) for e in queries})
        if len(unique) < 2:
            raise ValueError('need at least two distinct qidx groups')
        calibration_ids = set(unique[::2])
        split = {p: [e for e in queries if (int(e[1]) in calibration_ids) == (p == 'calibration')]
                 for p in ('calibration', 'confirmation')}
        selection = {}
        paths = {}
        for phase, events in split.items():
            trace = self.out / name / (phase + '.trace')
            put_trace(trace, events)
            tape = self.oracle(data, trace, self.out / name / (phase + '-oracle'))
            paths[phase] = (trace, tape)
        audit_prefix = self.out / name / 'audit'
        self.run([self.args.binary, '--data', data, '--trace', paths['confirmation'][0],
                  '--out', audit_prefix, '--audit-only', '--audit-count', 8], audit_prefix)
        audit = rows(Path(str(audit_prefix) + '.audit.csv'))
        if any(integer(r, 'heredity_applicable') and integer(r, 'heredity_mismatch') for r in audit):
            raise ValueError('subtree restriction invariant failed')
        mechanism = {key: sum(integer(r, key) for r in audit)
                     for key in ('H_vs_R_diff', 'H_vs_G_diff', 'H_vs_H0_diff')}
        pool_states = set()
        # Rotate arm order for every ef, then every confirmation repeat.
        for shift, ef in enumerate(self.args.efs):
            order = ARMS[shift % len(ARMS):] + ARMS[:shift % len(ARMS)]
            for arm in order:
                trace, tape = paths['calibration']
                record, qr = self.measure(name, data, trace, split['calibration'], tape, arm, ef, 'calibration', 0)
                nonempty = [r for r in qr if int(r['gt_count'])]
                selection.setdefault(arm, []).append(dict(ef=ef, quality=quality(qr),
                    mean_us=statistics.fmean(float(r['e2e_us']) for r in nonempty) if nonempty else None))
                if arm != 'A':
                    pool_states.add(record['summary']['pool_fingerprint'])
        if len(pool_states) != 1:
            raise ValueError('candidate pools differ across static ablations')
        chosen = {}
        for arm, points in selection.items():
            valid = [p for p in points if good(p['quality'])]
            point = min(valid, key=lambda p: (p['mean_us'], p['ef'])) if valid else max(points, key=lambda p: p['ef'])
            chosen[arm] = dict(ef=point['ef'], calibration_pass=bool(valid))
        # Freeze BEFORE any confirmation observation.
        write_json(self.out / name / 'selection.json', dict(grid=selection, chosen=chosen))
        confirmed = {arm: [] for arm in ARMS}
        for rep in range(self.args.repeats):
            order = ARMS[rep % len(ARMS):] + ARMS[:rep % len(ARMS)]
            for arm in order:
                trace, tape = paths['confirmation']
                record, qr = self.measure(name, data, trace, split['confirmation'], tape, arm,
                                          chosen[arm]['ef'], 'confirmation', rep)
                confirmed[arm].append(qr)
        event_fields = {i: e for i, e in enumerate(split['confirmation'], 1)}
        def group(qr, kind):
            result = []
            for r in qr:
                e = event_fields[int(r['event'])]
                enabled = (int(e[4]) != -1) + (int(e[5]) != -1 or int(e[6]) != -1) + (int(e[7]) != 0)
                if int(r['gt_count']) and (kind == 'nonempty' or
                    (int(r['scope_card']) >= .25 * live and enabled >= 2 and int(e[9]) == 0)):
                    result.append(r)
            return result
        groups = {}
        for kind in ('nonempty', 'wide_scope_multi_and'):
            groups[kind] = {arm: [metrics(group(qr, kind)) for qr in repeats]
                            for arm, repeats in confirmed.items()}
        quality_pass = all(chosen[a]['calibration_pass'] and all(good(quality(qr)) for qr in confirmed[a]) for a in ('A', 'H'))
        speed_groups = []
        for kind, data_group in groups.items():
            a, h = data_group['A'], data_group['H']
            enough = min(m['quality']['unique_qidx'] for m in h) >= 20
            ratios = [y['e2e_us']['mean'] / x['e2e_us']['mean'] for x, y in zip(a, h)
                      if x['e2e_us']['mean'] and y['e2e_us']['mean'] is not None]
            if enough and len(ratios) == self.args.repeats and max(ratios) <= 1 and statistics.median(ratios) <= .90:
                speed_groups.append(kind)
        admitted = (not self.args.smoke and self.args.repeats >= 3 and quality_pass and
                    bool(speed_groups) and mechanism['H_vs_R_diff'] > 0)
        result = dict(complete=True, stage='static', smoke_only=self.args.smoke, chosen=chosen,
                      groups=groups, mechanism=mechanism, audit_rows=len(audit),
                      experiment_contract=dict(k=self.args.k, binaries=self.manifest['binaries']),
                      hereditary_checks=sum(integer(r, 'heredity_applicable') for r in audit),
                      quality_pass=quality_pass, speed_groups=speed_groups, dynamic_admitted=admitted,
                      inputs=self.manifest['input'][name], pool_fingerprint=next(iter(pool_states)),
                      limitations=['Old query vectors; qidx-disjoint, not a new independent test set.',
                                   'Initial metadata frozen; updates discarded only in static stage.',
                                   'Audit samples sources, not a proof for all real-data edges.'],
                      tape_sha256={p: sha256_file(tape) for p, (_, tape) in paths.items()})
        write_json(self.out / name / 'result.json', result)
        return result

    def dynamic(self, name, data, source_trace, prior):
        live = self.input(name, data, source_trace)
        del live
        if not prior.get('complete'):
            raise ValueError('dynamic stage requires a completed static result')
        if self.args.smoke:
            if not prior.get('smoke_only'):
                raise ValueError('smoke dynamic replay requires a smoke static result')
        elif not prior.get('dynamic_admitted') or prior.get('smoke_only'):
            raise ValueError('dynamic stage requires an admitted non-smoke static result')
        current = self.manifest['input'][name]
        if prior['experiment_contract'] != dict(k=self.args.k, binaries=self.manifest['binaries']):
            raise ValueError('dynamic executable hashes or k differ from static calibration')
        if current['files'] != prior['inputs']['files'] or current['source_trace_sha256'] != prior['inputs']['source_trace_sha256']:
            raise ValueError('dynamic inputs differ from static frozen inputs')
        events, count = [], 0
        for e in read_trace(source_trace):
            events.append(e)
            count += e[0] == 'Q'
            if count == self.args.max_queries:
                break
        if not count or not any(e[0] != 'Q' for e in events):
            raise ValueError('dynamic prefix needs both Q and update events')
        trace = self.out / name / 'dynamic.trace'
        put_trace(trace, events)
        tape = self.oracle(data, trace, self.out / name / 'dynamic-oracle')
        audit_prefix = self.out / name / 'dynamic-audit'
        self.run([self.args.binary, '--data', data, '--trace', trace, '--out', audit_prefix,
                  '--audit-only', '--audit-count', 8], audit_prefix)
        results = []
        for rep in range(self.args.repeats):
            for arm in (('A', 'H') if rep % 2 == 0 else ('H', 'A')):
                record, _ = self.measure(name, data, trace, events, tape, arm,
                                         prior['chosen'][arm]['ef'], 'dynamic', rep)
                results.append(record)
        probe_events, probe_meta = maintenance_probe(data, read_trace(source_trace))
        probe_trace = self.out / name / 'maintenance-probe.trace'
        put_trace(probe_trace, probe_events)
        probe_tape = self.oracle(data, probe_trace, self.out / name / 'maintenance-probe-oracle')
        probe_audit = self.out / name / 'maintenance-probe-audit'
        self.run([self.args.binary, '--data', data, '--trace', probe_trace, '--out', probe_audit,
                  '--audit-only', '--audit-count', 8], probe_audit)
        probe_runs = []
        for rep in range(self.args.repeats):
            for arm in (('A', 'H') if rep % 2 == 0 else ('H', 'A')):
                record, _ = self.measure(name, data, probe_trace, probe_events, probe_tape, arm,
                                         prior['chosen'][arm]['ef'], 'maintenance-probe', rep)
                probe_runs.append(record)
        result = dict(complete=True, stage='dynamic', smoke_only=self.args.smoke,
                      event_counts=dict(Counter(e[0] for e in events)),
                      runs=results, tape_sha256=sha256_file(tape),
                      maintenance_probe=dict(metadata=probe_meta, runs=probe_runs,
                                             tape_sha256=sha256_file(probe_tape)),
                      caveat='Serialized kernel timings include synchronous rebuilds, not concurrent throughput; canonical validation and output IO are excluded.')
        write_json(self.out / name / 'result.json', result)
        return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dataset', action='append', required=True, help='NAME=existing data directory')
    p.add_argument('--trace', action='append', required=True, help='NAME=existing dynamic trace')
    p.add_argument('--out', type=Path, required=True, help='new output directory; never overwritten')
    p.add_argument('--stage', choices=('static', 'dynamic'), default='static')
    p.add_argument('--static-results', type=Path, help='completed static output root, required for dynamic')
    p.add_argument('--binary', type=Path, default=ROOT / 'build/closure_research')
    p.add_argument('--baseline', type=Path, default=ROOT / 'build/dynamic_triad')
    p.add_argument('--max-queries', type=int, default=200)
    p.add_argument('--efs', type=int, nargs='+', default=[64, 256, 1024])
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--k', type=int, default=10)
    p.add_argument('--cpu', type=int, help='pin driver and all serial children to one available CPU')
    p.add_argument('--smoke', action='store_true', help='exercise pipeline, never admit research performance gate')
    args = p.parse_args()
    args.binary = args.binary.resolve(strict=True)
    args.baseline = args.baseline.resolve(strict=True)
    if args.max_queries < 2 or args.repeats < 1 or args.k < 1 or any(ef < args.k for ef in args.efs) or len(set(args.efs)) != len(args.efs):
        p.error('invalid query/repeat/k/ef configuration')
    if args.stage == 'dynamic' and not args.static_results:
        p.error('--static-results is required for dynamic')
    datasets, traces = pairs(args.dataset), pairs(args.trace)
    if datasets.keys() != traces.keys():
        p.error('dataset and trace names must match exactly')
    experiment = Experiment(args)
    try:
        results = {}
        for name, data in datasets.items():
            if args.stage == 'static':
                results[name] = experiment.static(name, data, traces[name])
            else:
                prior = json.loads((args.static_results / name / 'result.json').read_text())
                results[name] = experiment.dynamic(name, data, traces[name], prior)
        experiment.manifest['complete'] = True
        experiment.manifest['results'] = results
        write_json(experiment.out / 'manifest.json', experiment.manifest)
        print(json.dumps(dict(complete=True, stage=args.stage, output=str(experiment.out),
                             dynamic_admitted={n: r.get('dynamic_admitted') for n, r in results.items()})))
    except Exception as error:
        experiment.manifest['error'] = str(error)
        write_json(experiment.out / 'manifest.json', experiment.manifest)
        raise


if __name__ == '__main__':
    main()
