# SimJoin / Fast AVJ reproduction

This project reproduces the central mechanisms of:

- Xie, Yu, Liu, **Fast Approximate Similarity Join in Vector Databases** (PACMMOD 2025, DOI `10.1145/3725403`).
- Kim et al., **Fast Approximate Vector Joins via Offline-Online Co-Design** (arXiv `2603.16360v2`, 2026).

It is a clean-room research implementation over the public hnswlib graph substrate. It is not an author artifact and must not be used to claim exact reproduction of reported paper numbers.

## Implemented methods

| CLI method | Paper role | Mechanism |
|---|---|---|
| `independent` | INLJ baseline | Fixed-entry greedy graph search followed by in-range expansion |
| `hard` | SimJoin / HWS | HNSW query graph, approximate MST order, reuse of the parent's final join window |
| `soft` | Fast AVJ / SWS | Same order, but cache only the closest visited data point, including out-of-range points |
| `mi` | Fast AVJ / MI | Joint query–data HNSW graph; query-node neighborhood serves as the offline cross-set entry map |
| `mi-adapt` | Fast AVJ / MI+Adapt | Merged index plus OOD-triggered traversal through bounded out-of-range bridges |

All emitted pairs are verified with exact squared-L2 distance, so precision should be 1. Recall remains approximate because the proximity graph can disconnect an in-range region.

## Dependencies

- CMake 3.20+
- C++20 compiler
- Ninja (optional)
- [hnswlib](https://github.com/nmslib/hnswlib), pinned for FetchContent to commit `ca426729609b3221563047ceb269cc1213e0d376`

The local research checkout is expected at `codes/upstream/hnswlib` and is ignored by Git. Alternatively configure with `-DVJ_FETCH_HNSWLIB=ON`.

```bash
cmake -S codes/vector-join-reproduction \
      -B codes/vector-join-reproduction/build \
      -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build codes/vector-join-reproduction/build
ctest --test-dir codes/vector-join-reproduction/build --output-on-failure
```

## Run on the local SIFT data

The repository's SIFT metadata defines distance as **squared L2**; thresholds passed to this program use the same unit.

```bash
codes/vector-join-reproduction/build/vector_join_bench \
  --base datasets/sift25k/base.f32 \
  --queries datasets/sift25k/queries.f32 \
  --dim 128 --threshold 90000 \
  --base-limit 25000 --query-limit 1000 \
  --ground-truth-limit 100 --method hard
```

Or run all five variants:

```bash
QUERY_LIMIT=1000 GROUND_TRUTH_LIMIT=100 \
  codes/vector-join-reproduction/scripts/run_sift25k.sh
```

The executable writes one JSON object containing separate offline build, ordering and online join time; greedy/expansion distance computations; pair counts; macro recall, global pair recall and precision.

For intuition, squared-L2 `90000` corresponds to an L2 radius of `300`. The ANN-Benchmarks distance samples stored in `sift1m/meta.json` are displayed in the unsquared unit even though hnswlib's `L2Space` and this implementation compute squared L2.

## Faithfulness and known deviations

- The papers use NSG as their primary backend and also evaluate HNSW. This reproduction uses HNSW because it has a small auditable public implementation.
- SimJoin's exact code was not public. The 2026 paper also states that it reimplemented SimJoin and added visited-node tracking. This implementation likewise computes each query–node distance once per search.
- The query order is a minimum spanning **forest** over the undirected level-0 HNSW query graph. Disconnected components receive independent roots.
- `mi` uses the merged HNSW neighborhood directly. It captures the paper's encoder-centric merged-index mechanism but does not reproduce the NSG-specific RNG behavior.
- The OOD feature implements the paper's $d_1/d_2>1.5$ rule. The exact local neighborhood differs with the HNSW backend.
- Index build time is reported separately and is never hidden inside online latency.

## BriskFlow relationship

BriskFlow commit `89d4aa5b387fc603346a7ce3f349fc9cc393ce26` was audited as a potential runtime. Its current `HNSWJoinMethod` performs independent per-record `query_for_join`, so it is an appropriate future INLJ integration point but does not yet expose cross-query batch order or search-state caches. See [BriskFlow integration notes](docs/briskflow-integration.md).
