#!/usr/bin/env python3
"""Read existing static CSVs; never build/search/tune or authorize another stage.

Requires original raw run directory, not the GitHub small aggregate package.
Output separates E<ef forced traversal from other queries. Counts cannot identify
expanded sets/orders or determine exactly which edge first discovers a point.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
import math
from pathlib import Path
import re


def integer(row, key):
    value = int(row[key])
    if value < 0:
        raise ValueError('negative ' + key)
    return value


def real(row, key):
    value = float(row[key])
    if not math.isfinite(value) or value < 0:
        raise ValueError('invalid ' + key)
    return value


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def load_rows(path):
    with path.open(newline='') as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames or len(set(reader.fieldnames)) != len(reader.fieldnames):
            raise ValueError('missing/duplicate query header')
        result = list(reader)
    if any(None in row or any(v is None for v in row.values()) for row in result):
        raise ValueError('malformed query row')
    return result


def identity(row):
    return tuple(integer(row, key) for key in ('event', 'version', 'query_index', 'qidx'))


def summarize(query_rows, ef):
    counts = Counter()
    totals = Counter()
    violations = []
    for row in query_rows:
        scope, eligible, expanded = [integer(row, k) for k in ('scope_card', 'eligible_card', 'expanded')]
        distances, gt_count = integer(row, 'distance_calls'), integer(row, 'gt_count')
        if not eligible <= scope or not expanded <= distances <= scope:
            raise ValueError('scope/eligible/expanded/distance count invariant failed')
        counts['queries'] += 1
        group = 'nonempty' if gt_count else 'empty'
        counts[group] += 1
        if eligible < ef:
            counts['forced_' + group] += 1
            totals['forced_' + group + '_expanded'] += expanded
            if expanded != scope or distances != scope or integer(row, 'hit_count') != gt_count:
                violations.append(dict(identity=list(identity(row)), scope=scope, eligible=eligible,
                                       expanded=expanded, distances=distances, gt_count=gt_count))
        else:
            counts['not_forced_' + group] += 1
        if expanded == scope:
            counts['full_scope_' + group] += 1
        totals[group + '_expanded'] += expanded
        totals[group + '_e2e_us'] += real(row, 'e2e_us')
        totals[group + '_compile_us'] += real(row, 'compile_us')
        for key in ('invalid_count', 'duplicate_count', 'state_errors', 'gt_distance_calls'):
            if integer(row, key):
                raise ValueError('nonzero ' + key + ' in completed measured run')
    shares = {}
    for group in ('nonempty', 'empty'):
        n = counts[group]
        shares['forced_' + group + '_query_fraction'] = counts['forced_' + group] / n if n else None
        expanded = totals[group + '_expanded']
        shares['forced_' + group + '_expanded_fraction'] = totals['forced_' + group + '_expanded'] / expanded if expanded else None
        elapsed = totals[group + '_e2e_us']
        shares[group + '_compile_fraction'] = totals[group + '_compile_us'] / elapsed if elapsed else None
    return dict(counts=dict(counts), totals=dict(totals), shares=shares, forced_property_violations=violations)


def paired(left, right, ef):
    if len(left) != len(right):
        raise ValueError('H/R query counts differ')
    by_group = {}
    for h, r in zip(left, right):
        if identity(h) != identity(r) or any(h[k] != r[k] for k in ('scope_card', 'eligible_card', 'gt_ids')):
            raise ValueError('H/R identity or current GT mismatch')
        if not integer(h, 'gt_count'):
            continue
        group = 'E_lt_ef' if integer(h, 'eligible_card') < ef else 'E_ge_ef'
        value = by_group.setdefault(group, Counter())
        hx, rx = integer(h, 'expanded'), integer(r, 'expanded')
        value['queries'] += 1
        value['H_expanded_total'] += hx
        value['R_expanded_total'] += rx
        value['equal_expanded_count'] += hx == rx
        value['H_fewer_expanded_count'] += hx < rx
        value['H_more_expanded_count'] += hx > rx
    return {group: dict(values) for group, values in by_group.items()}


def analyze(raw):
    manifest = json.loads((raw / 'manifest.json').read_text())
    if manifest.get('complete') is not True or manifest.get('stage') != 'static':
        raise ValueError('requires completed static manifest')
    runs_path = raw / 'runs.json'
    runs = json.loads(runs_path.read_text())
    summaries, cache, inputs = [], {}, []
    for run in runs:
        arm = run['arm']
        if arm == 'A':
            continue
        if arm not in ('U', 'G', 'R', 'H', 'H0') or run['phase'] not in ('calibration', 'confirmation'):
            raise ValueError('unexpected static arm/phase')
        name, phase = run['dataset'], run['phase']
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
            raise ValueError('invalid dataset name')
        ef, repeat = int(run['ef']), int(run['repeat'])
        if ef < 1 or repeat < 0:
            raise ValueError('invalid ef/repeat')
        path = raw / name / phase / f'{arm}-ef{ef}-rep{repeat}.queries.csv'
        digest = sha(path)
        if run['raw_sha256'].get(path.name) != digest:
            raise ValueError('raw query SHA mismatch: ' + str(path))
        qr = load_rows(path)
        if len(qr) != run['summary']['n_queries']:
            raise ValueError('query count does not match original summary')
        result = summarize(qr, ef)
        summaries.append(dict(dataset=name, phase=phase, repeat=repeat, ef=ef, arm=arm, **result))
        key = (name, phase, repeat, ef, arm)
        if key in cache:
            raise ValueError('duplicate original run identity')
        cache[key] = qr
        inputs.append(dict(path=str(path), sha256=digest))
    comparisons = []
    for (name, phase, repeat, ef, arm), qr in cache.items():
        other = (name, phase, repeat, ef, 'R')
        if arm == 'H' and other in cache:
            comparisons.append(dict(dataset=name, phase=phase, repeat=repeat, ef=ef,
                                    groups=paired(qr, cache[other], ef)))
    violations = sum(len(r['forced_property_violations']) for r in summaries)
    return dict(complete=True, all_forced_property_checks_pass=not violations,
                kind='offline_existing_CSV_attribution_not_new_ANN_run', raw=str(raw),
                manifest_sha256=sha(raw / 'manifest.json'), runs_sha256=sha(runs_path),
                measured_query_inputs=inputs, summaries=summaries, matched_H_R=comparisons,
                limitations=['E<ef floor is derived for the frozen legal-results-heap plus connected scope backbone.',
                             'E>=ef does not imply early termination or absence of backbone dependence.',
                             'Same expanded count does not identify the same visited set or expansion sequence.',
                             'CSV does not record first-discovery edge or full frontier history.',
                             'Does not repair core, retune parameters, rerun search or authorize dynamic.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True, help='new JSON file; never overwrite')
    args = parser.parse_args()
    result = analyze(args.raw.resolve(strict=True))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x') as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, allow_nan=False)
        handle.write('\n')
    print(json.dumps(dict(output=str(args.out), files=len(result['summaries']),
                          all_forced_property_checks_pass=result['all_forced_property_checks_pass'])))
    if not result['all_forced_property_checks_pass']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
