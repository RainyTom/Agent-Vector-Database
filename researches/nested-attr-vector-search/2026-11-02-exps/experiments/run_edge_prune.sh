#!/usr/bin/env bash
# Cross-owner edge pruning ablation: entries and qualified adjacency unchanged.
# Also re-runs the two reference arms in this session, because the archived
# ratios were produced in a different run and absolute times are not comparable.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.."
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
out=${EDGE_PRUNE_OUT:-work/edge-prune-m64}
nq=${EDGE_PRUNE_NQ:-120}
methods=codes/dag-composition-methods
exp=researches/nested-attr-vector-search/2026-11-02-exps/experiments
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
mkdir -p "$out/build"
for file in "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv"; do
    test -s "$file" || { printf 'Missing fixture: %s\n' "$file" >&2; exit 1; }
done
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$out/input-sha256.txt"
sha256sum "$exp/dag_composition_bench.cpp" "$exp/dag_composition_fixture.h" "$exp/dag_pareto_benchmark.h" \
    codes/directory-aware-vector-attr-search/src/raw_l2_space.h > "$out/build/harness-source-sha256.txt"
g++ --version > "$out/build/compiler.txt"
lscpu --json > "$out/hardware.json"
for method in dag_merge nodeann; do
    g++ -std=c++17 -O3 -march=native -pthread -MMD -MF "$out/build/$method.d" \
        -I"$methods/$method/hnswlib" -Icodes/directory-aware-vector-attr-search/src \
        "$methods/$method/main.cpp" -o "$out/build/$method"
    sha256sum "$methods/$method/main.cpp" "$methods/$method/hnswlib/hnswlib/"*.h \
        > "$out/build/$method-source-sha256.txt"
done
g++ -std=c++17 -O3 -march=native -pthread -I"$methods/dag_merge/hnswlib" \
    -Icodes/directory-aware-vector-attr-search/src "$exp/dag_composition_test.cpp" \
    -o "$out/build/dag_composition_test"
taskset -c "${EDGE_PRUNE_CPU:-3}" "$out/build/dag_composition_test" | tee "$out/regression.txt"
for semantics in recursive direct; do
    # Same-session references for the QPS ratio; the ablation never rescales them.
    for method in dag_merge nodeann; do
        ref="$out/envcheck/nq$nq-$semantics/$method"
        mkdir -p "$ref"
        printf 'REFERENCE method=%s semantics=%s\n' "$method" "$semantics"
        taskset -c "${EDGE_PRUNE_CPU:-3}" /usr/bin/time -v -o "$ref/process-resource.txt" \
            "$out/build/$method" "$base" "$queries" "$dag" "$ref" "$nq" pareto "$semantics" \
            | tee "$ref/output.txt"
    done
    run="$out/nq$nq-$semantics"
    mkdir -p "$run"
    printf 'RUN nq=%s semantics=%s M=64 efConstruction=200 cases=k10/ef32,k100/ef128\n' "$nq" "$semantics"
    taskset -c "${EDGE_PRUNE_CPU:-3}" /usr/bin/time -v -o "$run/process-resource.txt" \
        "$out/build/dag_merge" "$base" "$queries" "$dag" "$run" "$nq" edge-prune "$semantics" \
        | tee "$run/output.txt"
done
python3 "$exp/analyze_edge_prune.py" --dir "$out" --nq "$nq"
