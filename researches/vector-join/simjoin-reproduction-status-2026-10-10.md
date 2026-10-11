# SimJoin reproduction status — 2026-10-10

> Historical report, superseded by the reproduction audit and subsequent fixes.
> The times below came from a Debug build and exclude online order selection.
> The 640,000-byte figure is an obsolete allocation estimate, not peak RSS.
> The old selectivity used approximate output cardinality and must not be reused.
> The exact-filter ordering variant has been replaced; these records do not
> describe the corrected code. See the current reproduction contract and
> `codes/vector-join-reproduction/docs/remote-experiments.md` for valid execution.

This report deliberately separates paper statements, executions on this host,
and deviations. The experimental contract and the evidence extracted from the
paper are in
[`simjoin-reproduction-plan.md`](../../codes/vector-join-reproduction/docs/simjoin-reproduction-plan.md).

## Implemented

- Paper-derived JoinSlide (Algorithm 3), with explicit `w` and exact Euclidean
  L2 verification.
- BaseMST, MST + WindowOrder-Sort, and a conservatively named
  `WindowOrder-epsilon-exact-filter` ordering path. The latter name records
  that an exact root-edge filter is used because the paper does not provide
  enough implementation detail to reproduce its approximate filter exactly.
- Paper-derived kJoinSlide (Algorithm 6), with explicit `k` and `w`.
- Separate pybind-backed command-line entries: `run_simjoin.py` and
  `run_kjoinslide.py`.
- Generic row-major float32 data loading and external adjacency-graph loading;
  data names are not allow-listed.
- Average per-query recall and precision, NDC, join result count, both possible
  interpretations of nSelectivity, graph/offline-order/online-order/join times,
  and complete user-supplied parameter/provenance records.
- A conservative preflight memory estimate plus an operating-system 2 GiB
  address-space ceiling. A run whose estimate reaches 2 GiB is rejected.

This is a clean-room implementation from published pseudocode. It is not the
authors' artifact and is never labelled as such.

## Actually executed on this host

Only a small pipeline validation was run. Input was two independently generated
uniform `[0,1)` matrices, each 200 x 16 float32, seed 1234. The graph was built
with `hnswlib`, `M=16`, `ef_construction=100`; `w=32`; all 200 queries received
exact ground truth; each case was repeated five times. The estimated peak was
640,000 bytes, far below 2 GiB.

| Run | Mean join time (ms) | Average recall | Average precision |
|---|---:|---:|---:|
| epsilon=1.2, BaseMST | 5.730 | 0.885579 | 1.0 |
| epsilon=1.2, MST + WindowOrder-Sort | 6.007 | 0.885579 | 1.0 |
| epsilon=1.2, WindowOrder-epsilon exact filter | 5.664 | 0.885579 | 1.0 |
| k=10, MST + WindowOrder-Sort | 34.029 | 1.0 | 1.0 |

Raw five-run JSONL records are under
`codes/vector-join-reproduction/experiment-results/simjoin-validation/`.
These values are **not paper performance results**: the data, scale, hardware,
graph backend and compiler standard differ from the paper.

## Deviations in the validation run

| Item | Paper | This validation |
|---|---|---|
| Dataset/scale | eight named datasets at their reported scales | synthetic 200 x 16 matrices |
| Default graph | NSG | hnswlib HNSW |
| Language/toolchain | C++11, g++ 11.4.1 `-O3` | C++20, local compiler; recorded per run |
| Hardware | CentOS, Xeon Silver 4215, 128 GiB | current host; no absolute-time equivalence claimed |
| Parameters | several graph and search values unreported | explicit local values above |
| Resource policy | paper-scale host | strict local ceiling below 2 GiB |

## Paper experiment completion matrix

| Experiment | Status on this host | Reason |
|---|---|---|
| 1 parameter sensitivity | not run | exact `w`/beam/`tau` grids and verified baselines unavailable |
| 2 epsilon/self-join comparison | not run | paper data plus verified baseline revisions/operating points unavailable |
| 3 epsilon/selectivity at recall 0.99 | not run | Crawl/Higgs workload exceeds the local policy; tuning details incomplete |
| 4 NDC-recall | not run | same data/baseline blockers |
| 5 base/query join | not run | paper's SIFT/GIST subset details are not reported; scale may violate policy |
| 6 scalability | not run | full 500--500,000 matrix would include runs violating the conservative limit |
| 7 NSG/HNSW/Vamana | not run | graph build parameters and verified revisions are unavailable |
| 8 ordering microbenchmark | pipeline only | algorithms execute, but no paper data/graph and no 100-repeat paper run |
| 9 k=10 join | pipeline only | k=10 verified in the paper; paper data/VBASE configuration unavailable |
| 10 index maintenance | not implemented/run | verified FreshDiskANN revision/settings and graph-maintenance details unavailable |

No missing case is represented as a zero, omitted bar, or completed result.

## Baseline and graph provenance status

Public MSVBASE, DiskANN/FreshDiskANN and NSG repositories were found, but the
paper does not identify immutable commits and complete native parameter sets.
No verified paper-specific XJoin or FGF-Hilbert artifact was found. Therefore:

- no local substitute is reported as VBASE, XJoin, FGF-Hilbert or
  FreshDiskANN;
- a graph loaded from a file is reported as
  `external-user-declared/<label>` and `paper-derived-external-graph-unverified`;
- an HNSW run is reported as `paper-derived-graph-backend-deviation`;
- full experiments remain blocked until artifacts/data/settings fit within the
  local resource policy.

## Paper-reported values versus local values

Exact values printed in paper tables are recorded below. Approximate
coordinates from plotted figures are not transcribed as exact values. No local
paper-scale measurement exists, so these must not be treated as direct
hardware-normalized comparisons.

Paper Table 3 reports the following for **SimJoin** at epsilon=0.4; times are
seconds. This is paper evidence, not a local execution:

| Dataset | Self recall | Self time | Join recall | Join time | Join pre-ratio |
|---|---:|---:|---:|---:|---:|
| Rand | 1 | 1.8626 | 0.99298 | 6.3837 | 0.00623% |
| Gauss | 1 | 1.1332 | 1.0000 | 1.0223 | 0.03870% |
| Msong | 0.99368 | 135.89 | 0.99052 | 178.20 | 0.00066% |
| GIST | 0.99091 | 120.62 | 0.99256 | 355.38 | 0.00032% |
| GloVe | 1 | 18.158 | 0.99788 | 15.203 | 0.01474% |
| Crawl | 0.99998 | 47.114 | 0.99084 | 202.72 | 0.00201% |
| SIFT | 0.99564 | 5232.8 | 0.99344 | 5532.6 | 0.00023% |
| Higgs | 0.99014 | 2044.6 | 0.99067 | 2020.3 | 0.00109% |

Paper Table 4 reports the following maintenance peak memory and time. The paper
text introduces Exp. 10 as Crawl/Higgs, while Table 4 contains all eight
datasets; this inconsistency is retained rather than silently resolved.

| Dataset | Rebuild peak/time | FreshDiskANN peak/time | k-join peak/time |
|---|---|---|---|
| Rand | 430.72 MB / 129.624 s | 274.64 MB / 37.3372 s | 249.95 MB / 33.3368 s |
| Gauss | 419.21 MB / 131.991 s | 166.51 MB / 29.9972 s | 167.75 MB / 29.2273 s |
| Msong | 3.9465 GB / 2632.23 s | 1.6452 GB / 343.617 s | 1.6432 GB / 327.254 s |
| GIST | 9.0220 GB / 6632.01 s | 5.5203 GB / 960.500 s | 5.5196 GB / 906.157 s |
| GloVe | 4.2359 GB / 958.089 s | 1.1092 GB / 875.349 s | 1.0849 GB / 864.672 s |
| Crawl | 7.3334 GB / 1963.67 s | 2.4671 GB / 674.069 s | 2.4202 GB / 585.061 s |
| SIFT | 17.257 GB / 3269.74 s | 8.1037 GB / 1989.38 s | 8.0456 GB / 1825.94 s |
| Higgs | 22.394 GB / 2961.66 s | 4.7927 GB / 769.701 s | 4.7922 GB / 744.140 s |

The five-run local validation table earlier in this report is the only local
numeric result set. Its absolute times are not compared with either paper
table.
