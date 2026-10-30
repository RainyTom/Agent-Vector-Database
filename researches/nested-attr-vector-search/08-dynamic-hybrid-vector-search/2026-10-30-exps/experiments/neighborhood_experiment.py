#!/usr/bin/env python3
"""Frozen E1/E2 orchestration; all online search and exact GT remain in C++.

Run from any directory (relative CLI paths resolve against the repository):
  python PATH/neighborhood_experiment.py prepare --out OUT --binary BIN --baseline BASE
  python PATH/neighborhood_experiment.py run --out OUT
  python PATH/neighborhood_experiment.py analyze --out OUT
  python PATH/neighborhood_experiment.py run-e1 --out OUT

OUT must not exist at prepare time. Each execution/analysis phase creates a fresh
subdirectory and refuses overwrites. No update events are replayed. No results
exist until the supplied real binaries have completed. A is dynamic_triad arm A,
not its unrelated arm C. Budgets are frozen before confirmation, never retuned.
"""

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[5]
PREVIOUS = ROOT / 'researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/work/dynamic_triad'
WIKI = PREVIOUS / 'wiki-topology/wiki-topology-s20261029-q456-f7046c642b34/root-3626'
SIFT = PREVIOUS / 'crossover'
DATASETS = {
    'wiki': (WIKI / 'native', WIKI / 'traces/qu1.txt'),
    'sift': (SIFT / 'datasets/sift-n12000-r1000-q64-d3-s20261029',
             SIFT / 'traces/n12000-s20261029-1d584b6d2829/sift-d3-uniform-mixed-r0p1-qu1.txt'),
}
HNSW = ROOT / 'codes/directory-aware-vector-attr-search/deps/hnswlib'
HNSW_REVISION = 'ca426729609b3221563047ceb269cc1213e0d376'
FIELDS = ('qidx', 'recursive', 'anchor', 'category', 'lo', 'hi', 'tagmode', 'tags', 'combine')
THREAD_ENV = {key: '1' for key in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS', 'NUMEXPR_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS')}
COUNTERS = ('distance_calls', 'ann_hops', 'local_indexes', 'fanout', 'scan_nodes',
            'onehop_ids', 'twohop_ids', 'page_containers', 'sparse_offsets',
            'dense_words', 'duplicate_checks', 'eligible_expansions', 'pages_prepared',
            'qualification_us', 'neighborhood_us', 'search_us', 'dir_us',
            'center_count', 'acquire_ns', 'qual_us')


def resolve(value):
    path = Path(value).expanduser()
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def dump(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n')


def load(path):
    return json.loads(Path(path).read_text())


def write_csv(path, rows, fields=None):
    if fields is None:
        fields = sorted({key for row in rows for key in row})
    with Path(path).open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: json.dumps(value, sort_keys=True) if isinstance(value, (dict, list)) else value
                             for key, value in row.items()})


def git_head(path):
    return subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()


def file_record(path):
    return {'path': str(path), 'bytes': Path(path).stat().st_size, 'sha256': sha256(path)}


def native_snapshot(data):
    bindings = [int(value) for value in (data / 'bindings.txt').read_text().split()]
    attrs = [tuple(map(int, line.split())) for line in (data / 'attrs.txt').read_text().splitlines()]
    n, dim, nq = map(int, (data / 'meta.txt').read_text().split())
    if len(bindings) != n or len(attrs) != n or any(len(row) != 4 or row[3] not in (0, 1) for row in attrs):
        raise ValueError(f'{data}: malformed bindings/attrs')
    if (data / 'base.f32').stat().st_size != n * dim * 4 or (data / 'test.f32').stat().st_size != nq * dim * 4:
        raise ValueError(f'{data}: vector shape mismatch')
    lines = (data / 'tree.txt').read_text().splitlines()
    count = int(lines[0])
    parent = {}
    for line in lines[1:]:
        node, ancestor = map(int, line.split())
        if node in parent or not 0 <= node < count or not -1 <= ancestor < count:
            raise ValueError('invalid native tree')
        parent[node] = ancestor
    if len(parent) != count or sum(p == -1 for p in parent.values()) != 1:
        raise ValueError('missing tree nodes or root')
    ancestors = {}
    for node in parent:
        chain, cursor = set(), node
        while cursor != -1:
            if cursor in chain or cursor not in parent:
                raise ValueError('cyclic/disconnected native tree')
            chain.add(cursor)
            cursor = parent[cursor]
        ancestors[node] = chain
    if any(node not in parent for node in bindings):
        raise ValueError('binding references absent node')
    return bindings, attrs, ancestors, {'n': n, 'dim': dim, 'nq': nq, 'n_live': sum(row[3] for row in attrs), 'n_nodes': count}


def predicate(attr, query):
    category, numeric, tags, _ = attr
    conditions = []
    if query['category'] != -1:
        conditions.append(category == query['category'])
    if query['lo'] != -1 or query['hi'] != -1:
        conditions.append(query['lo'] <= numeric <= query['hi'])
    mode, wanted = query['tagmode'], query['tags']
    if mode:
        conditions.append({1: (tags & wanted) == wanted, 2: bool(tags & wanted),
                           3: tags == wanted, 4: (tags & wanted) == tags}[mode])
    return not conditions or (any(conditions) if query['combine'] else all(conditions))


def select_queries(trace, snapshot):
    bindings, attrs, ancestors, shape = snapshot
    selected, total_queries, events = [], 0, 0
    with trace.open() as handle:
        declared = int(handle.readline())
        for line_number, raw in enumerate(handle, 2):
            parts = raw.split()
            if not parts:
                raise ValueError(f'blank source event at {trace}:{line_number}')
            events += 1
            if parts[0] != 'Q':
                continue  # Deliberately never apply source update events.
            total_queries += 1
            if len(selected) >= 200:
                continue
            if len(parts) != 10:
                raise ValueError(f'bad Q fields at source line {line_number}')
            query = dict(zip(FIELDS, map(int, parts[1:])))
            if not 0 <= query['qidx'] < shape['nq'] or query['anchor'] not in ancestors or query['recursive'] not in (0, 1) or query['combine'] not in (0, 1) or not 0 <= query['tagmode'] <= 4:
                raise ValueError('invalid source query')
            scope = [i for i, node in enumerate(bindings) if attrs[i][3] and
                     (query['anchor'] in ancestors[node] if query['recursive'] else node == query['anchor'])]
            eligible = sum(predicate(attrs[i], query) for i in scope)
            fraction = len(scope) / shape['n_live'] if shape['n_live'] else 0
            enabled = int(query['category'] != -1) + int(query['lo'] != -1 or query['hi'] != -1) + int(query['tagmode'] != 0)
            selected.append(dict(query, selected_row=len(selected), source_line=line_number,
                                 source_event=events, source_query_index=total_queries - 1,
                                 raw=raw.rstrip('\r\n'), initial_scope_count=len(scope),
                                 initial_scope_fraction=fraction, initial_eligible_count=eligible,
                                 initial_eligible_selectivity=eligible / len(scope) if scope else None,
                                 scope_profile='wide' if fraction >= .25 else 'narrow' if fraction <= .05 else 'middle',
                                 enabled_fields=enabled, strong_and=enabled >= 2 and query['combine'] == 0))
    if events != declared:
        raise ValueError('source trace event count disagrees with header')
    ranks = {qidx: rank for rank, qidx in enumerate(sorted({row['qidx'] for row in selected}))}
    for row in selected:
        row['qidx_rank'] = ranks[row['qidx']]
        row['split'] = 'calibration' if row['qidx_rank'] % 2 == 0 else 'confirmation'
    return selected, total_queries


def prepare(args):
    out = resolve(args.out)
    if out.exists():
        raise ValueError(f'prepare requires a fresh output directory: {out}')
    binary, baseline = resolve(args.binary), resolve(args.baseline)
    for path in (binary, baseline):
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ValueError(f'not an executable binary: {path}')
    budgets = args.budgets
    if not 1 <= len(budgets) <= 3 or len(set(budgets)) != len(budgets) or any(b not in (64, 256, 1024) for b in budgets):
        raise ValueError('use one to three distinct budgets from 64,256,1024')
    revision = git_head(HNSW)
    if revision != HNSW_REVISION:
        raise ValueError(f'hnsw HEAD {revision} differs from required {HNSW_REVISION}')
    manifest = {'version': 1, 'source_sha': git_head(ROOT), 'hnsw_head': revision,
                'hnsw_path': str(HNSW), 'binary': file_record(binary), 'baseline': file_record(baseline),
                'budgets': budgets, 'datasets': {}, 'protocol': {
                    'query_selection': 'first 200 existing Q rows in temporal order, no regeneration',
                    'snapshot': 'initial attrs, bindings and tree; updates discarded; C++ inline initial GT',
                    'split': 'sorted unique qidx rank: even calibration, odd confirmation; repeated qidx grouped',
                    'scope_profiles': {'wide_min_fraction': .25, 'narrow_max_fraction': .05},
                    'strong_and': 'enabled(category,numeric,tags)>=2 and combine==0',
                    'quality': 'nonempty macro>=.999 AND pair>=.999, no invalid/duplicate/state errors; empty results correct separately',
                    'execution': 'serial single-worker; budget-major rotating A/B/C; confirmation 3 rotating repeats',
                    'baseline': 'dynamic_triad --arm A --scan-threshold 512 (true direct-node NodeANN)',
                    'selection': 'minimum mean e2e_us passing calibration budget per arm, tie by smaller ef',
                    'no_e3': 'no E3 execution; only assess eligibility after C gains against both at quality',
                }, 'performance_results': 'none yet'}
    # Compute all provenance before creating OUT, so a missing input cannot leave a plausible prepared run.
    prepared = {}
    for name, (data, trace) in DATASETS.items():
        snapshot = native_snapshot(data)
        rows, available = select_queries(trace, snapshot)
        metadata = load(data / 'meta.json')
        files = [file_record(path) for path in sorted(data.rglob('*')) if path.is_file()]
        actual = {Path(item['path']).name: item['sha256'] for item in files}
        for filename, expected in metadata.get('file_sha256', {}).items():
            if actual.get(filename) != expected:
                raise ValueError(f'{name}: supplied native file disagrees with SOURCE SHA: {filename}')
        counts = {}
        shortages = []
        for split in ('all', 'calibration', 'confirmation'):
            subset = [row for row in rows if split == 'all' or row['split'] == split]
            counts[split] = {'rows': len(subset), 'unique_qidx': len({row['qidx'] for row in subset})}
            for scope in ('wide', 'middle', 'narrow'):
                for strength in ('strong_and', 'other'):
                    key = f'{scope}/{strength}'
                    value = sum(row['scope_profile'] == scope and row['strong_and'] == (strength == 'strong_and') for row in subset)
                    counts[split][key] = value
                    if not value:
                        shortages.append(f'{split}: no {key} rows in literal selection')
            if not subset:
                shortages.append(f'{split}: empty split; cannot quality-gate')
        if len(rows) < 200:
            shortages.append(f'only {len(rows)} source Q rows available (requested 200)')
        prepared[name] = rows
        manifest['datasets'][name] = {'data': str(data), 'source_trace': file_record(trace),
                                      'input_files': files, 'native_metadata': metadata, 'shape': snapshot[3],
                                      'source_query_count': available, 'selected_count': len(rows),
                                      'counts': counts, 'shortages': shortages, 'traces': {}}
    out.mkdir(parents=True)
    for name, rows in prepared.items():
        directory = out / name
        directory.mkdir()
        write_csv(directory / 'selection.csv', rows)
        dump(directory / 'selection.json', rows)
        for split in ('all', 'calibration', 'confirmation'):
            subset = [row for row in rows if split == 'all' or row['split'] == split]
            path = directory / f'{split}.qtrace.txt'
            path.write_text(str(len(subset)) + '\n' + ''.join(row['raw'] + '\n' for row in subset))
            manifest['datasets'][name]['traces'][split] = file_record(path)
        manifest['datasets'][name]['selection'] = file_record(directory / 'selection.json')
    dump(out / 'manifest.json', manifest)
    print(f'Prepared {out}; no performance results yet.')


def check_inputs(manifest):
    if git_head(Path(manifest['hnsw_path'])) != manifest['hnsw_head']:
        raise ValueError('hnsw HEAD changed since prepare')
    records = [manifest['binary'], manifest['baseline']]
    for data in manifest['datasets'].values():
        records += data['input_files'] + [data['source_trace'], data['selection']] + list(data['traces'].values())
    for record in records:
        if sha256(record['path']) != record['sha256']:
            raise ValueError(f'input hash changed: {record["path"]}')


def ids(value):
    return [int(part) for part in value.split(';') if part]


def quantile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    position = (len(values) - 1) * fraction
    lo = int(position)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (position - lo)


def metrics(rows):
    nonempty = [row for row in rows if row['gt_count'] > 0]
    empty = [row for row in rows if row['gt_count'] == 0]
    times = [row['e2e_us'] for row in rows]
    total_gt = sum(row['gt_count'] for row in nonempty)
    macro = statistics.mean(row['hit_count'] / row['gt_count'] for row in nonempty) if nonempty else None
    pair = sum(row['hit_count'] for row in nonempty) / total_gt if total_gt else None
    result = {'n_queries': len(rows), 'n_nonempty': len(nonempty), 'n_empty': len(empty),
              'nonempty_macro_recall': macro, 'pair_recall': pair,
              'empty_correct': sum(not row['result_ids'] for row in empty),
              'empty_correctness': all(not row['result_ids'] for row in empty),
              'invalid': sum(row['invalid'] for row in rows), 'duplicate': sum(row['duplicate'] for row in rows),
              'state_errors': sum(row.get('state_errors', 0) for row in rows),
              'mean_e2e_us': statistics.mean(times) if times else None,
              'p50_e2e_us': quantile(times, .5), 'p95_e2e_us': quantile(times, .95), 'p99_e2e_us': quantile(times, .99)}
    nonempty_times = [row['e2e_us'] for row in nonempty]
    result.update(nonempty_mean_e2e_us=statistics.mean(nonempty_times) if nonempty_times else None,
                  nonempty_p50_e2e_us=quantile(nonempty_times, .5),
                  nonempty_p95_e2e_us=quantile(nonempty_times, .95),
                  nonempty_p99_e2e_us=quantile(nonempty_times, .99))
    for field in COUNTERS:
        values = [row[field] for row in rows if field in row]
        result[f'mean_{field}'] = statistics.mean(values) if values else None
    result['quality_pass'] = bool(nonempty and macro >= .999 and pair >= .999 and result['empty_correctness'] and
                                  not any(result[key] for key in ('invalid', 'duplicate', 'state_errors')))
    return result


def inspect_output(prefix, arm, expected, shape):
    errors, normalized = [], []
    summary = load(str(prefix) + '.summary.json')
    if summary.get('complete') is not True:
        errors.append('summary does not declare complete=true')
    if arm == 'A':
        if summary.get('arm') != 'A' or summary.get('scan_threshold') != 512 or summary.get('n_updates') != 0:
            errors.append('baseline is not frozen direct-node arm A / scan512 / no updates')
        for key in ('invalid_results', 'duplicate_results', 'state_violations'):
            if summary.get(key) != 0:
                errors.append(f'baseline summary {key} missing or nonzero')
    else:
        for key in ('build_s', 'graph_build_s', 'neighborhood_build_s', 'neighborhood_bytes', 'graph_bytes',
                    'peak_rss_bytes', 'n', 'n_live', 'dim', 'macro_recall', 'pair_recall', 'graph_sha256'):
            if key not in summary:
                errors.append(f'B/C summary missing {key}')
        for key in ('n', 'n_live', 'dim'):
            if summary.get(key) != shape[key]:
                errors.append(f'B/C summary {key} differs from frozen input')
    with Path(str(prefix) + '.queries.csv').open(newline='') as handle:
        reader = csv.DictReader(handle)
        required = {'query_index', 'qidx', 'gt_count', 'hit_count', 'recall', 'e2e_us', 'gt_ids', 'result_ids'}
        required |= {'invalid_count', 'duplicate_count', 'version', 'scope_card', 'eligible_card'} if arm == 'A' else {'invalid', 'duplicate', 'state_errors'}
        missing = required - set(reader.fieldnames or ())
        if missing:
            raise ValueError('query schema missing: ' + ','.join(sorted(missing)))
        for index, raw in enumerate(reader):
            if index >= len(expected):
                errors.append('unexpected extra query row')
                break
            source = expected[index]
            row = dict(source)
            row['raw_output'] = raw
            for key in ('query_index', 'qidx', 'gt_count', 'hit_count'):
                row[key] = int(raw[key])
            for key in ('e2e_us', 'recall'):
                row[key] = float(raw[key])
                if not math.isfinite(row[key]) or row[key] < 0:
                    errors.append(f'row {index}: invalid {key}')
            row['invalid'] = int(raw['invalid_count' if arm == 'A' else 'invalid'])
            row['duplicate'] = int(raw['duplicate_count' if arm == 'A' else 'duplicate'])
            if arm != 'A':
                row['state_errors'] = int(raw['state_errors'])
            if any(row.get(key, 0) < 0 for key in ('invalid', 'duplicate', 'state_errors', 'gt_count', 'hit_count')):
                errors.append(f'row {index}: negative quality/error count')
            row['gt_ids'], row['result_ids'] = ids(raw['gt_ids']), ids(raw['result_ids'])
            for key in COUNTERS:
                if key in raw:
                    row[key] = float(raw[key])
                    if not math.isfinite(row[key]) or row[key] < 0:
                        errors.append(f'row {index}: invalid counter {key}')
            if row['query_index'] != index or row['qidx'] != source['qidx']:
                errors.append(f'row {index}: query identity mismatch')
            gt, result = row['gt_ids'], row['result_ids']
            if len(gt) != row['gt_count'] or len(set(gt)) != len(gt) or row['gt_count'] != min(10, source['initial_eligible_count']):
                errors.append(f'row {index}: GT count differs from initial attrs')
            if len(set(result)) != len(result) or len(result) > 10 or len(result) > source['initial_eligible_count']:
                errors.append(f'row {index}: duplicate/excess result IDs')
            if any(item < 0 or item >= shape['n'] for item in gt + result):
                errors.append(f'row {index}: out of range ID')
            hit = len(set(gt) & set(result))
            recall = hit / len(gt) if gt else float(not result)
            if hit != row['hit_count'] or abs(recall - row['recall']) > 1e-8:
                errors.append(f'row {index}: recall inconsistent with IDs')
            if arm == 'A' and (int(raw['version']) != 0 or int(raw['scope_card']) != source['initial_scope_count'] or int(raw['eligible_card']) != source['initial_eligible_count']):
                errors.append(f'row {index}: baseline is not initial snapshot')
            normalized.append(row)
    if len(normalized) != len(expected):
        errors.append(f'query row count mismatch: {len(normalized)} != {len(expected)}')
    aggregate = metrics(normalized)
    aggregate['quality_pass'] &= not errors
    return summary, normalized, aggregate, errors


def execute(out, directory, manifest, dataset, arm, ef, phase, repeat, order):
    data = manifest['datasets'][dataset]
    split = 'calibration' if phase == 'calibration' else 'confirmation'
    expected = [row for row in load(data['selection']['path']) if row['split'] == split]
    prefix = directory / f'{dataset}-{phase}-r{repeat}-{arm}-ef{ef}'
    command = [manifest['baseline' if arm == 'A' else 'binary']['path'], '--data', data['data'], '--arm', arm,
               '--ef', str(ef), '--out', str(prefix)]
    if arm == 'A':
        command += ['--trace', data['traces'][split]['path'], '--scan-threshold', '512', '--k', '10']
    else:
        command += ['--queries', data['traces'][split]['path']]
    record = {'dataset': dataset, 'arm': arm, 'ef': ef, 'phase': phase, 'repeat': repeat,
              'serial_order': order, 'prefix': str(prefix), 'command': command, 'single_worker_env': THREAD_ENV,
              'trace_sha256': data['traces'][split]['sha256'], 'errors': []}
    started = time.time()
    with Path(str(prefix) + '.stdout.log').open('w') as stdout, Path(str(prefix) + '.stderr.log').open('w') as stderr:
        completed = subprocess.run(command, cwd=ROOT, env=dict(os.environ, **THREAD_ENV), stdout=stdout, stderr=stderr)
    record.update(returncode=completed.returncode, wall_seconds=time.time() - started)
    if completed.returncode:
        record['errors'].append(f'process exit {completed.returncode}; see stderr log')
    try:
        summary, rows, aggregate, errors = inspect_output(prefix, arm, expected, data['shape'])
        record.update(summary=summary, metrics=aggregate)
        record['errors'] += errors
        dump(str(prefix) + '.normalized.json', rows)
    except (OSError, ValueError, KeyError, TypeError, ZeroDivisionError) as error:
        record['errors'].append(f'output validation: {error}')
        record['metrics'] = {'quality_pass': False}
    record['metrics']['quality_pass'] &= not record['errors']
    record['artifacts'] = [file_record(path) for path in sorted(directory.glob(prefix.name + '.*'))]
    dump(str(prefix) + '.record.json', record)
    return record


def comparison_errors(records):
    errors, gt_reference, graph_reference = [], {}, {}
    for record in records:
        dataset, phase = record['dataset'], record['phase']
        normalized = Path(record['prefix'] + '.normalized.json')
        if normalized.exists():
            rows = load(normalized)
            signature = [(row['selected_row'], sorted(row['gt_ids'])) for row in rows]
            key = (dataset, phase)
            if key in gt_reference and signature != gt_reference[key]:
                errors.append(f'{dataset}/{phase}: GT ID sets differ across configurations; exact tie policy or snapshot mismatch, no comparable gain claim')
            else:
                gt_reference[key] = signature
        if record['arm'] in ('B', 'C'):
            graph = record.get('summary', {}).get('graph_sha256')
            if not isinstance(graph, str) or len(graph) != 64:
                errors.append(f'{dataset}: missing valid B/C graph_sha256')
            elif dataset in graph_reference and graph != graph_reference[dataset]:
                errors.append(f'{dataset}: B/C initial graph hashes differ')
            else:
                graph_reference[dataset] = graph
    return sorted(set(errors))


def run(args):
    out = resolve(args.out)
    manifest = load(out / 'manifest.json')
    check_inputs(manifest)
    directory = out / 'runs'
    directory.mkdir()  # No resume, overwrites, or accidental mixture of experiments.
    records, order = [], 0
    for dataset in manifest['datasets']:
        for budget_index, ef in enumerate(manifest['budgets']):
            arms = 'ABC'[budget_index % 3:] + 'ABC'[:budget_index % 3]
            for arm in arms:
                records.append(execute(out, directory, manifest, dataset, arm, ef, 'calibration', 0, order))
                order += 1
                dump(directory / 'records.json', records)
    calibration_errors = comparison_errors(records)
    frozen = {}
    for dataset in manifest['datasets']:
        frozen[dataset] = {}
        for arm in 'ABC':
            candidates = [record for record in records if record['dataset'] == dataset and record['arm'] == arm and record['metrics']['quality_pass']]
            dataset_errors = [error for error in calibration_errors if error.startswith(dataset + '/') or error.startswith(dataset + ':')]
            winner = min(candidates, key=lambda record: (record['metrics']['mean_e2e_us'], record['ef'])) if candidates and not dataset_errors else None
            frozen[dataset][arm] = {'ef': winner['ef'] if winner else None,
                                    'quality_failure': winner is None,
                                    'reason': 'no passing calibration budget or incompatible graph/GT' if winner is None else None,
                                    'calibration_mean_e2e_us': winner['metrics']['mean_e2e_us'] if winner else None}
    dump(directory / 'frozen_budgets.json', {'selected': frozen, 'calibration_protocol_errors': calibration_errors,
                                           'selection_uses_confirmation': False})
    for dataset in manifest['datasets']:
        for repeat in range(3):
            arms = 'ABC'[repeat:] + 'ABC'[:repeat]
            for arm in arms:
                ef = frozen[dataset][arm]['ef']
                if ef is None:
                    continue  # Failed arms keep every calibration configuration; never retune.
                records.append(execute(out, directory, manifest, dataset, arm, ef, 'confirmation', repeat + 1, order))
                order += 1
                dump(directory / 'records.json', records)
    dump(directory / 'protocol_errors.json', comparison_errors(records))
    print(f'Saved all serial calibration/confirmation evidence to {directory}; analyze separately.')


def run_e1(args):
    out = resolve(args.out)
    manifest = load(out / 'manifest.json')
    check_inputs(manifest)
    directory = out / 'e1'
    directory.mkdir()
    records = []
    for dataset, data in manifest['datasets'].items():
        prefix = directory / dataset
        command = [manifest['binary']['path'], '--data', data['data'], '--queries', data['traces']['all']['path'],
                   '--arm', 'C', '--ef', '64', '--e1-centers', '8', '--out', str(prefix)]
        with Path(str(prefix) + '.stdout.log').open('w') as stdout, Path(str(prefix) + '.stderr.log').open('w') as stderr:
            completed = subprocess.run(command, cwd=ROOT, env=dict(os.environ, **THREAD_ENV), stdout=stdout, stderr=stderr)
        records.append({'dataset': dataset, 'command': command, 'returncode': completed.returncode,
                        'trace_sha256': data['traces']['all']['sha256'], 'e1_centers': 8,
                        'artifacts': [file_record(path) for path in sorted(directory.glob(dataset + '.*'))],
                        'note': 'E1 diagnostics only; this is not an E2 quality/speed result'})
        dump(directory / 'records.json', records)
    print(f'Saved separate E1 full-query evidence to {directory}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest='phase', required=True)
    prep = subparsers.add_parser('prepare', help='freeze literal inputs, selections, source SHA and binary hashes')
    prep.add_argument('--out', required=True)
    prep.add_argument('--binary', required=True, help='real neighborhood_research executable')
    prep.add_argument('--baseline', required=True, help='existing dynamic_triad executable')
    prep.add_argument('--budgets', type=int, nargs='+', default=[64, 256, 1024])
    for phase in ('run', 'run-e1', 'analyze'):
        subparsers.add_parser(phase).add_argument('--out', required=True)
    args = parser.parse_args()
    if args.phase == 'prepare':
        prepare(args)
    elif args.phase == 'run':
        run(args)
    elif args.phase == 'run-e1':
        run_e1(args)
    else:
        from neighborhood_analyze import analyze
        analyze(args)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        sys.exit(f'neighborhood experiment: {error}')
