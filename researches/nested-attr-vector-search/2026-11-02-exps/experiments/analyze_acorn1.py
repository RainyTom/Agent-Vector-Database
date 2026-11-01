"""Join ACORN-1 observations, retaining failed quality gates and historical controls."""
import csv
import json
from pathlib import Path
import statistics
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[4]
DATA = ROOT / 'researches/nested-attr-vector-search/2026-11-02-exps/data'
ARMS = ['merged', 'global_result_filter', 'nodeann', 'global_prefilter', 'global_acorn1']
QUALITY_GATE = 0.95
OBJECTS = 168937
COUNTERS = ['distance_calls', 'distance_qualified', 'distance_rejected', 'unique_distance_items',
            'item_checks', 'item_rejects', 'owner_checks', 'owner_rejects']
ACORN_COUNTERS = ['adjacency_entries', 'bridge_expansions', 'qualified_candidates']
RESULT_FIELDS = ['eligible', 'truth', 'returned', 'hits']


def load(name):
    with (DATA / name).open() as source:
        return list(csv.DictReader(source))


def key(row):
    return row['arm'], int(row['ef']), int(row['query'])


def indexed(rows, make_key=key):
    result = {make_key(row): row for row in rows}
    assert len(result) == len(rows), 'duplicate observation keys'
    return result


def equal_fields(left, right, fields):
    for field in fields:
        assert int(left[field]) == int(right[field]), (field, left, right)


def validate_result(row):
    eligible, truth, returned, hits = (int(row[field]) for field in RESULT_FIELDS)
    assert truth == min(10, eligible)
    assert 0 <= hits <= returned <= truth
    assert int(row['distance_calls']) == int(row['distance_qualified']) + int(row['distance_rejected'])
    assert int(row['unique_distance_items']) <= int(row['distance_calls'])
    assert int(row['item_rejects']) <= int(row['item_checks'])
    assert int(row['owner_rejects']) <= int(row['owner_checks'])
    if not truth:
        assert all(int(row[field]) == 0 for field in COUNTERS)


rows = load('acorn1-profile.csv')
replay = load('acorn1-replay.csv')
sweep = load('acorn1-profile.csv.acorn1-sweep.csv')
previous = load('qualification-first-profile.csv')
previous_replay = load('qualification-first-replay.csv')
profiles, replays = indexed(rows), indexed(replay)
old, old_replays = indexed(previous), indexed(previous_replay)
sweeps = indexed(sweep, lambda row: (int(row['ef']), int(row['query'])))
assert len(rows) == len(replay) == len(sweep) == 120
assert len(previous) == len(previous_replay) == 96
assert set(profiles) == set(replays)
expected_keys = {(arm, 512 if arm == 'merged' else 32, query)
                 for arm in ARMS for query in range(24)}
assert set(profiles) == expected_keys
assert set(old) == set(old_replays) == {k for k in expected_keys if k[0] != 'global_acorn1'}
assert set(sweeps) == {(ef, query) for ef in [32, 128, 512, 2048, OBJECTS] for query in range(24)}
for row in rows:
    other = replays[key(row)]
    validate_result(row)
    validate_result(other)
    equal_fields(row, other, RESULT_FIELDS + COUNTERS)
    if row['arm'] != 'global_result_filter':
        assert int(row['distance_rejected']) == 0
    if row['arm'] == 'global_acorn1':
        equal_fields(row, other, ACORN_COUNTERS)
        equal_fields(row, sweeps[(32, int(row['query']))], RESULT_FIELDS + COUNTERS + ACORN_COUNTERS)
    else:
        # Blank means unmeasured, never evidence of zero graph-adjacency work.
        assert all(row[field] == other[field] == '' for field in ACORN_COUNTERS)
        equal_fields(row, old[key(row)], RESULT_FIELDS + COUNTERS)
        equal_fields(other, old_replays[key(row)], ['eligible', 'distance_calls', 'item_checks', 'owner_checks'])
for row in sweep:
    validate_result(row)
    assert int(row['distance_rejected']) == 0
    baseline = profiles[('global_acorn1', 32, int(row['query']))]
    equal_fields(row, baseline, ['eligible', 'truth'])
    assert all(int(row[field]) >= 0 for field in ACORN_COUNTERS)


def quality(subset):
    nonempty = [row for row in subset if int(row['truth'])]
    recall = statistics.mean(int(row['hits']) / int(row['truth']) for row in nonempty)
    passed = recall >= QUALITY_GATE
    return dict(queries=len(subset), nonempty_queries=len(nonempty), recall=recall,
                quality_gate_met=passed, quality_status='PASS' if passed else 'FAIL',
                gate_passing_normal_us=statistics.mean(float(row['normal_us']) for row in nonempty) if passed else None,
                mean_returned=statistics.mean(int(row['returned']) for row in nonempty),
                missing_results_queries=sum(int(row['returned']) < int(row['truth']) for row in nonempty))


def means(subset, fields):
    nonempty = [row for row in subset if int(row['truth'])]
    return {field: statistics.mean(float(row[field]) for row in nonempty) for field in fields}


summary = dict(objects=OBJECTS, queries=24, quality_gate=QUALITY_GATE,
    verified_profile_rows=len(rows), verified_replay_rows=len(replay), verified_sweep_rows=len(sweep),
    unchanged_historical_rows=len(previous), unchanged_historical_replay_rows=len(previous_replay),
    historical_comparison='Exact returned/hit, predicate, and distance counts; latency is not compared for equality.',
    provenance=dict(strategy='ACORN-1 search strategy, paper section 5.3: all one-hop plus all two-hop neighbors, '
                             'including through qualified intermediates, followed by predicate filtering and M truncation.',
        index='Unchanged diversified hnswlib HNSW graph; not the author unpruned ACORN-1 construction and no ACORN-gamma construction.',
        hnswlib_commit='ca426729609b3221563047ceb269cc1213e0d376',
        distance='RawL2Space exact FP32 L2; no FAISS runtime and no quantization.'),
    deviations=dict(entry_selection='Existing qualified-owner static representatives are a local hnswlib adaptation.',
        construction='Paper ACORN-1 omits RNG diversification; this transfer retains native hnswlib diversification and M/2M storage.',
        enumeration='Native direct-list order followed by all second-hop lists; stable local dedup excludes zero-hop self, paper-M cap at every level.',
        author_snapshot='Author c259f11cdbec880671658eaa0d8747e2cc9de79d interleaves traversal, uses nominal2M bottom cap and different bounded-queue/visited mechanics; not copied as a runtime.',
        fallback='Paper s_min brute prefilter fallback is explicitly disabled by the graph-primary/no-silent-fallback contract.',
        reproduction_scope='Search-strategy integration only; not the author FAISS implementation or full index/system reproduction.',
        interpretation='Report observed quality limits rather than silently falling back to a scan.'),
    semantics='Global HNSW ACORN-1 expanded neighborhoods with qualification before every exact FP32 L2. '
              'Qualified owner representatives adapt entry selection locally to hnswlib; not a full author-system reproduction. '
              'One bottom-layer frontier; no hidden scan fallback.',
    legacy_reference='global_result_filter is the native result-only callback; global_prefilter is the strict disconnected-graph control.',
    adjacency_measurement=dict(supported_arms=['global_acorn1'],
        adjacency_entries='Every examined adjacency-list entry, including upper/base layers and one/two hops.',
        bridge_expansions='Each intermediate list expanded for second-hop lookup; qualified and rejected intermediates are included.',
        qualified_candidates='Unique qualified candidates emitted per expanded-neighborhood lookup after local dedup/filter and before M truncation.',
        unsupported_arms='Other arms have unmeasured adjacency work, NOT zero adjacency work.'),
    predicate_attribution='Probe metadata lookups only attribute distances and are excluded from algorithmic predicate counts.',
    arms=[], acorn1_sweep=[])
for arm in ARMS:
    subset = [row for row in rows if row['arm'] == arm]
    result = dict(arm=arm, ef=int(subset[0]['ef']), **quality(subset))
    result['means'] = means(subset, ['normal_us', 'count_pass_us', 'profile_pass_us', 'scope_us'] + COUNTERS +
                            ['distance_est_us', 'item_est_us', 'owner_est_us'])
    result['replay_us'] = means([replays[key(row)] for row in subset],
                                ['l2_replay_us', 'item_replay_us', 'owner_replay_us'])
    result['adjacency_counters'] = (dict(status='measured', means=means(subset, ACORN_COUNTERS))
                                    if arm == 'global_acorn1' else dict(status='unsupported/unmeasured', means=None))
    summary['arms'].append(result)
for ef in [32, 128, 512, 2048, OBJECTS]:
    subset = [row for row in sweep if int(row['ef']) == ef]
    summary['acorn1_sweep'].append(dict(ef=ef, **quality(subset),
        means=means(subset, ['normal_us'] + COUNTERS + ACORN_COUNTERS)))
summary['acorn1_any_quality_gate_met'] = any(row['quality_gate_met'] for row in summary['acorn1_sweep'])
summary['limits'] = [
    'Fixed 24-query optimization workload with randomized owner binding and OR scopes; no generalization claim.',
    'Empty queries remain consistency-checked and are excluded from recall and mean latency, matching existing analysis.',
    'A failed quality gate is explicit; its raw latency is descriptive only, never a same-quality win.',
    'Component profiling perturbs search; timer-adjusted estimates and trace replay kernels are not additive query costs.',
    'Adjacency counters are ACORN-only; unsupported arms cannot be assigned zero adjacency cost.',
    'No FAISS runtime or quantization; pinned hnswlib and RawL2Space exact FP32 L2 are retained.'
]
if len(sys.argv) > 1 and sys.argv[1] == '--canonical':
    canonical = load('acorn1-canonical.csv')
    assert len(canonical) == 575
    for row in canonical:
        if row['arm'] == 'global_acorn1':
            equal_fields(row, sweeps[(int(row['ef']), int(row['query']))], ['eligible','truth','hits'])
        elif key(row) in profiles:
            equal_fields(row, profiles[key(row)], ['eligible','truth','hits'])
    output = (DATA / 'acorn1-canonical-output.txt').read_text()
    metrics = {line[7:].split('=',1)[0]: float(line.split('=',1)[1])
               for line in output.splitlines() if line.startswith('METRIC ')}
    assert bool(metrics['global_acorn1_quality_gate']) == summary['acorn1_any_quality_gate_met']
    assert abs(metrics['global_acorn1_recall_max'] - max(r['recall'] for r in summary['acorn1_sweep'])) < 1e-10
    assert ('global_acorn1_us' in metrics) == summary['acorn1_any_quality_gate_met']
    assert metrics['reference_exhaustive_errors'] == 0
    summary['canonical'] = dict(entrypoint='bash autoresearch.sh', rows_crosschecked=len(canonical), metrics=metrics)
    implementation = ROOT / 'researches/nested-attr-vector-search/2026-11-02-exps/experiments/global_acorn1_hnsw.h'
    summary['implementation_sha256'] = hashlib.sha256(implementation.read_bytes()).hexdigest()
    destination = 'acorn1-complete-summary.json'
else:
    destination = 'acorn1-final-summary.json'
text = json.dumps(summary, ensure_ascii=False, indent=2) + '\n'
(DATA / destination).write_text(text)
print(text, end='')
