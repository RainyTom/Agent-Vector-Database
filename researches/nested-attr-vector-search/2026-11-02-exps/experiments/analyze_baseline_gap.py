"""Offline analysis of saved run26; does not rerun or modify the benchmark."""
import collections
import csv
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[4]
DATA = ROOT / 'researches/nested-attr-vector-search/2026-11-01-exps/data'
OUT = ROOT / 'researches/nested-attr-vector-search/2026-11-02-exps/data'
WORKLOAD = ROOT / 'work/dag-scope/workload-run1'
rows = list(csv.DictReader((DATA / 'dag-local-merge-run-0026.csv').open()))
selected = {
    arm: {int(r['query']): r for r in rows if r['arm'] == arm and int(r['ef']) == ef}
    for arm, ef in [('merged', 512), ('global_filtered', 32), ('nodeann', 32)]
}
counts = collections.Counter(map(int, (WORKLOAD / 'objects.txt').read_text().split()))
children = collections.defaultdict(list)
for line in (WORKLOAD / 'edges.tsv').read_text().splitlines():
    child, parent = map(int, line.split())
    children[parent].append(child)
scopes = [list(map(int, line.split()))[1:] for line in (WORKLOAD / 'queries.tsv').read_text().splitlines() if line.strip()]
n = sum(counts.values())
per_query = []
for q in sorted(selected['merged']):
    allowed = set()
    stack = list(scopes[q * (len(scopes) - 1) // 23])
    while stack:
        c = stack.pop()
        if c in allowed:
            continue
        allowed.add(c)
        stack.extend(children[c])
    sizes = [counts[c] for c in allowed if counts[c]]
    merged = selected['merged'][q]
    eligible = int(merged['eligible'])
    assert sum(sizes) == eligible
    per_query.append(dict(
        query=q, eligible=eligible, selectivity=eligible / n,
        local_graphs=len(sizes), largest_local=max(sizes),
        local_graphs_at_most_32=sum(size <= 32 for size in sizes),
        merged_distance_calls=int(merged['distance_calls']),
        merged_recall=int(merged['hits']) / int(merged['truth']),
        **{arm + '_us': float(selected[arm][q]['latency_us']) for arm in selected},
    ))
groups = []
for lo, hi in [(1, 31), (32, 127), (128, 511), (512, n)]:
    subset = [r for r in per_query if lo <= r['eligible'] <= hi]
    groups.append(dict(eligible_range=[lo, hi], queries=len(subset),
        **{arm + '_mean_us': statistics.mean(r[arm + '_us'] for r in subset) for arm in selected},
        mean_local_graphs=statistics.mean(r['local_graphs'] for r in subset),
        mean_largest_local=statistics.mean(r['largest_local'] for r in subset)))
total_global = sum(r['global_filtered_us'] for r in per_query)
small = [r for r in per_query if r['eligible'] < 32]
exact_rows = [r for r in rows if r['arm'] == 'merged' and int(r['ef']) == n]
summary = dict(objects=n, nonempty_queries=len(per_query), groups=groups,
    small_result_queries=len(small),
    small_result_fraction_of_global_time=sum(r['global_filtered_us'] for r in small) / total_global,
    merged_full_scope_distance_queries=sum(r['merged_distance_calls'] == r['eligible'] for r in per_query),
    median_local_graphs=statistics.median(r['local_graphs'] for r in per_query),
    median_eligible=statistics.median(r['eligible'] for r in per_query),
    merged_exact_point_us=statistics.mean(float(r['latency_us']) for r in exact_rows),
    per_arm={arm: dict(mean_us=statistics.mean(r[arm + '_us'] for r in per_query),
                      median_us=statistics.median(r[arm + '_us'] for r in per_query),
                      recall=statistics.mean(int(r['hits']) / int(r['truth']) for r in selected[arm].values()))
             for arm in selected},
    largest_merged_minus_node_gaps=sorted(per_query, key=lambda r: r['merged_us'] - r['nodeann_us'], reverse=True)[:5],
    limitations=['Saved-data analysis only; no new SIFT timings.',
                 'Baseline distance/expansion/adjacency counts were not recorded.',
                 'ef value is not a distance-call or expansion budget.',
                 'Small synthetic native-filter probe is not SIFT performance data.'])
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'baseline-gap-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
with (OUT / 'baseline-gap-per-query.csv').open('w') as f:
    writer = csv.DictWriter(f, fieldnames=list(per_query[0]))
    writer.writeheader()
    writer.writerows(per_query)
print(json.dumps(summary, ensure_ascii=False, indent=2))
