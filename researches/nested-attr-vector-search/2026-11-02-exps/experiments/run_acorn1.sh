#!/usr/bin/env bash
set -euo pipefail
# Same fixed SIFT/CCS fixtures, seeds, timing schedule, and quality gate as qualification-first.
# Only acorn1-* observations are written; historical qualification-first data are read by analysis.
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
hnsw=codes/directory-aware-vector-attr-search/deps/hnswlib
experiment=researches/nested-attr-vector-search/2026-11-02-exps
work=work/acorn1
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
test "$(git -C "$hnsw" rev-parse HEAD)" = ca426729609b3221563047ceb269cc1213e0d376
mkdir -p "$work" "$experiment/data"
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$experiment/data/acorn1-input-sha256.txt"
g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/directory-aware-vector-attr-search/src -I"$hnsw" \
  "$experiment/experiments/query_cost_profile.cpp" -o "$work/query-cost-profile"
"$work/query-cost-profile" "$base" "$queries" "$dag" \
  "$experiment/data/acorn1-profile.csv" "$experiment/data/acorn1-meta.json" \
  --acorn1 | tee "$experiment/data/acorn1-profile-output.txt"
g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/directory-aware-vector-attr-search/src -I"$hnsw" \
  "$experiment/experiments/query_cost_replay.cpp" -o "$work/query-cost-replay"
"$work/query-cost-replay" "$base" "$queries" "$dag" \
  "$experiment/data/acorn1-replay.csv" --acorn1 | tee "$experiment/data/acorn1-replay-output.txt"
python3 "$experiment/experiments/analyze_acorn1.py" | tee "$experiment/data/acorn1-summary-output.txt"
