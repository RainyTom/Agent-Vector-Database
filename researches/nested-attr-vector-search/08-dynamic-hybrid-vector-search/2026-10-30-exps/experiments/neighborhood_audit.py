#!/usr/bin/env python3
"""Audit saved B/C equality and split empty/nonempty cost; never run searches."""
import argparse
import statistics
from pathlib import Path

from neighborhood_analyze import identity, profiles
from neighborhood_experiment import dump, load, metrics, resolve, write_csv


def audit(out):
    manifest = load(out / 'manifest.json')
    records = load(out / 'runs/records.json')
    report = load(out / 'analysis/report.json')
    destination = out / 'audit'
    destination.mkdir()
    rows_by_run = {record['prefix']: load(record['prefix'] + '.normalized.json') for record in records}
    pairs, mismatches = 0, []
    grouped = {}
    for record in records:
        if record['arm'] in ('B', 'C'):
            key = (record['dataset'], record['phase'], record['repeat'], record['ef'])
            grouped.setdefault(key, {})[record['arm']] = record
    for key, arms in grouped.items():
        if set(arms) != {'B', 'C'}:
            mismatches.append({'run': key, 'error': 'missing B or C'})
            continue
        b, c = (rows_by_run[arms[arm]['prefix']] for arm in ('B', 'C'))
        if len(b) != len(c):
            mismatches.append({'run': key, 'error': 'row count'})
            continue
        for index, (left, right) in enumerate(zip(b, c)):
            pairs += 1
            fields = ['gt_ids', 'result_ids', 'qidx', 'gt_count', 'hit_count']
            different = [field for field in fields if left[field] != right[field]]
            for field in ('distance_calls', 'onehop_ids', 'visited_checks', 'expanded', 'eligible_expansions'):
                if float(left['raw_output'][field]) != float(right['raw_output'][field]):
                    different.append(field)
            if different:
                mismatches.append({'run': key, 'query_index': index, 'different': different})
    split_rows, work_rows, cost_rows = [], [], []
    for record in records:
        if record['phase'] != 'confirmation':
            continue
        rows = rows_by_run[record['prefix']]
        for profile, subset in profiles(rows).items():
            for population in ('all', 'nonempty', 'empty'):
                selected = [row for row in subset if population == 'all' or
                            (row['gt_count'] > 0 if population == 'nonempty' else row['gt_count'] == 0)]
                split_rows.append(dict(identity(record), profile=profile, population=population, **metrics(selected)))
        for population in ('all', 'nonempty', 'empty'):
            subset = [row for row in rows if population == 'all' or
                      (row['gt_count'] > 0 if population == 'nonempty' else row['gt_count'] == 0)]
            row = dict(identity(record), population=population, n_rows=len(subset))
            for field in ('distance_calls', 'onehop_ids', 'twohop_ids', 'page_containers', 'sparse_offsets',
                          'dense_words', 'duplicate_checks', 'eligible_expansions', 'pages_prepared',
                          'point_tests', 'point_cache_hits', 'owner_tests', 'mask_words', 'expanded',
                          'neighborhood_us', 'qualification_us', 'dir_us', 'local_indexes', 'scan_nodes', 'fanout'):
                values = [float(item['raw_output'][field]) for item in subset if field in item['raw_output']]
                row['mean_' + field] = statistics.mean(values) if values else None
            work_rows.append(row)
        cost_rows.append(dict(identity(record), **record['summary']))
    write_csv(destination / 'confirmation-populations.csv', split_rows)
    write_csv(destination / 'confirmation-work.csv', work_rows)
    dump(destination / 'confirmation-work.json', work_rows)
    write_csv(destination / 'confirmation-costs.csv', cost_rows)
    dump(destination / 'confirmation-costs.json', cost_rows)
    aggregates = []
    for dataset in manifest['datasets']:
        for profile in ('all', 'wide', 'narrow', 'strong_and', 'wide/strong_and'):
            for population in ('all', 'nonempty', 'empty'):
                cell = {'dataset': dataset, 'profile': profile, 'population': population}
                for arm in 'ABC':
                    selected = [row for row in split_rows if row['dataset'] == dataset and row['arm'] == arm and
                                row['profile'] == profile and row['population'] == population]
                    cell[arm + '_n'] = [row['n_queries'] for row in selected]
                    cell[arm + '_quality'] = len(selected) == 3 and all(row['quality_pass'] for row in selected)
                    for field in ('mean_e2e_us', 'p50_e2e_us', 'p95_e2e_us', 'p99_e2e_us'):
                        values = [row[field] for row in selected if row[field] is not None]
                        cell[arm + '_' + field] = statistics.mean(values) if len(values) == 3 else None
                    cell[arm + '_repeat_means_us'] = [row['mean_e2e_us'] for row in selected]
                for baseline in ('A', 'B'):
                    cmean, bmean = cell['C_mean_e2e_us'], cell[baseline + '_mean_e2e_us']
                    cell['C_over_' + baseline] = cmean / bmean if cmean is not None and bmean else None
                aggregates.append(cell)
    dump(destination / 'population-aggregates.json', aggregates)
    write_csv(destination / 'population-aggregates.csv', aggregates)
    e1 = load(out / 'analysis/e1_profiles.json')
    e1_summary = []
    for dataset in manifest['datasets']:
        for profile in ('all', 'wide', 'narrow', 'strong_and', 'wide/strong_and'):
            arms = {arm: next((row for row in e1 if row['dataset'] == dataset and row['profile'] == profile and
                              Path(row['source_csv']).name.endswith('.' + arm + '.csv')), None) for arm in ('B', 'C')}
            if not all(arms.values()):
                continue
            e1_summary.append({'dataset': dataset, 'profile': profile, 'n_queries': arms['B']['n_rows'],
                               'B_mean_acquire_us': arms['B']['mean_acquire_ns'] / 1000,
                               'C_mean_acquire_us': arms['C']['mean_acquire_ns'] / 1000,
                               'B_over_C_acquire_speedup': arms['B']['mean_acquire_ns'] / arms['C']['mean_acquire_ns'],
                               'B_mean_qualification_us': arms['B']['mean_qual_us'],
                               'C_mean_qualification_us': arms['C']['mean_qual_us']})
    write_csv(destination / 'e1-summary.csv', e1_summary)
    result = {'B_C_query_pairs': pairs, 'B_C_result_navigation_distance_mismatches': mismatches,
              'protocol_errors': report['protocol_errors'], 'E1_errors': report['E1_errors'],
              'empty_population_has_no_recall_quality_claim': True,
              'statistic': 'mean of three repeat-specific arithmetic means or quantiles; never pooled as independent queries',
              'E3_executed': False}
    dump(destination / 'audit.json', result)
    if mismatches or report['protocol_errors'] or report['E1_errors']:
        raise ValueError('audit failed; retained exact mismatch evidence')
    print(f'Audited {pairs} B/C query pairs, zero result/navigation/distance mismatches; split population evidence saved to {destination}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    audit(resolve(parser.parse_args().out))
