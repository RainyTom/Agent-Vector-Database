# SimJoin experiment reproduction contract

This document separates settings stated by Xie, Yu, and Liu, *Fast
Approximate Similarity Join in Vector Databases* (PACMMOD/SIGMOD 2025,
DOI 10.1145/3725403), from settings that the paper does not disclose.  A run
must not be labelled `paper-reproduction` while any required item is marked
unverified.

## Evidence status

The paper PDF and its Algorithms 2--6 are the primary source.  The author
artifact could not be located through the paper, DOI record, author page, or a
web/GitHub search on 2026-10-10.  Public implementations were located for
MSVBASE and DiskANN/FreshDiskANN, but no verified SimJoin, XJoin, or
FGF-Hilbert artifact tied to the versions used in this paper was found.
Consequently, only an implementation that follows the published pseudocode may
currently be called `paper-derived`; it is not an author-artifact reproduction.

## Paper-stated common setup

| Item | Paper setting | Status |
|---|---|---|
| Distance | Euclidean L2; a pair is emitted only after exact comparison | verified |
| Split | random equal halves; self join on one half, ordinary join across halves | verified; split seed not reported |
| Threads | one for join methods; maintenance uses one for SimJoin/FreshDiskANN and all available for rebuild | verified |
| Repetitions | five, averaged; Exp.2 Table 3 pre-ratio measures order selection 100 times | verified; not an explicitly stated Exp.8 repetition count |
| Compiler/hardware | C++11, g++ 11.4.1 `-O3`; CentOS, Xeon Silver 4215 2.50 GHz, 128 GB | verified |
| Default graph | NSG for SimJoin and VBASE | verified |
| Index timing | excluded for SimJoin, VBASE, XJoin; included for FGF-Hilbert | verified |
| `w`, beam, `tau` sweep values | not enumerated in text or extractable figure labels | **unreported** |
| NSG/HNSW/Vamana construction parameters | not reported | **unreported** |
| random seed, normalization/preprocessing | not reported | **unreported** |

The paper datasets are Rand (64D, 100,000), Gauss (64D, 100,000), Msong
(420D, 992,272), GIST (960D, 1,000,000), GloVe (100D, 1,183,514), Crawl
(300D, 1,989,995), SIFT (128D, 10,000,000), and Higgs (29D, 11,000,000).
These are reference workloads, not a loader allow-list.  Any compatible dataset
may be supplied with an explicit path, format, dimension, preprocessing,
partition rule, and seed; deviations from the reference setup belong in every
result record.

## Experiment matrix

| Exp. | Purpose and inputs | Methods | Fixed / varied | Metrics and timing |
|---|---|---|---|---|
| 1 | parameter sensitivity, all eight split datasets, self and ordinary join | SimJoin, VBASE, XJoin, FGF-Hilbert | `epsilon=0.4`; vary `w`, VBASE beam, XJoin `tau`; exact grids unreported | join time vs average recall; exact precision |
| 2 | overall epsilon-join | same four | `epsilon=0.4`; selected operating points are not disclosed | join time, average recall/precision; preprocessing ratio; index timing policy above |
| 3 | threshold/selectivity sensitivity on Crawl and Higgs, self and ordinary join | SimJoin, VBASE, FGF-Hilbert; XJoin omitted because it did not reach 0.99 | epsilon shown as 0.1--0.5; tune to average recall 0.99 | join time and nSelectivity |
| 4 | work comparison on Crawl and Higgs, self and ordinary join | four join methods | `epsilon=0.4`; vary each method's quality knob | NDC vs average recall |
| 5 | ANN benchmark base/query join on SIFT and GIST | four join methods | paper does not state subset sizes or knobs | join time vs average recall |
| 6 | scalability on SIFT/GIST | SimJoin | sizes 500--500,000: self; vary base with query=500; vary query with base=500,000; recall=0.99 | join time; exact intermediate sizes unreported |
| 7 | graph sensitivity on Crawl/GIST, self and ordinary join | SimJoin over HNSW, Vamana, NSG | `epsilon=0.4`; construction settings unreported | join time vs average recall; record every graph parameter |
| 8 | window ordering on all eight datasets | BaseMST, MST + WindowOrder-Sort, WindowOrder-epsilon | graph/order settings unreported | offline MST, online order, and total online time separately; repetition count not explicitly specified for Exp.8 |
| 9 | k-similarity join on Crawl/Higgs, self and ordinary join | kJoinSlide and VBASE k-NN | `k=10`; vary `w`/beam (grid unreported) | join time vs average recall@10 |
| 10 | NSG maintenance on all eight datasets | global rebuild, FreshDiskANN, k-join maintenance | equal initial/insertion halves; delete random 10% from initial then insert same count; deletion seed and graph settings unreported | maintenance time, peak RSS, final-index QPS vs recall@10 |

## Metric definitions and unresolved ambiguity

For query `i`, recall is `|A_i intersect T_i| / |T_i|` and precision is
`|A_i intersect T_i| / |A_i|`; the paper averages per query.  Empty-set
denominators are not defined in the paper and must be recorded as a chosen
policy. This implementation defines empty-ground-truth recall as 1 only when
the returned set is also empty, and empty-result precision as 1 (vacuously no
false positives). The latter matches the paper's statement that exact distance
verification makes precision 1. NDC counts calls to the exact L2 distance
routine during search. Separate online-order and OOD counters are also recorded;
`total_online_ndc` adds all online distance work. Ground truth and index work
are excluded. `total_online_ms` includes online ordering and search (including
OOD decisions where applicable); `join_ms` is the search-stage timing only.

The prose calls nSelectivity the mean number of results per point, but prints
`|X| * |X join Y| / |X times Y|`, which simplifies to `|join|/|Y|`, not
`|join|/|X|`.  Runs therefore report both `paper_formula_nselectivity` and
`true_mean_results_per_query`; both use exact ground-truth cardinality and are
null unless all queries are evaluated. `returned_mean_results_per_query` is
reported separately and is not called selectivity. Quality is null when not
evaluated; prefix-only evaluation is explicitly labelled `prefix-sample`.

## Server safety policy

This server has a strict 2 GB (2,000,000,000 byte) ceiling. All Python bindings,
CLI paths and C++ run entry points set a hard Linux RLIMIT_AS, retaining any
stricter inherited limit. A planning estimate is also checked; it is not a
measurement or a proof of peak memory. Epsilon results are data-dependent, so
the estimate does not assume a dense Cartesian output. Actual allocations are
bounded by RLIMIT_AS. Peak RSS is measured at core return and labelled as the
process-lifetime peak (including previous repetitions). Full paper-scale
experiments must be run on another host with an explicit build-time limit;
never raise the limit on this server.

## Current implementation choices

WindowOrder-epsilon now runs JoinSlide on the query graph with radius maxT;
if it returns no candidates, it uses kJoinSlide top-1 as a connection fallback.
`--order-search-w` must be explicitly supplied for this variant. The paper
specifies approximate range search but not this search configuration, so it is
recorded as a local choice, not an author setting. The previous exact-filter
variant has been removed. Offline MST preparation runs once per join; optional
`--order-repeat 100` repeats only online ordering and reports its mean time and
per-execution NDC. Self-join skips order selection.

Fast AVJ uses its paper's strict threshold predicate; SimJoin uses <= epsilon.
Fast AVJ CLI threshold is squared L2, hence use epsilon squared for an equivalent
radius and retain the boundary-predicate difference in any comparison. C++20
and hnswlib are common implementation choices, not the SimJoin paper's C++11/NSG
environment. No cross-method performance conclusion is established by unit tests.

## Baseline naming rule

An executable may emit the names VBASE, XJoin, FGF-Hilbert, NSG, Vamana, or
FreshDiskANN only when its result record contains the verified repository URL,
immutable commit, build flags, and native parameters.  A locally similar
algorithm must use a descriptive non-paper name.  Missing baselines are marked
`blocked/missing-verifiable-artifact`, not replaced by HNSW range search or a
home-grown approximation.
