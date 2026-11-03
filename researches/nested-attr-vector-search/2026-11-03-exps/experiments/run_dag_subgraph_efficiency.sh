#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.."
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
out=${DAG_SUBGRAPH_OUT:-work/dag-subgraph-efficiency-20261103}
exp=researches/nested-attr-vector-search/2026-11-03-exps/experiments
old=researches/nested-attr-vector-search/2026-11-02-exps/experiments
methods=codes/dag-composition-methods
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
mkdir -p "$out/build"
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$out/input-sha256.txt"
lscpu --json > "$out/hardware.json"
g++ --version > "$out/build/compiler.txt"
printf '%s\n' '-std=c++17 -O3 -march=native -pthread' > "$out/build/flags.txt"
sha256sum "$exp/dag_subgraph_efficiency.cpp" "$exp/run_dag_subgraph_efficiency.sh" \
    "$old/dag_composition_bench.cpp" "$old/dag_composition_fixture.h" \
    codes/directory-aware-vector-attr-search/src/raw_l2_space.h > "$out/build/harness-sha256.txt"
for method in dag_merge nodeann; do
    family=0
    if [[ "$method" == nodeann ]]; then family=1; fi
    g++ -std=c++17 -O3 -march=native -pthread -MMD -MF "$out/build/$method.d" \
        -DDAG_VALIDATION_FAMILY="$family" -I"$methods/$method/hnswlib" \
        -Icodes/directory-aware-vector-attr-search/src "$exp/dag_subgraph_efficiency.cpp" -o "$out/build/$method"
    sha256sum "$methods/$method/hnswlib/hnswlib/"*.h > "$out/build/$method-sha256.txt"
done
sha256sum "$out/build/dag_merge" "$out/build/nodeann" > "$out/build/binary-sha256.txt"
for semantics in direct recursive; do
    for method in dag_merge nodeann; do
        run="$out/$semantics/$method"
        mkdir -p "$run"
        taskset -c "${DAG_SUBGRAPH_CPU:-3}" /usr/bin/time -v -o "$run/resources.txt" \
            "$out/build/$method" "$base" "$queries" "$dag" "$run" "$semantics" | tee "$run/output.txt"
    done
done
python3 "$exp/analyze_dag_subgraph_efficiency.py" --dir "$out"
