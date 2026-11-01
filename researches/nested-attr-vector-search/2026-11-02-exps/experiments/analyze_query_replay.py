"""Join actual operation counters with independent batch-kernel replay costs."""
import csv
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[4]
DATA = ROOT / 'researches/nested-attr-vector-search/2026-11-02-exps/data'
profiles = list(csv.DictReader((DATA / 'query-cost-profile-isolated.csv').open()))
replays = list(csv.DictReader((DATA / 'query-cost-replay.csv').open()))
initial = list(csv.DictReader((DATA / 'query-cost-profile.csv').open()))
key = lambda r: (r['arm'], int(r['ef']), int(r['query']))
lookup = {key(r): r for r in profiles}
initial_lookup = {key(r): r for r in initial}
assert len(replays) == len(profiles) == len(initial) == 72
for r in replays:
    p = lookup[key(r)]
    for field in ['eligible', 'distance_calls', 'item_checks', 'owner_checks']:
        assert int(r[field]) == int(p[field]) == int(initial_lookup[key(r)][field])
result = dict(rows_crosschecked=72, nonempty_queries=23, operating_points={'merged':512,'global_filtered':32,'nodeann':32},
    replay_protocol='Exact per-query distance-pointer and predicate-ID traces; repeat each full trace 1..256 rounds to amortize clocks, warm1, median5. Includes replay loop/checksum overhead. Not additive online query attribution.',
    timing_limits='Online timestamp brackets perturb execution. Calibrated estimates may be negative and are retained in raw CSV. Replay removes per-operation clocks but changes cache/prefetch/branch/interleaving context. Neither estimates nor replay costs may be subtracted from normal latency.',
    arms=[])
for arm in ['merged', 'global_filtered', 'nodeann']:
    rr = [r for r in replays if r['arm']==arm and int(r['eligible'])]
    pp = [lookup[key(r)] for r in rr]
    counters = {name: statistics.mean(int(r[name]) for r in pp) for name in [
        'distance_calls','distance_qualified','distance_rejected','unique_distance_items',
        'item_checks','item_rejects','owner_checks','owner_rejects']}
    result['arms'].append(dict(arm=arm, queries=len(rr), ef=int(rr[0]['ef']), counters=counters,
        normal_us=statistics.mean(float(p['normal_us']) for p in pp),
        scope_profile_us=statistics.mean(float(p['scope_us']) for p in pp),
        online_component_estimates_us={name:statistics.mean(float(p[name]) for p in pp)
            for name in ['distance_est_us','item_est_us','owner_est_us']},
        replay_us={name:statistics.mean(float(r[name]) for r in rr)
            for name in ['l2_replay_us','item_replay_us','owner_replay_us']},
        recall=statistics.mean(int(p['hits'])/int(p['truth']) for p in pp)))
(DATA / 'query-cost-components-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
print(json.dumps(result, ensure_ascii=False, indent=2))
