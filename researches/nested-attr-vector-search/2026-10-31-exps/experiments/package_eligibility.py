#!/usr/bin/env python3
"""Read-only packaging and semantic audit of the eligibility runs; never searches or selects.

Verifies documented invariants directly from the per-query CSVs, then writes compact
aggregates (static, bands, NodeANN audit, maintenance) plus the raw file inventory.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics

BANDS = ('empty', 'E_lt_ef', 'E_le_512', 'E_gt_512')
ARMS = ('A-graph', 'U-off', 'U-cap', 'U-aware', 'H-off', 'H-cap', 'H-aware', 'Exact', 'A-hybrid-reference')


def load(path):
    return json.loads(Path(path).read_text())


def dump(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def rows(path):
    with Path(path).open(newline='') as handle:
        return list(csv.DictReader(handle))


def field_stat(group, field, stat):
    block = group.get(field) or group.get('extra', {}).get(field) or {}
    return block.get(stat)


def fmean(values):
    values = [v for v in values if v is not None]
    return statistics.fmean(values) if values else None


def pct(ordered, p):
    return ordered[max(0, -(-int(p * len(ordered)) // 1) - 1)] if ordered else None


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def summarize(selected):
    """selected: list of per-query dicts (already filtered)."""
    out = {'queries': len(selected)}
    if not selected:
        return out
    for key in ('e2e_us', 'distance_calls', 'expanded', 'compile_us', 'eligibility_us', 'eligible_scan_us',
                'directory_visits', 'graph_calls', 'scalar_scan_group_calls', 'scanned_live_objects',
                'graph_distance_calls', 'scan_distance_calls', 'merged_candidates', 'qualification_directory_visits',
                'qualification_posting_words', 'qualification_attribute_words', 'effective_ef'):
        values = [float(r[key]) for r in selected if key in r]
        if values:
            ordered = sorted(values)
            out[key] = dict(mean=statistics.fmean(values), p50=pct(ordered, .5), p95=pct(ordered, .95),
                            p99=pct(ordered, .99), maximum=ordered[-1])
    nonempty = [r for r in selected if int(r['gt_count'])]
    if nonempty:
        out['quality'] = dict(nonempty=len(nonempty), unique_qidx=len({r['qidx'] for r in nonempty}),
                              macro=statistics.fmean(float(r['recall']) for r in nonempty),
                              pair=sum(int(r['hit_count']) for r in nonempty) / sum(int(r['gt_count']) for r in nonempty))
    return out


def semantic_check(raw, records, summary):
    """Independent re-derivation of the documented execution invariants."""
    problems, checks = [], {'rows': 0, 'exact_rows': 0, 'hybrid_exact_rows': 0, 'empty_skipped': 0,
                            'cap_exact_path_rows': 0, 'a_graph_rows': 0, 'distance_split_rows': 0}
    for record in records:
        prefix = Path(record['prefix'])
        for name, expected in record['raw_sha256'].items():
            if sha(prefix.parent / name) != expected:
                problems.append(f'artifact changed: {prefix.parent / name}')
        for r in rows(str(prefix) + '.queries.csv'):
            checks['rows'] += 1
            ef = int(record['ef'])
            policy = r['eligibility_policy']
            e = int(r['eligible_card'])
            gt = [x for x in r['gt_ids'].split(';') if x]
            got = [x for x in r['result_ids'].split(';') if x]
            for key in ('invalid_count', 'duplicate_count', 'state_errors', 'gt_distance_calls',
                        'eligibility_mask_mismatches', 'qualification_mask_mismatches'):
                if int(r.get(key, 0) or 0):
                    problems.append(f'{record["variant"]}: nonzero {key}')
            if r.get('gt_mode', 'precomputed') not in ('precomputed',):
                problems.append(f'{record["variant"]}: gt_mode {r.get("gt_mode")}')
            if 'graph_distance_calls' in r and 'scan_distance_calls' in r and \
                    int(r['graph_distance_calls']) + int(r['scan_distance_calls']) != int(r['distance_calls']):
                checks['distance_split_rows'] += 1
                problems.append(f'{record["variant"]}: distance split != total')
            exact = policy == 'exact' or (policy == 'hybrid' and e <= max(10, ef, 512))
            if exact and e:
                checks['exact_rows' if policy == 'exact' else 'hybrid_exact_rows'] += 1
                if r['result_ids'] != r['gt_ids']:
                    problems.append(f'{record["variant"]}: exact result ids differ from GT')
                if int(r['distance_calls']) != e:
                    problems.append(f'{record["variant"]}: exact distance calls {r["distance_calls"]} != E {e}')
            if e == 0:
                checks['empty_skipped'] += 1
                if int(r['distance_calls']) or int(r.get('graph_calls', 0)) or int(r.get('expanded', 0)):
                    problems.append(f'{record["variant"]}: E=0 still searched')
            if policy == 'cap' and r.get('execution_path') == 'exact':
                checks['cap_exact_path_rows'] += 1
                problems.append('cap silently used the exact eligible scan')
            if record['variant'] == 'A-graph':
                checks['a_graph_rows'] += 1
                if (int(r.get('scalar_scan_group_calls', 0)) or int(r.get('scanned_live_objects', 0)) or
                        int(r.get('scan_distance_calls', 0)) or float(r.get('eligibility_us', 0)) or policy != 'off'):
                    problems.append('A-graph used a scan or eligibility planner')
                if int(r['graph_calls']) != int(r.get('fanout', r['graph_calls'])):
                    problems.append('A-graph graph_calls != fanout')
                if int(r['scope_card']) > 0 and int(r['graph_calls']) == 0:
                    problems.append('A-graph did not search a nonempty scope')
    summary['semantic'] = checks
    return problems


def variant_table(records, dataset, trace):
    out = []
    for label in ARMS:
        for ef in (15, 20):
            selected = [r for r in records if r['dataset'] == dataset and r['trace'] == trace
                        and r['variant'] == label and r['ef'] == ef]
            if not selected:
                continue
            assert len(selected) == 3, (label, ef, len(selected))
            base = selected[0]['groups']['all_nonempty']
            row = dict(dataset=dataset, variant=label, ef=ef, repeats=len(selected),
                       nonempty=[r['groups']['all_nonempty']['quality']['nonempty'] for r in selected],
                       unique_qidx=[r['groups']['all_nonempty']['quality']['unique_qidx'] for r in selected],
                       macro=[r['groups']['all_nonempty']['quality']['macro'] for r in selected],
                       pair=[r['groups']['all_nonempty']['quality']['pair'] for r in selected])
            for field in ('e2e_us', 'distance_calls', 'expanded', 'compile_us', 'eligibility_us',
                          'eligible_scan_us', 'graph_calls', 'scanned_live_objects', 'graph_distance_calls',
                          'scan_distance_calls', 'merged_candidates', 'effective_ef'):
                for stat in ('mean', 'p50', 'p95', 'p99'):
                    row[f'{field}_{stat}'] = fmean([field_stat(r['groups']['all_nonempty'], field, stat)
                                                    for r in selected])
            out.append(row)
    return out


def band_table(records, dataset, trace):
    out = []
    for label in ARMS:
        for ef in (15, 20):
            for band in BANDS:
                selected = [r for r in records if r['dataset'] == dataset and r['trace'] == trace
                            and r['variant'] == label and r['ef'] == ef]
                if not selected:
                    continue
                values = [r['groups'][band] for r in selected]
                row = dict(dataset=dataset, variant=label, ef=ef, band=band,
                           queries=[v['e2e_us']['count'] for v in values],
                           macro=[v.get('quality', {}).get('macro') for v in values],
                           pair=[v.get('quality', {}).get('pair') for v in values])
                for field in ('e2e_us', 'expanded', 'distance_calls', 'compile_us', 'eligibility_us',
                              'eligible_scan_us'):
                    row[f'{field}_mean'] = fmean([field_stat(v, field, 'mean') for v in values])
                out.append(row)
    return out


def split_groups(records, dataset, trace):
    """direct/recursive split recomputed from raw CSVs; no program change."""
    out = []
    for label in ARMS:
        for ef in (15, 20):
            picked = [r for r in records if r['dataset'] == dataset and r['trace'] == trace
                      and r['variant'] == label and r['ef'] == ef]
            if not picked:
                continue
            per_query = []
            for record in picked:
                per_query += rows(str(Path(record['prefix'])) + '.queries.csv')
            for name, keep in (('direct', '0'), ('recursive', '1')):
                stats = summarize([r for r in per_query if r['recursive'] == keep])
                if not stats.get('queries'):
                    continue
                out.append(dict(dataset=dataset, variant=label, ef=ef, band=name, **{
                    f'{k}_mean': v['mean'] for k, v in stats.items() if isinstance(v, dict) and k != 'quality'},
                    queries=stats['queries'],
                    macro=stats.get('quality', {}).get('macro'), pair=stats.get('quality', {}).get('pair'),
                    nonempty=stats.get('quality', {}).get('nonempty'),
                    unique_qidx=stats.get('quality', {}).get('unique_qidx')))
    return out


def nodeann_audit(records, raw, dataset, trace, summary_by_run):
    out = {}
    for label in ('A-graph', 'A-hybrid-reference'):
        picked = [r for r in records if r['dataset'] == dataset and r['trace'] == trace
                  and r['variant'] == label and r['ef'] == 20]
        if not picked:
            continue
        summary = picked[0]['summary']
        entry = {'runs': len(picked), 'ef': 20,
                 'containers': summary.get('final_local_hnsw_index_count'),
                 'initial_indexed_vectors': summary.get('initial_indexed_vectors'),
                 'final_indexed_vectors': summary.get('final_indexed_vectors'),
                 'live_objects': summary.get('n_live') or summary.get('live_objects'),
                 'total_vectors': summary.get('n_vectors'),
                 'index_bytes': summary.get('index_bytes'),
                 'index_bytes_kind': summary.get('index_bytes_kind'),
                 'peak_rss_bytes': summary.get('peak_rss_bytes'),
                 'build_s': summary.get('build_s')}
        per_query, groups = [], {}
        for record in picked:
            for r in rows(str(Path(record['prefix'])) + '.queries.csv'):
                per_query.append(r)
        entry['per_query'] = summarize(per_query)
        for scope_lo, scope_hi, name in ((0, 0, 'scope_0'), (1, 63, 'scope_1_63'), (64, 511, 'scope_64_511'),
                                         (512, 11869, 'scope_512_plus')):
            sel = [r for r in per_query if scope_lo <= int(r['scope_card']) <= scope_hi]
            if sel:
                groups[name] = summarize(sel)
        for rec in ('0', '1'):
            sel = [r for r in per_query if r['recursive'] == rec]
            if sel:
                groups['recursive' if rec == '1' else 'direct'] = summarize(sel)
        entry['groups'] = groups
        out[label] = entry
    return out


def main(raw, out):
    raw = Path(raw).resolve()
    manifest = load(raw / 'manifest.json')
    assert manifest.get('complete') is True, 'incomplete workflow'
    records = load(raw / 'runs.json')
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    summary = {}
    problems = semantic_check(raw, records, summary)
    datasets = sorted({r['dataset'] for r in records})
    traces = sorted({r['trace'] for r in records})
    table, bands, audit = [], [], {}
    for dataset in datasets:
        for trace in traces:
            table += variant_table(records, dataset, trace)
            bands += band_table(records, dataset, trace)
            if trace == 'static':
                audit[dataset] = nodeann_audit(records, raw, dataset, trace, {})
                bands += split_groups(records, dataset, trace)
    with (out / 'static-summary.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=sorted({k for r in table for k in r}))
        writer.writeheader()
        writer.writerows([{k: (v if not isinstance(v, list) else ';'.join(map(str, v))) for k, v in r.items()} for r in table])
    with (out / 'bands.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=sorted({k for r in bands for k in r}))
        writer.writeheader()
        writer.writerows([{k: (v if not isinstance(v, list) else ';'.join(map(str, v))) for k, v in r.items()} for r in bands])
    dump(out / 'static-summary.json', table)
    dump(out / 'bands.json', bands)
    dump(out / 'nodeann-audit.json', audit)
    maintenance = [r for r in records if r['trace'] != 'static']
    if maintenance:
        ops = []
        for record in maintenance:
            for op, value in record['updates'].items():
                ops.append(dict(dataset=record['dataset'], trace=record['trace'], variant=record['variant'],
                                ef=record['ef'], repeat=record['repeat'], op=op, **value))
        dump(out / 'maintenance-ops.json', ops)
    inventory = [dict(path=str(p), bytes=p.stat().st_size, sha256=sha(p))
                 for p in sorted(raw.rglob('*')) if p.is_file()]
    dump(out / 'raw-inventory.json', inventory)
    dump(out / 'semantic-check.json', dict(problems=problems, checks=summary['semantic'],
                                           measured_runs=len(records), manifest_complete=manifest['complete'],
                                           baseline_definition=manifest.get('baseline_definition'),
                                           note='Invariants re-derived from per-query CSVs; no algorithm change.'))
    print(json.dumps(dict(packaged=str(out), problems=len(problems), runs=len(records), checks=summary['semantic'])))
    if problems:
        raise SystemExit(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    main(args.raw, args.out)
