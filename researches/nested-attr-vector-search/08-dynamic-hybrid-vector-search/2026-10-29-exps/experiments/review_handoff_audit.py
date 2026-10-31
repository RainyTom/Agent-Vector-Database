#!/usr/bin/env python3
"""Recompute published summaries only; does not rerun ANN or certify raw traces."""
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean

EXP = Path(__file__).resolve().parents[1]
FILES = [
    EXP / 'data/dynamic_triad/wiki-topology/tape-main-ef1024/results.json',
    EXP / 'data/dynamic_triad/wiki-topology/scale/tape-main-ef1024/results.json',
]


def main():
    runs = []
    sources = []
    for path in FILES:
        raw = path.read_bytes()
        doc = json.loads(raw)
        sources.append({'path': str(path.relative_to(EXP)),
                        'sha256': hashlib.sha256(raw).hexdigest(),
                        'runs': len(doc['runs'])})
        runs.extend(doc['runs'])
    errors = []
    groups = defaultdict(list)
    paired = defaultdict(dict)
    for r in runs:
        s, c = r['summary'], r['config']
        if not (r['quality_pass'] and s['macro_recall'] >= .999 and s['pair_recall'] >= .999):
            errors.append([r['config_id'], r['arm'], 'quality'])
        if any(s[k] for k in ('invalid_results', 'duplicate_results', 'state_violations', 'gt_distance_calls')):
            errors.append([r['config_id'], r['arm'], 'invariant'])
        parts = ('tombstone_compaction', 'threshold_build', 'threshold_drop')
        if s['compaction_count'] != sum(s[p + '_count'] for p in parts):
            errors.append([r['config_id'], r['arm'], 'maintenance partition'])
        if abs(s['total_workload_us'] - s['query_total_us'] - s['update_total_us']) > .01:
            errors.append([r['config_id'], r['arm'], 'service accounting'])
        key = (c['cohort_root'], c['topology'], c['target_q_u'], r['arm'])
        groups[key].append(s)
        paired[(r['config_id'], r['repeat'])][r['arm']] = s
    points = []
    for (root, topology, ratio, arm), summaries in sorted(groups.items()):
        points.append({'root': root, 'topology': topology, 'nominal_q_u': ratio,
                       'arm': arm, 'repeats': len(summaries),
                       'service_s': mean(s['total_workload_us'] / 1e6 for s in summaries),
                       'build_s': mean(s['build_s'] for s in summaries),
                       'cold_one_window_s': mean(s['build_s'] + s['total_workload_us'] / 1e6 for s in summaries),
                       'tombstone_fraction_of_updates': mean(s['tombstone_compaction_us'] / s['update_total_us'] for s in summaries)})
    pair_checks = []
    for (config, repeat), arms in sorted(paired.items()):
        pair_checks.append({'config': config, 'repeat': repeat,
                           'service_winner': min(arms, key=lambda a: arms[a]['total_workload_us']),
                           'cold_one_window_winner': min(arms, key=lambda a: arms[a]['build_s'] + arms[a]['total_workload_us'] / 1e6)})
    historical = []
    for family in ('screening', 'crossover', 'move-crossover', 'pilot'):
        path = EXP / 'data/dynamic_triad' / family / 'results.json'
        raw = path.read_bytes()
        old_runs = json.loads(raw)['runs']
        historical.append({'family': family, 'sha256': hashlib.sha256(raw).hexdigest(),
                           'runs': len(old_runs),
                           'query_executions': sum(r['summary']['n_queries'] for r in old_runs),
                           'quality_failed': sum(not r['quality_pass'] for r in old_runs),
                           'timing_status': 'inline GT; diagnostic only'})
    prune_path = EXP.parents[1] / '01-directory-aware-acceleration/2026-10-28-exps/data/scope_prune_probe.json'
    prune = json.loads(prune_path.read_text())['anchors'][0]
    tau = prune['tau']
    zero_at_zero_slack = all(next(s for s in p['sweep'] if s['slack'] == 0)['pruned_blocks_frac'] == 0
                            for p in prune['partitions'].values())
    prune_inference = {'query_index': prune['query_index'], 'squared_tau': tau,
                       'correct_euclidean_tau': math.sqrt(tau),
                       'saved_zero_slack_pruning_is_zero': zero_at_zero_slack,
                       'unit_correction_cannot_increase_pruning_for_same_bounds': 0 < tau < 1 and zero_at_zero_slack,
                       'kind': 'monotonicity inference from saved threshold; not a rerun'}
    out = {'kind': 'offline audit of committed summaries, not new ANN measurements',
           'sources': sources, 'runs': len(runs),
           'query_executions': sum(r['summary']['n_queries'] for r in runs),
           'update_executions': sum(r['summary']['n_updates'] for r in runs),
           'min_macro_recall': min(r['summary']['macro_recall'] for r in runs),
           'min_pair_recall': min(r['summary']['pair_recall'] for r in runs),
           'benchmark_gt_distance_calls': sum(r['summary']['gt_distance_calls'] for r in runs),
           'errors': errors, 'points': points, 'paired_winners': pair_checks,
           'historical_inline': historical,
           'single_query_pruning_inference': prune_inference,
           'limits': ['Original vectors, tape and per-event CSV are not independently revalidated here.',
                      'Cold cost is arithmetic for one measured window; no stationarity or future break-even implied.',
                      'Repeated query executions are not independent query samples.']}
    target = EXP / 'data/independent-review-audit-20261029.json'
    target.write_text(json.dumps(out, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: v for k, v in out.items() if k not in ('sources', 'points', 'paired_winners', 'limits')}, ensure_ascii=False))
    print('Cold one-window winners:', dict((a, sum(p['cold_one_window_winner'] == a for p in pair_checks)) for a in 'ABC'))
    print('Output:', target.relative_to(EXP))
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
