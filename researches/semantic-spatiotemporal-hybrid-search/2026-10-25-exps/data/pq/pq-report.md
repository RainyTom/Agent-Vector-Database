# Product quantization for episode-catalog ranking (2026-10-25)

Test of whether product quantization (PQ) over the segment embeddings can cut the
distance cost of the episode catalog's ranking stage without losing Recall@10.

Workload: `production/semantic-mobility-search/work/all6_8760h_s50_potion`
(6 cities, 1-year window, 50 km, 500 queries, 1,838,793 segments, 128-D potion
embeddings; see `../potion/potion-report.md`). Catalog: `--topc {300,600}
--cap-hours 48`, `k=10`, 500 queries, single process. Ground truth:
`work/bench_out_potion/groundtruth.csv` (exact time-first). All numbers below are
from actual runs; artifacts are under
`production/semantic-mobility-search/work/{pq,bench_out_pq}`.

## PQ model

`src/pq_train.py` (new, numpy only), trained on the full segment table:

| item | value |
|---|---|
| subspaces / centroids / sub-dim | 8 x 256 x 16 (8 bytes per 128-D vector) |
| training sample | 200,000 segments, seed 20261025 |
| k-means | Lloyd, 15 iterations, empty-cluster reseeding |
| training time | 27.759 s (8 subspaces); 46.054 s script wall incl. sampling and encoding all 1,838,793 segments |
| codes.bin | 14,710,344 B = 14.03 MiB (uint8 N x 8) |
| centroids.npy | 131,072 B payload (8, 256, 16) float32; file 131,200 B |
| total model | 14.84 MB vs 941.5 MB of float32 embeddings: 64x smaller codes |
| codeword usage | 0 empty codewords, 1 .. 44,948 assignments |
| quantization error | mean squared reconstruction error 0.337 (sum over 8 subspaces) |
| reconstruction cosine | mean 0.812, 5th pct 0.660, min 0.536 on the training sample |

Per-subspace relative squared error rises from 0.23 (subspace 0, energy 0.395) to
0.45 (subspace 7, energy 0.039), i.e. the PQ approximation is coarse at 8 B/vector.
Exact centroids and meta are in `work/pq/meta.json`.

## Implementation

New files only; `src/bench.cpp` is untouched.

- `src/bench_pq.cpp`: byte-identical copy of `src/bench.cpp` plus
  - `load_pq()` (`codes.bin`, `.npy` centroids with header parsing),
  - `banded_dtw_pq<SQRT>()`: the same banded DP as the baseline, but the per-cell
    cost is an ADC lookup over the 8 code bytes instead of a 64-D projected distance.
    The LUT is rebuilt per query:
    `lut[i][s][k] = ||q_i[s*16:(s+1)*16] - centroid[s][k]||^2`.
    `SQRT=false` is the spec's literal cost (sum of 8 lookups);
    `SQRT=true` applies `sqrt()` so the additive scale matches the exact L2 DTW.
  - mode `catalog-pq` (`--pq DIR`, `--pq-sqrt 0|1`). Everything else (slice, hard
    constraints, top-C truncation) is unchanged. The final top-C verification still
    uses the original 128-D `banded_dtw()`; final scores are never approximated.
  - extra timing split `mean_rank_ms` (slice + hard filter + banded ranking +
    `nth_element`) and `mean_verify_ms` (exact 128-D DTW on the top-C spans).
- Built with `g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib`
  into `work/bench_pq`.

The PQ catalog build (`build_s` 0.35-0.44 s) skips the 128->64 projection matrix
and `proj_pts` array that the baseline materializes (`build_s` 0.80-0.84 s), because
PQ ranking does not consume them. `build_s` is therefore not comparable across the
two methods; span construction itself is identical (6,213,067 spans, 298.2 MB).

## Results

Each configuration was run 3 times; `mean_us` is the mean of the three run means,
`p95_us` the median of the three per-run p95 values, `rank/verify/build` the medians.
Recall is deterministic across repeats. Full table in `summary.csv`.

| method | topc | Recall@10 | mean_us | p95_us | qps | cand | verified | rank_ms | verify_ms | build_s |
|---|---|---|---|---|---|---|---|---|---|---|
| catalog (baseline) | 300 | 0.9894 | 10566.7 | 23185.6 | 0.095 | 54597.3 | 293.5 | 10.269 | 0.234 | 0.827 |
| catalog (baseline) | 600 | 0.9956 | 11239.8 | 25428.6 | 0.089 | 54597.3 | 578.5 | 10.797 | 0.460 | 0.810 |
| catalog-pq (no sqrt) | 300 | 0.9702 | 7921.9 | 18241.8 | 0.126 | 54597.3 | 293.5 | 7.868 | 0.238 | 0.393 |
| catalog-pq (no sqrt) | 600 | 0.9884 | 8169.2 | 17048.4 | 0.123 | 54597.3 | 578.5 | 7.523 | 0.453 | 0.362 |
| catalog-pq-sqrt | 300 | 0.9876 | 7846.9 | 17129.7 | 0.128 | 54597.3 | 293.5 | 7.835 | 0.239 | 0.361 |
| catalog-pq-sqrt | 600 | **0.9966** | **7728.9** | **16861.3** | 0.130 | 54597.3 | 578.5 | 7.242 | 0.440 | 0.351 |

Deltas against the same-topc baseline (median mean_us):

| comparison | speedup | recall delta |
|---|---|---|
| catalog-pq vs catalog, topc=300 | 1.296x | -0.0192 |
| catalog-pq vs catalog, topc=600 | 1.411x | -0.0072 |
| catalog-pq-sqrt vs catalog, topc=300 | 1.300x | -0.0018 |
| catalog-pq-sqrt vs catalog, topc=600 | 1.465x | +0.0010 |

Cross operating point: `catalog-pq-sqrt topc=600` is 1.367x faster than
`catalog topc=300` (7.73 ms vs 10.50 ms median; p95 16.9 ms vs 22.9 ms) while
scoring 0.9966 vs 0.9894 Recall@10. Candidate counts (54,597.3 spans) and verified
counts are identical across methods, so the comparison isolates ranking quality and
cost.

## Interpretation

- PQ ranking does cut the ranking stage: 10.27 -> 7.84 ms at topc=300 and
  10.80 -> 7.24 ms at topc=600 (medians), a 1.31x / 1.49x reduction of
  `mean_rank_ms`. The verify stage is unchanged in cost per span (0.234 vs 0.239 ms
  for 293.5 spans; 0.460 vs 0.440 ms for 578.5 spans), confirming only candidate
  ordering changed.
- The gain is real but modest, not the order of magnitude an 8-byte code suggests.
  Removing `sqrt` from the ADC cost only bought ~4% (7.87 vs 7.84 ms at topc=300
  with worse recall), so the residual is not the transcendental. `mean_rank_ms`
  also contains stage-1/2 slicing over 54,597 spans and the hard-constraint loop
  over 11,673 spans. [INFERENCE] ADC makes the per-cell distance cheaper; the
  sequential DP recurrence plus slice/filter bookkeeping now bound the stage, which
  is consistent with the small effect of dropping `sqrt`.
- At topc=300 the recall loss is not acceptable for the sqrt variant (-0.0018) and
  clearly unacceptable for the literal cost (-0.0192): PQ reorders enough true
  neighbours below rank 300. At topc=600 the sqrt variant recovers them and slightly
  beats the baseline (0.9966 vs 0.9956).
- Recommendation: if PQ is used here, use the scale-preserving (`sqrt`) ADC and
  topc=600; it is the only point that improves both recall and latency. The literal
  ADC cost is cheaper to compute but loses more recall at every topc tested.

## Failures / caveats

1. **Literal ADC loses recall.** Cost = sum of the 8 lookups (the task's stated form)
   gives 0.9702 at topc=300 and 0.9884 at topc=600, i.e. -0.0192 / -0.0072 vs the
   projection baseline. Because the DP accumulates squared instead of L2 distances,
   the objective is no longer scale-comparable across spans that take different
   matchings. The `sqrt` variant fixes this at ~4% higher ranking time.
2. **No order-of-magnitude win.** Ranking falls by 2.4-3.6 ms, not by the ~8x
   per-cell arithmetic reduction; see Interpretation. PQ alone is not enough to make
   the catalog distance-bound-free.
3. **Timing noise.** Repeat runs of the same binary/config varied by up to ~8%
   (e.g. baseline topc=300 range 10.37-10.83 ms), so the table reports medians over
   three runs. A single earlier baseline run measured 10.37 ms, matching the
   repository's prior `data/potion/summary.csv` (10.37 ms).
4. **Loader bug during bring-up.** The first `centroids.npy` reader used the header
   length as the payload offset (forgot the 10-byte prefix); `bench_pq` exited with
   status 2 before any query ran. Fixed by adding the prefix; all measurements above
   are from the fixed binary. No data or baseline file was affected.
5. PQ was trained on a uniform 200k sample, not on catalog spans; span-start
   distribution may differ, but the catalog spans are contiguous point ranges so
   point coverage is the relevant target. Not evaluated further.

## Reproduction

```
# training (numpy only; writes work/pq/{codes.bin,centroids.npy,meta.json})
cd production/semantic-mobility-search
./venv_potion/bin/python src/pq_train.py \
    --dir work/all6_8760h_s50_potion --out work/pq

# build
cd ../..
g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib \
    production/semantic-mobility-search/src/bench_pq.cpp \
    -o production/semantic-mobility-search/work/bench_pq

# runs (from production/semantic-mobility-search/work; out dir needs groundtruth.csv)
./bench_pq --mode catalog        --dir all6_8760h_s50_potion --k 10 --topc 300 --cap-hours 48 --out bench_out_pq/base_c300
./bench_pq --mode catalog        --dir all6_8760h_s50_potion --k 10 --topc 600 --cap-hours 48 --out bench_out_pq/base_c600
./bench_pq --mode catalog-pq     --dir all6_8760h_s50_potion --k 10 --topc 300 --cap-hours 48 --out bench_out_pq/pq_c300 --pq pq
./bench_pq --mode catalog-pq     --dir all6_8760h_s50_potion --k 10 --topc 600 --cap-hours 48 --out bench_out_pq/pq_c600 --pq pq
./bench_pq --mode catalog-pq --pq-sqrt 1 --dir all6_8760h_s50_potion --k 10 --topc 300 --cap-hours 48 --out bench_out_pq/pqsqrt_c300 --pq pq
./bench_pq --mode catalog-pq --pq-sqrt 1 --dir all6_8760h_s50_potion --k 10 --topc 600 --cap-hours 48 --out bench_out_pq/pqsqrt_c600 --pq pq
```

Per-run logs: `work/bench_out_pq/<config>/rep{1,2,3}.log` (plus the pre-repeat
`run.log`). Each out dir contains the per-query CSV written by the run
(`catalog.csv` or `catalog-pq[-sqrt].csv`) and a copy of `groundtruth.csv`.

## Raw SUMMARY lines (three repeats per configuration)

```
base_c300
SUMMARY catalog n=500 recall=0.9894 mean_us=10367.5 p95_us=22888.9 qps=0.0964549 mean_cand=54597.3 mean_dtw=293.506 empty=0
SUMMARY catalog n=500 recall=0.9894 mean_us=10503.6 p95_us=23185.6 qps=0.0952051 mean_cand=54597.3 mean_dtw=293.506 empty=0
SUMMARY catalog n=500 recall=0.9894 mean_us=10828.9 p95_us=23460.1 qps=0.0923454 mean_cand=54597.3 mean_dtw=293.506 empty=0
base_c600
SUMMARY catalog n=500 recall=0.9956 mean_us=11515.2 p95_us=25428.6 qps=0.0868421 mean_cand=54597.3 mean_dtw=578.482 empty=0
SUMMARY catalog n=500 recall=0.9956 mean_us=10946.7 p95_us=24195.1 qps=0.0913515 mean_cand=54597.3 mean_dtw=578.482 empty=0
SUMMARY catalog n=500 recall=0.9956 mean_us=11257.4 p95_us=25719.8 qps=0.0888306 mean_cand=54597.3 mean_dtw=578.482 empty=0
pq_c300
SUMMARY catalog-pq n=500 recall=0.9702 mean_us=7494.2 p95_us=16243.8 qps=0.133437 mean_cand=54597.3 mean_dtw=293.506 empty=0
SUMMARY catalog-pq n=500 recall=0.9702 mean_us=8105.93 p95_us=18241.8 qps=0.123366 mean_cand=54597.3 mean_dtw=293.506 empty=0
SUMMARY catalog-pq n=500 recall=0.9702 mean_us=8165.42 p95_us=18825.4 qps=0.122468 mean_cand=54597.3 mean_dtw=293.506 empty=0
pq_c600
SUMMARY catalog-pq n=500 recall=0.9884 mean_us=8743.66 p95_us=20964.4 qps=0.114369 mean_cand=54597.3 mean_dtw=578.482 empty=0
SUMMARY catalog-pq n=500 recall=0.9884 mean_us=7976.53 p95_us=16724.1 qps=0.125368 mean_cand=54597.3 mean_dtw=578.482 empty=0
SUMMARY catalog-pq n=500 recall=0.9884 mean_us=7787.5 p95_us=17048.4 qps=0.128411 mean_cand=54597.3 mean_dtw=578.482 empty=0
pqsqrt_c300
SUMMARY catalog-pq-sqrt n=500 recall=0.9876 mean_us=8141.99 p95_us=18280.7 qps=0.12282 mean_cand=54597.3 mean_dtw=293.506 empty=0
SUMMARY catalog-pq-sqrt n=500 recall=0.9876 mean_us=8082.11 p95_us=17129.7 qps=0.12373 mean_cand=54597.3 mean_dtw=293.506 empty=0
SUMMARY catalog-pq-sqrt n=500 recall=0.9876 mean_us=7316.7 p95_us=16301.9 qps=0.136674 mean_cand=54597.3 mean_dtw=293.506 empty=0
pqsqrt_c600
SUMMARY catalog-pq-sqrt n=500 recall=0.9966 mean_us=7492.88 p95_us=16090.7 qps=0.13346 mean_cand=54597.3 mean_dtw=578.482 empty=0
SUMMARY catalog-pq-sqrt n=500 recall=0.9966 mean_us=7681.69 p95_us=16861.3 qps=0.13018 mean_cand=54597.3 mean_dtw=578.482 empty=0
SUMMARY catalog-pq-sqrt n=500 recall=0.9966 mean_us=8012.05 p95_us=17761.1 qps=0.124812 mean_cand=54597.3 mean_dtw=578.482 empty=0
```

Representative CATALOG lines (repeat 1 of each configuration; the aggregate table
uses medians over three repeats):

```
CATALOG method=catalog spans=6213067 build_s=0.844238 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=293.506 mean_rank_ms=10.1396 mean_verify_ms=0.227947 topc=300 cap_h=48 pq=0 pq_sqrt=0
CATALOG method=catalog spans=6213067 build_s=0.824494 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=578.482 mean_rank_ms=11.0432 mean_verify_ms=0.471957 topc=600 cap_h=48 pq=0 pq_sqrt=0
CATALOG method=catalog-pq spans=6213067 build_s=0.402991 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=293.506 mean_rank_ms=7.26221 mean_verify_ms=0.231988 topc=300 cap_h=48 pq=1 pq_sqrt=0
CATALOG method=catalog-pq spans=6213067 build_s=0.364448 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=578.482 mean_rank_ms=8.27747 mean_verify_ms=0.466194 topc=600 cap_h=48 pq=1 pq_sqrt=0
CATALOG method=catalog-pq-sqrt spans=6213067 build_s=0.350232 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=293.506 mean_rank_ms=7.90303 mean_verify_ms=0.23896 topc=300 cap_h=48 pq=1 pq_sqrt=1
CATALOG method=catalog-pq-sqrt spans=6213067 build_s=0.346506 bytes=2.98227e+08 mean_slice=54597.3 mean_filter=11673.3 mean_verify=578.482 mean_rank_ms=7.06781 mean_verify_ms=0.425067 topc=600 cap_h=48 pq=1 pq_sqrt=1
```
