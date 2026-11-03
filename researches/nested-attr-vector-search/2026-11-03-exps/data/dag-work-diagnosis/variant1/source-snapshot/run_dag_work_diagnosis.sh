#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.."
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
out=${DAG_WORK_OUT:-work/dag-work-diagnosis-20261103}
exp=researches/nested-attr-vector-search/2026-11-03-exps/experiments
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
mkdir -p "$out/build"
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$out/input-sha256.txt"
lscpu --json > "$out/hardware.json"
g++ --version > "$out/build/compiler.txt"
sha256sum "$exp/dag_work_diagnosis.cpp" "$exp/owner_block_search.h" "$exp/native_work_probe.h" \
    researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_composition_fixture.h \
    researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_composition_bench.cpp \
    codes/directory-aware-vector-attr-search/src/raw_l2_space.h > "$out/build/harness-sha256.txt"
for method in dag_merge nodeann; do
    family=0
    if [[ "$method" == nodeann ]]; then family=1; fi
    lib=codes/dag-composition-methods/$method/hnswlib
    g++ -std=c++17 -O3 -march=native -pthread -MMD -MF "$out/build/$method.d" \
        -DDAG_VALIDATION_FAMILY="$family" -I"$lib" -Icodes/directory-aware-vector-attr-search/src \
        "$exp/dag_work_diagnosis.cpp" -o "$out/build/$method"
    sha256sum "$lib/hnswlib/"*.h > "$out/build/$method-sha256.txt"
done
sha256sum "$out/build/dag_merge" "$out/build/nodeann" > "$out/build/binary-sha256.txt"
for method in dag_merge nodeann; do
    run="$out/direct/$method"
    mkdir -p "$run"
    taskset -c "${DAG_WORK_CPU:-3}" /usr/bin/time -v -o "$run/resources.txt" \
        "$out/build/$method" "$base" "$queries" "$dag" "$run" | tee "$run/output.txt"
done
