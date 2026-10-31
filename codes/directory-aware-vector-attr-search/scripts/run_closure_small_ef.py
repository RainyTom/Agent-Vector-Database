#!/usr/bin/env python3
"""Fixed low-budget A/U/R/H replay: k=10, query ef=15/20, no selection.

Uses the exact original data and first 200 static Qs from a completed run_closure
manifest. Rebuilds the frozen C++ programs, precomputes GT, rotates serial runs
three times, and reports all quality/cost points. Never admits dynamic.
--smoke is only for synthetic pipeline checks and skips redundant C++ builds.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess

from analyze_closure_termination import summarize
from generate_dynamic_triad import sha256_file, write_json
from run_closure import (ROOT, SINGLE_THREAD_ENV, file_hashes, metrics, put_trace,
                         read_trace, verify_run)

PIN = '7654f1e'
REPOSITORY = ROOT.parents[1]
ARMS = ('A', 'U', 'R', 'H')
EFS = (15, 20)
REPEATS = 3
FROZEN_FILES = (
    'src/closure_index.h', 'src/closure_order.h', 'src/closure_research.cpp',
    'src/neighborhood_filter.h', 'src/raw_l2_space.h', 'src/dynamic_triad.cpp',
    'scripts/run_closure.py', 'scripts/run_dynamic_triad.py', 'scripts/generate_dynamic_triad.py',
    'build_closure.sh', 'build_dynamic_triad.sh')


def blob(content):
    return hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest()


def checked_sources():
    result = {}
    for relative in FROZEN_FILES:
        path = ROOT / relative
        repo_path = path.relative_to(REPOSITORY).as_posix()
        expected = subprocess.check_output(['git', 'rev-parse', PIN + ':' + repo_path],
                                           cwd=REPOSITORY, text=True).strip()
        actual = blob(path.read_bytes())
        if actual != expected:
            raise ValueError('frozen source differs from ' + PIN + ': ' + repo_path)
        result[repo_path] = dict(git_blob=actual, sha256=sha256_file(path))
    for path in (Path(__file__).resolve(), ROOT / 'scripts/analyze_closure_termination.py'):
        result[path.relative_to(REPOSITORY).as_posix()] = dict(sha256=sha256_file(path))
    return result


def trace_groups(previous, name, events):
    phase_ids, original = {}, []
    for phase in ('calibration', 'confirmation'):
        part = read_trace(previous / name / (phase + '.trace'))
        if any(e[0] != 'Q' for e in part):
            raise ValueError('previous static traces must contain Q only')
        phase_ids[phase] = {int(e[1]) for e in part}
        original.extend(part)
    if phase_ids['calibration'] & phase_ids['confirmation']:
        raise ValueError('previous qidx calibration/confirmation overlap')
    if Counter(map(tuple, original)) != Counter(map(tuple, events)):
        raise ValueError('first 200 Qs differ from previous static trace multiset')
    return phase_ids


def group_rows(query_rows, events, phase_ids, live):
    output = {'all_nonempty': [], 'historical_calibration_nonempty': [],
              'historical_confirmation_nonempty': [], 'wide_scope_multi_and': []}
    for row in query_rows:
        if not int(row['gt_count']):
            continue
        output['all_nonempty'].append(row)
        for phase, qidx in phase_ids.items():
            if int(row['qidx']) in qidx:
                output['historical_' + phase + '_nonempty'].append(row)
        event = events[int(row['event']) - 1]
        enabled = int(event[4] != '-1') + int(event[5] != '-1' or event[6] != '-1') + int(event[7] != '0')
        if int(row['scope_card']) >= .25 * live and enabled >= 2 and event[9] == '0':
            output['wide_scope_multi_and'].append(row)
    return output


def pair_rows(left, right):
    if len(left) != len(right):
        raise ValueError('paired query counts differ')
    for lrow, rrow in zip(left, right):
        for key in ('event', 'qidx', 'scope_card', 'eligible_card', 'gt_ids'):
            if lrow[key] != rrow[key]:
                raise ValueError('paired query state mismatch in ' + key)


def run(args):
    previous = args.previous_raw.resolve(strict=True)
    old = json.loads((previous / 'manifest.json').read_text())
    if old.get('complete') is not True or old.get('stage') != 'static':
        raise ValueError('requires completed original static manifest')
    if bool(old.get('smoke_only')) != args.smoke:
        raise ValueError('smoke/real manifest status mismatch')
    if old.get('k', 10) != 10:
        raise ValueError('this frozen rerun uses k=10')
    names = args.dataset or sorted(old['input'])
    if len(set(names)) != len(names) or any(name not in old['input'] for name in names):
        raise ValueError('unknown/duplicate dataset name')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    manifest = dict(complete=False, kind='fixed_small_query_budget_not_calibration_selection',
                    source_pin=PIN, smoke_only=args.smoke, k=10, efs=list(EFS), arms=list(ARMS),
                    repeats=REPEATS, max_queries=200, efConstruction=200, M=16,
                    candidate_pool_build_ef=200, pool_size=64, scan_threshold=512,
                    previous_manifest=str(previous / 'manifest.json'),
                    previous_manifest_sha256=sha256_file(previous / 'manifest.json'),
                    dynamic_admitted=False, commands=[], input={},
                    interpretation='All points retained. Time ratios are same-budget diagnostics, not automatic same-quality speedups.')
    write_json(out / 'manifest.json', manifest)
    env = dict(os.environ, **SINGLE_THREAD_ENV)
    records, comparisons, cache = [], [], {}

    def command(cmd, prefix):
        prefix.parent.mkdir(parents=True, exist_ok=True)
        manifest['commands'].append(list(map(str, cmd)))
        write_json(out / 'manifest.json', manifest)
        with Path(str(prefix) + '.log').open('w') as log:
            subprocess.run(list(map(str, cmd)), cwd=REPOSITORY, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)

    try:
        manifest['sources'] = checked_sources()
        if args.cpu is not None:
            os.sched_setaffinity(0, {args.cpu})
        manifest['cpu_affinity'] = sorted(os.sched_getaffinity(0))
        if not args.smoke:
            command(['bash', ROOT / 'build_closure.sh'], out / 'build-closure')
            command(['bash', ROOT / 'build_dynamic_triad.sh'], out / 'build-nodeann')
        candidate, baseline = ROOT / 'build/closure_research', ROOT / 'build/dynamic_triad'
        manifest['binaries'] = {str(p): sha256_file(p) for p in (candidate, baseline)}
        for name in names:
            if not all(c.isalnum() or c in '_-' for c in name):
                raise ValueError('unsafe dataset name')
            original = old['input'][name]
            data, source = Path(original['data']), Path(original['source_trace'])
            for filename, expected in original['files'].items():
                if sha256_file(data / filename) != expected:
                    raise ValueError('original data SHA differs: ' + name + '/' + filename)
            if sha256_file(source) != original['source_trace_sha256']:
                raise ValueError('original source trace SHA differs: ' + name)
            events = [e for e in read_trace(source) if e[0] == 'Q'][:200]
            if not events:
                raise ValueError('no query in source trace')
            if not args.smoke and len(events) != 200:
                raise ValueError('real rerun requires the original 200 Qs')
            phase_ids = trace_groups(previous, name, events)
            live = sum(int(line.split()[3]) for line in (data / 'attrs.txt').read_text().splitlines())
            trace = out / name / 'static.trace'
            put_trace(trace, events)
            manifest['input'][name] = dict(original, static_trace=str(trace),
                                           static_trace_sha256=sha256_file(trace),
                                           qidx_groups={phase: sorted(values) for phase, values in phase_ids.items()})
            oracle = out / name / 'oracle'
            command([candidate, '--data', data, '--trace', trace, '--out', oracle, '--oracle-only', '--k', 10], oracle)
            oracle_summary = json.loads(Path(str(oracle) + '.summary.json').read_text())
            if oracle_summary.get('complete') is not True:
                raise ValueError('canonical oracle incomplete')
            tape = Path(str(oracle) + '.queries.csv')
            manifest['input'][name]['gt_tape_sha256'] = sha256_file(tape)
            grid = [(arm, ef) for ef in EFS for arm in ARMS]
            for repeat in range(REPEATS):
                ordered = grid[repeat:] + grid[:repeat]
                for arm, ef in ordered:
                    prefix = out / name / 'fixed' / f'{arm}-ef{ef}-rep{repeat}'
                    cmd = [baseline if arm == 'A' else candidate, '--data', data, '--trace', trace,
                           '--out', prefix, '--arm', arm, '--ef', ef, '--k', 10, '--gt-in', tape]
                    if arm == 'A':
                        cmd += ['--scan-threshold', 512]
                    command(cmd, prefix)
                    qr, _, summary = verify_run(prefix, tape, events)
                    groups = group_rows(qr, events, phase_ids, live)
                    record = dict(dataset=name, arm=arm, ef=ef, repeat=repeat, prefix=str(prefix),
                                  all_queries=metrics(qr), groups={g: metrics(r) for g, r in groups.items()},
                                  summary=summary, raw_sha256=file_hashes(prefix))
                    if arm != 'A':
                        record['termination_attribution'] = summarize(qr, ef)
                        if record['termination_attribution']['forced_property_violations']:
                            raise ValueError('source-derived E<ef property contradicted; retain raw and inspect assumptions')
                    records.append(record)
                    cache[(name, repeat, ef, arm)] = qr
                    write_json(out / 'runs.json', records)
            pools = {r['summary']['pool_fingerprint'] for r in records if r['dataset'] == name and r['arm'] != 'A'}
            if len(pools) != 1:
                raise ValueError('candidate pool fingerprints differ across fixed-budget ablations')
            for repeat in range(REPEATS):
                for ef in EFS:
                    h, a, r = [cache[(name, repeat, ef, arm)] for arm in ('H', 'A', 'R')]
                    pair_rows(h, a); pair_rows(h, r)
                    hg, ag = group_rows(h, events, phase_ids, live), group_rows(a, events, phase_ids, live)
                    for group in hg:
                        hm, am = metrics(hg[group]), metrics(ag[group])
                        ha, aa = hm['e2e_us']['mean'], am['e2e_us']['mean']
                        comparisons.append(dict(dataset=name, repeat=repeat, ef=ef, group=group,
                            H=hm, A=am, same_budget_H_over_A=ha / aa if ha is not None and aa else None,
                            quality_is_reported_not_used_for_selection=True))
                    # Same ef, same nonempty source queries; equal counts do not prove equal sets.
                    selected = [(x, y) for x, y in zip(h, r) if int(x['gt_count'])]
                    comparisons.append(dict(dataset=name, repeat=repeat, ef=ef, group='matched_H_R_nonempty',
                        queries=len(selected), equal_expanded_count=sum(int(x['expanded']) == int(y['expanded']) for x, y in selected),
                        H_fewer_expanded_count=sum(int(x['expanded']) < int(y['expanded']) for x, y in selected)))
            write_json(out / 'comparisons.json', comparisons)
        # A compact table keeps every arm/ef/repeat, never discards low-recall points.
        columns = ['dataset', 'arm', 'ef', 'repeat', 'group', 'nonempty', 'unique_qidx',
                   'macro', 'pair', 'mean_us', 'p50_us', 'p95_us', 'p99_us', 'distances_mean',
                   'expanded_mean', 'compile_mean_us']
        with (out / 'points.csv').open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=columns); writer.writeheader()
            for record in records:
                for group, values in record['groups'].items():
                    row = {key: record[key] for key in ('dataset', 'arm', 'ef', 'repeat')}
                    q = values['quality']
                    row.update(group=group, nonempty=q['nonempty'], unique_qidx=q['unique_qidx'],
                               macro=q['macro'], pair=q['pair'], mean_us=values['e2e_us']['mean'],
                               p50_us=values['e2e_us']['p50'], p95_us=values['e2e_us']['p95'],
                               p99_us=values['e2e_us']['p99'], distances_mean=values['distance_calls']['mean'],
                               expanded_mean=values['expanded']['mean'], compile_mean_us=values['compile_us']['mean'])
                    writer.writerow(row)
        manifest['complete'] = True
        manifest['measured_runs'] = len(records)
        manifest['selection_performed'] = False
        write_json(out / 'manifest.json', manifest)
        print(json.dumps(dict(complete=True, out=str(out), runs=len(records), efs=list(EFS), dynamic_admitted=False)))
    except Exception as error:
        manifest['error'] = str(error)
        write_json(out / 'manifest.json', manifest)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous-raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True, help='new output root')
    parser.add_argument('--dataset', action='append', help='reuse named original datasets; default all')
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--smoke', action='store_true', help='synthetic pipeline verification only, no scientific gate')
    run(parser.parse_args())


if __name__ == '__main__':
    main()
