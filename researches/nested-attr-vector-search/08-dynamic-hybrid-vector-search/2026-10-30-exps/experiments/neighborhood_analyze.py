#!/usr/bin/env python3
"""Analyze only recorded serial neighborhood evidence; no benchmarks or tuning.

  python PATH/neighborhood_analyze.py --out OUT

Writes a fresh OUT/analysis directory with every configuration (including
failures), nonempty quality, separately counted empty correctness, profile
summaries, frozen confirmation repeat means and same-statistic C/A and C/B
ratios. Portable PNGs are rendered separately by neighborhood_figures.py.
A time ratio below one means C is faster. No E3 is executed or inferred from calibration alone.
"""

import argparse
import math
from pathlib import Path
import statistics
import sys

from neighborhood_experiment import (COUNTERS, comparison_errors, dump, file_record,
                                     load, metrics, quantile, resolve, sha256, write_csv)


def profiles(rows):
    result = {'all': rows}
    for scope in ('wide', 'middle', 'narrow'):
        result[scope] = [row for row in rows if row['scope_profile'] == scope]
        for strength in ('strong_and', 'other'):
            result[f'{scope}/{strength}'] = [row for row in result[scope] if row['strong_and'] == (strength == 'strong_and')]
    result['strong_and'] = [row for row in rows if row['strong_and']]
    result['other'] = [row for row in rows if not row['strong_and']]
    return result


def identity(record):
    return {key: record[key] for key in ('dataset', 'phase', 'arm', 'ef', 'repeat', 'serial_order')}


def configuration_row(record):
    row = dict(identity(record), returncode=record['returncode'], prefix=record['prefix'],
               errors=record['errors'], **record['metrics'])
    for key, value in record.get('summary', {}).items():
        if isinstance(value, (int, float, str, bool)) or value is None:
            row['summary_' + key] = value
    return row


def analyze_e1(out, destination, manifest):
    records_path = out / 'e1/records.json'
    if not records_path.exists():
        return {'status': 'not run', 'performance_results': 'none yet'}, []
    records = load(records_path)
    summaries, raw_records, errors = [], [], []
    for record in records:
        dataset = record['dataset']
        for artifact in record['artifacts']:
            if sha256(artifact['path']) != artifact['sha256']:
                errors.append(f'{dataset}: E1 artifact changed: {artifact["path"]}')
        if record['returncode']:
            errors.append(f'{dataset}: E1 process exit {record["returncode"]}')
        # Preserve all diagnostics, even when the binary's separate E1 schema evolves.
        csv_paths = [Path(item['path']) for item in record['artifacts'] if item['path'].endswith('.csv')]
        selection = load(manifest['datasets'][dataset]['selection']['path'])
        import csv
        for path in csv_paths:
            with path.open(newline='') as handle:
                rows = list(csv.DictReader(handle))
            for raw in rows:
                raw_records.append({'dataset': dataset, 'source_csv': str(path), **raw})
            required = {'query_index', 'center_count', 'acquire_ns', 'qual_us'}
            if not rows or not required.issubset(rows[0]):
                errors.append(f'{dataset}: {path.name} lacks E1 acquisition schema {sorted(required)}; raw retained, no invented timings')
                continue
            numeric = []
            for raw in rows:
                index = int(raw['query_index'])
                if not 0 <= index < len(selection):
                    errors.append(f'{dataset}: E1 query_index outside full selected query set')
                    continue
                row = dict(selection[index])
                for key, value in raw.items():
                    try:
                        number = float(value)
                        if math.isfinite(number):
                            row[key] = number
                    except (ValueError, TypeError):
                        pass
                numeric.append(row)
            if len({row['selected_row'] for row in numeric}) != len(selection):
                errors.append(f'{dataset}: E1 diagnostics do not cover full selected query set')
            for profile, subset in profiles(numeric).items():
                result = {'dataset': dataset, 'source_csv': str(path), 'profile': profile, 'n_rows': len(subset)}
                for key in sorted({key for row in rows for key in row} - {'query_index', 'qidx', 'gt_ids', 'result_ids'}):
                    values = [row[key] for row in subset if isinstance(row.get(key), (int, float)) and not isinstance(row[key], bool)]
                    if values:
                        result[f'mean_{key}'] = statistics.mean(values)
                        result[f'p50_{key}'] = quantile(values, .5)
                        result[f'p95_{key}'] = quantile(values, .95)
                summaries.append(result)
    write_csv(destination / 'e1_raw.csv', raw_records)
    dump(destination / 'e1_raw.json', raw_records)
    write_csv(destination / 'e1_profiles.csv', summaries)
    dump(destination / 'e1_profiles.json', summaries)
    return {'status': 'recorded', 'records': records, 'errors': errors, 'not_e2_performance': True}, errors


def analyze(args):
    out = resolve(args.out)
    manifest = load(out / 'manifest.json')
    records_path = out / 'runs/records.json'
    records = load(records_path) if records_path.exists() else []
    destination = out / 'analysis'
    destination.mkdir()  # Fresh analysis output; never silently replace a previous report.
    protocol_errors = comparison_errors(records)
    configuration_rows, profile_rows, normalized_rows = [], [], {}
    for record in records:
        for artifact in record['artifacts']:
            if sha256(artifact['path']) != artifact['sha256']:
                protocol_errors.append(f'{record["dataset"]}: raw artifact changed: {artifact["path"]}')
        configuration_rows.append(configuration_row(record))
        path = Path(record['prefix'] + '.normalized.json')
        if not path.exists():
            continue
        rows = load(path)
        normalized_rows[record['prefix']] = rows
        for profile, subset in profiles(rows).items():
            values = metrics(subset)
            values['quality_pass'] &= not record['errors']
            profile_rows.append(dict(identity(record), profile=profile, **values))
    write_csv(destination / 'configurations.csv', configuration_rows)
    dump(destination / 'configurations.json', configuration_rows)
    write_csv(destination / 'profiles.csv', profile_rows)
    dump(destination / 'profiles.json', profile_rows)
    frozen_path = out / 'runs/frozen_budgets.json'
    frozen = load(frozen_path) if frozen_path.exists() else {'selected': {}, 'not_run': True}
    repeated, ratio_rows, eligibility = [], [], {}
    for dataset in manifest['datasets']:
        dataset_errors = [error for error in protocol_errors if error.startswith(dataset + ':') or error.startswith(dataset + '/')]
        arm_means, arm_quality = {}, {}
        for arm in 'ABC':
            runs = [record for record in records if record['dataset'] == dataset and record['arm'] == arm and record['phase'] == 'confirmation']
            means = [record['metrics']['mean_e2e_us'] for record in runs if record['metrics'].get('mean_e2e_us') is not None]
            all_quality = len(runs) == 3 and len(means) == 3 and all(record['metrics']['quality_pass'] for record in runs) and not dataset_errors
            arm_quality[arm] = all_quality
            arm_means[arm] = statistics.mean(means) if len(means) == 3 else None
            repeated.append({'dataset': dataset, 'arm': arm,
                             'ef': frozen.get('selected', {}).get(dataset, {}).get(arm, {}).get('ef'),
                             'n_confirmation_repeats': len(runs), 'repeat_mean_e2e_us': means,
                             'mean_of_repeat_means_e2e_us': arm_means[arm],
                             'stdev_repeat_means_e2e_us': statistics.stdev(means) if len(means) > 1 else None,
                             'min_repeat_mean_e2e_us': min(means) if means else None,
                             'max_repeat_mean_e2e_us': max(means) if means else None,
                             'all_repeats_quality_pass': all_quality,
                             'nonempty_macro_recall_by_repeat': [record['metrics'].get('nonempty_macro_recall') for record in runs],
                             'pair_recall_by_repeat': [record['metrics'].get('pair_recall') for record in runs],
                             'empty_count_by_repeat': [record['metrics'].get('n_empty') for record in runs]})
        ratios = {}
        for comparator in ('A', 'B'):
            valid = arm_quality.get('C') and arm_quality.get(comparator)
            ratio = arm_means['C'] / arm_means[comparator] if valid and arm_means.get(comparator, 0) else None
            ratios[comparator] = ratio
            ratio_rows.append({'dataset': dataset, 'comparison': f'C/{comparator}',
                               'statistic': 'mean of 3 serial confirmation repeat arithmetic mean e2e_us',
                               'quality_comparable': bool(valid), 'C_time_over_baseline_time': ratio,
                               'baseline_over_C_speedup': 1 / ratio if ratio and ratio > 0 else None,
                               'C_mean_e2e_us': arm_means.get('C'), 'baseline_mean_e2e_us': arm_means.get(comparator),
                               'reason_if_unavailable': None if ratio is not None else 'missing 3 passing frozen confirmation repeats or incompatible graph/GT/artifacts'})
        eligibility[dataset] = {'C_gains_vs_A_and_B_at_quality': all(value is not None and value < 1 for value in ratios.values()),
                                'ratios_C_over_baseline_mean_time': ratios, 'E3_executed': False,
                                'note': 'Observed repeated means only; no significance claim and no E3 authorization from calibration.'}
        # Profile ratios also compare arithmetic mean to arithmetic mean, not medians to means.
        for profile in ('all', 'wide', 'middle', 'narrow', 'strong_and', 'wide/strong_and', 'narrow/strong_and'):
            grouped = {}
            for arm in 'ABC':
                grouped[arm] = [row for row in profile_rows if row['dataset'] == dataset and row['arm'] == arm and row['phase'] == 'confirmation' and row['profile'] == profile]
            for baseline in ('A', 'B'):
                crows, brows = grouped['C'], grouped[baseline]
                valid = len(crows) == len(brows) == 3 and all(row['quality_pass'] for row in crows + brows) and arm_quality.get('C') and arm_quality.get(baseline)
                cmean = statistics.mean(row['mean_e2e_us'] for row in crows) if len(crows) == 3 and all(row['mean_e2e_us'] is not None for row in crows) else None
                bmean = statistics.mean(row['mean_e2e_us'] for row in brows) if len(brows) == 3 and all(row['mean_e2e_us'] is not None for row in brows) else None
                ratio_rows.append({'dataset': dataset, 'profile': profile, 'comparison': f'C/{baseline}',
                                   'statistic': 'mean of 3 serial confirmation repeat profile arithmetic mean e2e_us',
                                   'quality_comparable': bool(valid), 'C_mean_e2e_us': cmean, 'baseline_mean_e2e_us': bmean,
                                   'C_time_over_baseline_time': cmean / bmean if valid and bmean else None})
    write_csv(destination / 'confirmation_repeated_means.csv', repeated)
    dump(destination / 'confirmation_repeated_means.json', repeated)
    write_csv(destination / 'ratios_at_quality.csv', ratio_rows)
    dump(destination / 'ratios_at_quality.json', ratio_rows)
    e1, e1_errors = analyze_e1(out, destination, manifest)
    report = {'protocol': manifest['protocol'], 'source_sha': manifest['source_sha'],
              'hnsw_head': manifest['hnsw_head'], 'binary': manifest['binary'], 'baseline': manifest['baseline'],
              'manifest': file_record(out / 'manifest.json'), 'configuration_count': len(records),
              'performance_results': 'recorded serial evidence; see complete configurations' if records else 'none yet',
              'all_configurations_retained': True, 'frozen_budgets': frozen,
              'dataset_counts_and_shortages': {name: {'counts': value['counts'], 'shortages': value['shortages']} for name, value in manifest['datasets'].items()},
              'protocol_errors': sorted(set(protocol_errors)), 'ratios': ratio_rows,
              'E3_eligibility': eligibility, 'E3_executed': False,
              'E3_query_gain_prerequisite_met_for_all_datasets': bool(records) and all(value['C_gains_vs_A_and_B_at_quality'] for value in eligibility.values()),
              'E1': e1, 'E1_errors': e1_errors,
              'limitations': [
                  'This frozen first200 protocol differs from dynamic-state/update replay query sets; do not compare its timings or recall to those experiments.',
                  'WIKI is the complete supplied original root-3626 subtree, not the full WIKI corpus; SIFT uses the supplied synthetic hierarchy.',
                  'Calibration and confirmation are intentionally disjoint qidx groups; only confirmation arms share the same confirmation query set.',
                  'GT ID sets are checked across configurations at each split. Perfect recall alone does not establish identical GT tie handling.',
                  'Exact distance-tie prevalence is not emitted by the binaries; no tie frequency or statistical-significance claim is made.',
                  'Empty-GT rows are excluded from macro/pair quality gates and shown with separate empty correctness/counts.',
                  'No concurrent-vs-serial or mean-vs-median speed comparisons are made; all ratios compare serial arithmetic means.',
                  'A profile with no nonempty rows cannot pass the quality gate. Missing cells are shortages, never regenerated queries.',
              ]}
    dump(destination / 'report.json', report)
    print(f'Analysis saved to {destination}; E3 not executed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', required=True)
    try:
        analyze(parser.parse_args())
    except (OSError, ValueError, KeyError) as error:
        sys.exit(f'neighborhood analysis: {error}')
