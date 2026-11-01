"""Aggregate exact counters and explicitly perturbed component timings."""
import csv
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[4]
DATA = ROOT / 'researches/nested-attr-vector-search/2026-11-02-exps/data'
rows = list(csv.DictReader((DATA / 'query-cost-profile-isolated.csv').open()))
meta = json.loads((DATA / 'query-cost-profile-isolated-meta.json').read_text())
assert len(rows) == meta['verified_query_arm_rows'] == 72
old = list(csv.DictReader((ROOT / 'researches/nested-attr-vector-search/2026-11-01-exps/data/dag-local-merge-run-0026.csv').open()))
old_lookup = {(r['arm'], int(r['ef']), int(r['query'])): r for r in old}
for r in rows:
    if int(r['eligible']):
        before = old_lookup[(r['arm'], int(r['ef']), int(r['query']))]
        assert all(int(r[k]) == int(before[k]) for k in ['eligible', 'truth', 'hits'])
        if r['arm'] == 'merged':
            assert int(r['distance_calls']) == int(before['distance_calls'])
    assert int(r['distance_calls']) == int(r['distance_qualified']) + int(r['distance_rejected'])
    if r['arm'] != 'global_filtered':
        assert int(r['distance_rejected']) == 0
    if r['arm'] == 'nodeann':
        assert int(r['item_checks']) == 0
summary = {'metadata': meta, 'arms': [], 'interpretation': [
    'Counts include actual raw-kernel invocations across all HNSW levels, including repeated items.',
    'Only nonempty queries are aggregated, matching the existing benchmark recall convention.',
    'Item and owner checks are algorithmic checks only; diagnostic distance-attribution lookups are excluded.',
    'Normal time has no per-operation timers and native distance pointers are unwrapped.',
    'Raw component timings belong to a separate perturbed pass, not normal latency.',
    'Estimated times subtract empty clock-bracket calibration; negative values remain visible and mean unresolved.',
    'Do not subtract components from normal time or interpret corrected estimates as exact unperturbed costs.',
]}
columns = [k for k in rows[0] if k not in ['arm', 'ef', 'query', 'eligible', 'truth', 'hits']]
for arm in ['merged', 'global_filtered', 'nodeann']:
    subset = [r for r in rows if r['arm'] == arm and int(r['truth'])]
    means = {key: statistics.mean(float(r[key]) for r in subset) for key in columns}
    raw_sum = means['scope_us'] + means['distance_raw_us'] + means['item_raw_us'] + means['owner_raw_us']
    summary['arms'].append(dict(arm=arm, ef=int(subset[0]['ef']), queries=len(subset),
        recall=statistics.mean(int(r['hits']) / int(r['truth']) for r in subset), means=means,
        profile_to_normal=means['profile_pass_us'] / means['normal_us'],
        count_to_normal=means['count_pass_us'] / means['normal_us'],
        rejected_distance_fraction=means['distance_rejected'] / means['distance_calls'],
        profile_remaining_us=means['profile_pass_us'] - raw_sum,
        negative_component_estimate_rows={key: sum(float(r[key]) < 0 for r in subset)
                                          for key in ['distance_est_us', 'item_est_us', 'owner_est_us']}))
(DATA / 'query-cost-isolated-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(summary, ensure_ascii=False, indent=2))
