# Similarity-join paper reproductions

This project contains a paper-derived implementation of Xie, Yu, and Liu,
**Fast Approximate Similarity Join in Vector Databases** (PACMMOD/SIGMOD 2025,
DOI `10.1145/3725403`). It follows published Algorithms 2--6 but is not the
authors' artifact: no author implementation or complete parameter manifest was
found. Read [the reproduction contract](docs/simjoin-reproduction-plan.md)
before interpreting results.

## What is implemented

- Algorithm 3 JoinSlide with explicit sliding bound `w` and exact L2 checks.
- Algorithms 4--5 ordering variants: BaseMST, MST + WindowOrder-Sort, and a
  WindowOrder-epsilon using an explicitly labelled, locally chosen approximate
  JoinSlide root search (the paper does not specify its complete search policy).
- Algorithm 6 kJoinSlide with explicit `k` and `w`.
- Average recall, average precision, NDC, two interpretations of nSelectivity,
  graph/order/join timing, raw parameter provenance, and optional pair output.
- HNSW graph construction for the paper's graph-comparison experiment, plus a
  generic adjacency-file adapter for externally built NSG/Vamana graphs.
- A process-wide Linux address-space ceiling of 2,000,000,000 bytes by default.

The HNSW path is always reported as
`paper-derived-graph-backend-deviation`, because the paper uses NSG by default.
An imported graph is reported as external and does not become a verified NSG or
Vamana result merely because it is given that label.

VBASE, XJoin, FGF-Hilbert, NSG/Vamana builders, and FreshDiskANN are not silently
reimplemented. Their experiments remain blocked until a verifiable artifact
revision and the paper's missing build/search parameters are supplied. Public
MSVBASE and DiskANN repositories were identified, but they are not assumed to
be the exact revisions/configurations used by the SimJoin paper.

## Build

```bash
cmake -S codes/vector-join-reproduction \
      -B codes/vector-join-reproduction/build \
      -DCMAKE_BUILD_TYPE=Release \
      -DVJ_FETCH_HNSWLIB=ON \
      -DVJ_FETCH_PYBIND11=ON
cmake --build codes/vector-join-reproduction/build
ctest --test-dir codes/vector-join-reproduction/build --output-on-failure
```

The Python runners only parse arguments and call the C++ pybind11 module. Put
the build directory on `PYTHONPATH`; no runner mutates `sys.path`.

## Dataset input

Datasets are not allow-listed. `run_simjoin.py` and `run_kjoinslide.py` each
accept any row-major float32 matrix with an explicit dimension. For an ordinary join, provide separate X
(`--queries`) and Y (`--base`) files. For a self join, pass `--self-join` and
only Y. Record the dataset name, preprocessing and split in every run.

To reproduce the paper's common split, randomly partition the original vectors
into equal halves and record the seed; use one half for self join and the two
halves for ordinary join. The paper does not disclose that seed or whether
vectors were normalized, so neither choice is inferred by this code. Other
datasets and existing base/query splits are valid, but the recorded split and
preprocessing make their deviation explicit.

Example of a small HNSW-backed validation run (not the paper-default NSG run):

```bash
PYTHONPATH=codes/vector-join-reproduction/build \
python3 codes/vector-join-reproduction/scripts/run_simjoin.py \
  --base /data/Y.f32 --queries /data/X.f32 --dim 128 \
  --epsilon 0.4 --w 64 \
  --order window-order-sort \
  --graph-source hnswlib --graph-m 20 --ef-construction 200 --seed 1234 \
  --dataset-name my-data --preprocessing none \
  --split-description 'user-provided X/Y split' \
  --ground-truth-limit 100 --repeat 5 \
  --output experiment-results/my-data/simjoin.jsonl
```

`w`, HNSW parameters, and the seed above are examples, not paper settings. The
paper does not report their concrete values. Use `run_kjoinslide.py` with
`--k 10 --w ...` for k-join; it has a separate entry point and does not expose
an epsilon mode.

For an external graph, use `--graph-source external`, `--base-graph`, and (for
ordinary joins) `--query-graph`. The UTF-8 adjacency format has one node per
line: the source ID followed by zero or more neighbor IDs separated by spaces.
IDs are zero-based. Also provide `--graph-label`, `--graph-provenance`, entry
IDs, and `--ef-construction 0`. Provenance should contain the repository URL,
immutable commit, build flags, and every graph parameter.

## Server limit and execution status

This host must not run experiments exceeding 2 GB. All entry points enforce a
hard address-space limit of 2,000,000,000 bytes, preserving stricter inherited
limits. Planning estimates are not measured memory: epsilon output size depends
on the data, and the OS ceiling governs actual allocations. Reported peak RSS is
process-lifetime, not an isolated per-repetition peak. Full paper-scale workloads
remain `not-run/resource-policy` here. Unit and tiny synthetic executions validate
correctness and plumbing only; they are never reported as paper performance.

See [remote experiment handoff](docs/remote-experiments.md) for commands, metric
boundaries, required return files, and the distinction between validation and
paper reproduction. Results are never overwritten. Missing quality evaluation
is represented by `null`, and full nSelectivity requires full ground truth.

## Other exploratory code

The `independent`, `hard`, `soft`, `mi`, and `mi-adapt` C++ API is retained for
the separate Fast AVJ exploration. Its `hard` branch is now named
`run_hard_window_cache` in the binding and source to prevent it from being
misreported as SimJoin. Those methods use squared L2 and are not part of this
paper reproduction.
