"""Analyze strict pre-distance qualification without hiding missing results."""
import csv
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[4]
DATA = ROOT / 'researches/nested-attr-vector-search/2026-11-02-exps/data'
rows = list(csv.DictReader((DATA / 'qualification-first-profile.csv').open()))
replay = list(csv.DictReader((DATA / 'qualification-first-replay.csv').open()))
sweep = list(csv.DictReader((DATA / 'qualification-first-profile.csv.sweep.csv').open()))
previous = list(csv.DictReader((DATA / 'query-cost-profile-isolated.csv').open()))
key = lambda r: (r['arm'], int(r['ef']), int(r['query']))
replays = {key(r):r for r in replay}
old = {key(r):r for r in previous}
assert len(rows) == len(replay) == 96 and len(sweep) == 120
for r in rows:
    rr = replays[key(r)]
    for field in ['eligible', 'distance_calls', 'item_checks', 'owner_checks']:
        assert int(r[field]) == int(rr[field])
    if r['arm'] == 'global_prefilter':
        assert int(r['distance_rejected']) == 0
    else:
        old_arm = 'global_filtered' if r['arm']=='global_result_filter' else r['arm']
        before = old[(old_arm, int(r['ef']), int(r['query']))]
        for field in ['eligible','truth','hits','distance_calls','distance_rejected','item_checks','owner_checks']:
            assert int(r[field]) == int(before[field])
summary = dict(objects=168937, queries=24, nonempty_queries=23,
    semantics='Strict item qualification BEFORE distance at entry, all upper layers, and layer0. Rejected vertices are neither scored nor expanded. Qualified owner highest-level static representatives seed one shared frontier; no hidden exact fallback.',
    legacy_reference='global_result_filter is native result-only callback, retained only as a separate historical reference.',
    quality_gate=0.95, arms=[], prefilter_sweep=[])
for arm in ['merged','global_prefilter','nodeann','global_result_filter']:
    subset = [r for r in rows if r['arm']==arm and int(r['truth'])]
    means = {field:statistics.mean(float(r[field]) for r in subset) for field in [
        'normal_us','distance_calls','distance_rejected','unique_distance_items','item_checks','item_rejects',
        'owner_checks','owner_rejects','scope_us','distance_est_us','item_est_us','owner_est_us']}
    replay_means = {field:statistics.mean(float(replays[key(r)][field]) for r in subset)
                    for field in ['l2_replay_us','item_replay_us','owner_replay_us']}
    recall = statistics.mean(int(r['hits'])/int(r['truth']) for r in subset)
    summary['arms'].append(dict(arm=arm, ef=int(subset[0]['ef']), queries=len(subset), recall=recall,
        quality_gate_met=recall>=0.95, missing_results_queries=sum(int(r['returned'])<int(r['truth']) for r in subset),
        mean_returned=statistics.mean(int(r['returned']) for r in subset), means=means, replay_us=replay_means))
for ef in sorted({int(r['ef']) for r in sweep}):
    subset = [r for r in sweep if int(r['ef'])==ef and int(r['truth'])]
    assert all(int(r['distance_rejected'])==0 for r in subset)
    recall = statistics.mean(int(r['hits'])/int(r['truth']) for r in subset)
    summary['prefilter_sweep'].append(dict(ef=ef, recall=recall, quality_gate_met=recall>=0.95,
        normal_us=statistics.mean(float(r['normal_us']) for r in subset),
        distance_calls=statistics.mean(int(r['distance_calls']) for r in subset),
        mean_returned=statistics.mean(int(r['returned']) for r in subset),
        missing_results_queries=sum(int(r['returned'])<int(r['truth']) for r in subset)))
summary['prefilter_any_quality_gate_met'] = any(r['quality_gate_met'] for r in summary['prefilter_sweep'])
summary['limits'] = ['Tiny fixed optimization workload and randomized owner binding, OR scopes only.',
    'Profile estimates and replay times are not additive unperturbed costs.',
    'Low-recall latency must not be presented as same-quality acceleration.',
    'A failed ef sweep does not measure real-data component count; upper greedy entry coverage also matters.']
canonical = list(csv.DictReader((DATA / 'qualification-first-canonical.csv').open()))
assert len(canonical) == 460
profile_lookup = {key(r):r for r in rows}
sweep_lookup = {(int(r['ef']),int(r['query'])):r for r in sweep}
for r in canonical:
    if r['arm']=='global_prefilter':
        other = sweep_lookup[(int(r['ef']),int(r['query']))]
    elif key(r) in profile_lookup:
        other = profile_lookup[key(r)]
    else:
        continue
    assert all(int(r[field])==int(other[field]) for field in ['eligible','truth','hits'])
output = (DATA / 'qualification-first-canonical-output.txt').read_text()
metrics = {line[7:].split('=',1)[0]:float(line.split('=',1)[1])
           for line in output.splitlines() if line.startswith('METRIC ')}
assert metrics['global_prefilter_quality_gate']==0
assert metrics['reference_exhaustive_errors']==0
assert metrics['global_prefilter_exhaustive_miss_queries']==22
assert 'global_prefilter_us' not in metrics and 'global_filtered_us' not in metrics
assert 'global_result_filter_us' in metrics
summary['canonical'] = dict(rows_crosschecked=len(canonical), metrics=metrics,
    entrypoint='bash autoresearch.sh',quality_failure_is_explicit=True,
    low_quality_latency_is_not_a_gate_passing_metric=True)
(DATA / 'qualification-first-final-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n')
print(json.dumps(summary, ensure_ascii=False, indent=2))
