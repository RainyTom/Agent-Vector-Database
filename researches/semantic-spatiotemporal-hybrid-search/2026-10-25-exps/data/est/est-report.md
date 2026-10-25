# Episode Span Tree (EST) vs flat-bucket catalog — 2026-10-25

## Setup

- Workload: `production/semantic-mobility-search/work/all6_8760h_s50_potion`
  (6 cities, 1,838,793 points, 63,084 users, 500 queries, k=10, 8760 h window).
- Config: `--topc 300 --cap-hours 48`, default `--rank proj` (64-D projection
  sketch, top-C=300 verified by banded DTW), serial query loop.
- New binary: `production/semantic-mobility-search/src/bench_est.cpp`
  (copy of `bench.cpp` + mode `catalog-est`); built with
  `g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib src/bench_est.cpp -o work/bench_est`.
- EST structure: static k-d tree over the 6,213,067 materialized spans
  (all spans with duration <= 48 h, same span set as the catalog), keys
  `(start_ts, dur)`, leaf size 64. Nodes store `ts_min/ts_max`,
  `dur_min/dur_max`, `count`, `bbox_union[4]` (min/max lat/lon over the
  subtree) and either a leaf range `[begin,end)` into a flat span array or two
  child ids. Splits use the dimension with the larger relative spread
  (`(max-min)/global_range`) at the median, so the ~6e7 s timestamp range does
  not starve the duration axis. 131,072 leaves, 262,144 nodes, full binary tree.
- Query rectangle: `ts in [ws, we-dur_q+tol]`, `dur in [dur_q-tol, dur_q+tol]`;
  prune when node ranges are disjoint, the node bbox does not intersect the
  query bbox, or `count == 0`; leaves emit all span ids in their range.
- Both modes share the same code path after candidate collection: identical
  exact hard filters (`n >= m`, duration, `ts >= ws`, `ts+dur <= we`, span bbox
  inside query bbox), identical 64-D projection banded DP, identical
  top-C=300 banded-DTW verification and ranking.

## Correctness (hard constraints)

Per-query comparison of `catalog_detail.csv` vs `est_detail.csv` (500 queries):

| check | differing queries |
|---|---|
| spans after exact filters (`mean 11,673.3`) | 0 / 500 |
| verified spans (`mean 293.506`) | 0 / 500 |
| result count `hits` | 0 / 500 |
| recall vs ground truth | 0 / 500 |

Mean recall is 0.9894 for both, identical to the pre-existing catalog result.
No failure; the tree pruned no feasible span on this workload.

## Results (3 repeats per method, alternating order)

| method | mean_us | p95_us | per-query median | per-query max | recall | spans collected | after exact filter | verified |
|---|---|---|---|---|---|---|---|---|
| catalog | 10,500.4 | 23,615.9 | 8,486 | 83,577 | 0.9894 | 54,597.3 | 11,673.3 | 293.5 |
| catalog_est | 5,648.1 | 14,735.6 | 4,348 | 70,520 | 0.9894 | 211,968 | 11,673.3 | 293.5 |

- Mean speedup 1.86x on repeat means (paired repeats 1.81x–1.94x); p95 1.60x;
  median 1.95x.
- Per-query medians/max are taken from the last repeat's detail CSV.
- EST visits 9,633.5 nodes/query (3.68% of 262,144) and 4,471.5 leaves/query
  (3.41% of 131,072).
- Spans collected by EST (211,968) are ~3.9x the catalog's duration slice
  (54,597.3), because a visited leaf emits all ~47 of its spans and only 5.5%
  of them survive the exact filter. The win therefore comes from replacing
  duration-slice + time-bucket/grid/city enumeration and hash lookups with a
  branch-predictable node walk, not from higher candidate selectivity.

## Build time and memory

- Span materialization (shared): 0.77–0.83 s.
- EST build: 2.77–2.89 s (median split + reordered flat span array).
- Index bytes (program-reported): EST 315,004,432 B = spans 298,227,216 B
  (48 B/span) + 16,777,216 B nodes (64 B x 262,144). Flat catalog:
  298,227,216 B spans + 124,261,340 B of auxiliary span-id arrays
  (`dur_order`, `dur_sorted`, time buckets, space/time/duration grid,
  `by_city`; 5 x 24,852,268 B) = 422,488,556 B, excluding hash-table overhead.
- Peak RSS: catalog 1,904,284 KiB (1.82 GiB); EST 2,075,660 KiB (1.98 GiB).
  EST's peak is higher only because the construction-time span array and the
  tree copy coexist during build; the construction array is freed afterwards.
  Points (1.84 M x 536 B) plus the 64-D projection buffer dominate RSS in both.

## SUMMARY lines (verbatim)

```
SUMMARY catalog n=500 recall=0.9894 mean_us=11022.2 p95_us=24535.5 qps=0.0907262 mean_cand=54597.3 mean_dtw=293.506 empty=0
CATALOG spans=6213067 build_s=0.834674 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=293.506 topc=300 cap_h=48
SUMMARY catalog n=500 recall=0.9894 mean_us=10170.7 p95_us=22542 qps=0.0983215 mean_cand=54597.3 mean_dtw=293.506 empty=0
CATALOG spans=6213067 build_s=0.769505 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=293.506 topc=300 cap_h=48
SUMMARY catalog n=500 recall=0.9894 mean_us=10308.3 p95_us=23770.2 qps=0.0970097 mean_cand=54597.3 mean_dtw=293.506 empty=0
CATALOG spans=6213067 build_s=0.781467 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=293.506 topc=300 cap_h=48
SUMMARY catalog_est n=500 recall=0.9894 mean_us=5669.25 p95_us=14841.9 qps=0.17639 mean_cand=211968 mean_dtw=293.506 empty=0
EST spans=6213067 build_s=0.805249 bytes=2.98227e+08 mean_slice=211968 mean_filter=11673.3 mean_verify=293.506 topc=300 cap_h=48 est_nodes=262144 est_leaves=131072 est_leaf_size=64 est_build_s=2.8929 est_bytes=3.15004e+08 mean_nodes=9633.47 mean_leaves=4471.51
SUMMARY catalog_est n=500 recall=0.9894 mean_us=5632.49 p95_us=14447.7 qps=0.177542 mean_cand=211968 mean_dtw=293.506 empty=0
EST spans=6213067 build_s=0.824182 bytes=2.98227e+08 mean_slice=211968 mean_filter=11673.3 mean_verify=293.506 topc=300 cap_h=48 est_nodes=262144 est_leaves=131072 est_leaf_size=64 est_build_s=2.88957 est_bytes=3.15004e+08 mean_nodes=9633.47 mean_leaves=4471.51
SUMMARY catalog_est n=500 recall=0.9894 mean_us=5642.67 p95_us=14917.3 qps=0.177221 mean_cand=211968 mean_dtw=293.506 empty=0
EST spans=6213067 build_s=0.809112 bytes=2.98227e+08 mean_slice=211968 mean_filter=11673.3 mean_verify=293.506 topc=300 cap_h=48 est_nodes=262144 est_leaves=131072 est_leaf_size=64 est_build_s=2.76855 est_bytes=3.15004e+08 mean_nodes=9633.47 mean_leaves=4471.51
```

(`mean_cand` = spans collected per query, `mean_filter` = spans after exact
filters, `mean_dtw`/`mean_verify` = spans verified with banded DTW.)

## Repro

```
cd production/semantic-mobility-search
./work/bench_est --dir work/all6_8760h_s50_potion --out work/bench_out_est \
  --mode catalog  --topc 300 --cap-hours 48
./work/bench_est --dir work/all6_8760h_s50_potion --out work/bench_out_est \
  --mode catalog-est --topc 300 --cap-hours 48
```

`work/bench_out_est/groundtruth.csv` is a copy of the potion ground truth;
both modes read it for recall.

## Artifacts

- `summary.csv` — one row per repeat: timings, recall, per-query candidate/filter
  counts, node/leaf visits, build times, index bytes, peak RSS.
- `catalog_detail.csv`, `est_detail.csv` — per-query `time_us`,
  `nodes_visited`, `leaves_visited`, `spans_collected`, `spans_after_filter`,
  `verified`, `hits`, `recall`.
- Raw logs: `production/semantic-mobility-search/work/bench_out_est/*_rep*.log`
  and `*.time`.

## Caveats

- Recall 0.9894 is the pre-existing top-C=300 sketch-ranking loss; EST does not
  change it (per-query recall identical), it only changes candidate generation.
- The `ts` bound uses `dur_q`, not each span's own duration, so it is a
  superset; leaf emission is likewise block-granular. Both are covered by the
  unchanged per-span exact filter, verified by the 500/500 feasible-count match.
- Query timings are serial; both modes run in separate processes.
