#!/usr/bin/env bash
set -euo pipefail
# Run from repository root; fixed offline workload, no changes to autoresearch.sh.
# Historical three-arm result-filter-reference reproduction only.
# Current qualification-first baseline: run_qualification_first.sh or bash autoresearch.sh.
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
hnsw=codes/directory-aware-vector-attr-search/deps/hnswlib
experiment=researches/nested-attr-vector-search/2026-11-02-exps
work=work/query-cost-profile
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
test "$(git -C "$hnsw" rev-parse HEAD)" = ca426729609b3221563047ceb269cc1213e0d376
mkdir -p "$work" "$experiment/data"
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$experiment/data/query-cost-isolated-input-sha256.txt"
g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/directory-aware-vector-attr-search/src -I"$hnsw" \
  "$experiment/experiments/query_cost_profile.cpp" -o "$work/query-cost-profile"
"$work/query-cost-profile" "$base" "$queries" "$dag" \
  "$experiment/data/query-cost-profile-isolated.csv" "$experiment/data/query-cost-profile-isolated-meta.json" \
  | tee "$work/run.log"
