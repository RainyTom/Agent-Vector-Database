#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
src=researches/nested-attr-vector-search/2026-11-01-exps/experiments/merge_arms.cpp
hnsw=codes/directory-aware-vector-attr-search/deps/hnswlib
out=work/autoresearch-dag-merge
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
for file in "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv"; do
    test -s "$file" || { printf 'Missing offline fixture: %s\n' "$file" >&2; exit 1; }
done
test "$(git -C "$hnsw" rev-parse HEAD)" = ca426729609b3221563047ceb269cc1213e0d376
mkdir -p "$out"
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$out/input-sha256.txt"
g++ -std=c++17 -O3 -march=native -pthread \
    -Icodes/directory-aware-vector-attr-search/src -I"$hnsw" "$src" -o "$out/merge_arms"
"$out/merge_arms" "$base" "$queries" "$dag" "$out/results.csv" | tee "$out/run.log"
