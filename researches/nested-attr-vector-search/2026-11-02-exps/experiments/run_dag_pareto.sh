#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.."
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
out=${DAG_PARETO_OUT:-work/dag-two-cases-m64}
nq=${DAG_PARETO_NQ:-120}
methods=codes/dag-composition-methods
exp=researches/nested-attr-vector-search/2026-11-02-exps/experiments
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
mkdir -p "$out/build"
for file in "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv"; do test -s "$file"; done
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$out/input-sha256.txt"
g++ --version > "$out/build/compiler.txt"
lscpu --json > "$out/hardware.json"
sha256sum "$exp/dag_composition_bench.cpp" "$exp/dag_composition_fixture.h" "$exp/dag_pareto_benchmark.h" \
    codes/directory-aware-vector-attr-search/src/raw_l2_space.h > "$out/build/harness-source-sha256.txt"
for method in dag_merge nodeann acorn1 global_prefilter; do
    g++ -std=c++17 -O3 -march=native -pthread -MMD -MF "$out/build/$method.d" \
        -I"$methods/$method/hnswlib" -Icodes/directory-aware-vector-attr-search/src \
        "$methods/$method/main.cpp" -o "$out/build/$method"
    sha256sum "$methods/$method/main.cpp" "$methods/$method/hnswlib/hnswlib/"*.h \
        > "$out/build/$method-source-sha256.txt"
done
for semantics in recursive direct; do
    run="$out/nq$nq-$semantics"
    for method in dag_merge nodeann acorn1 global_prefilter; do
        mkdir -p "$run/$method"
        printf 'RUN nq=%s mode=%s method=%s M=64 efConstruction=200 cases=k10/ef32,k100/ef128\n' "$nq" "$semantics" "$method"
        taskset -c "${DAG_PARETO_CPU:-3}" /usr/bin/time -v -o "$run/$method/process-resource.txt" \
            "$out/build/$method" "$base" "$queries" "$dag" "$run/$method" "$nq" pareto "$semantics" \
            | tee "$run/$method/output.txt"
    done
done
python3 - "$out" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
methods = ('dag_merge', 'nodeann', 'acorn1', 'global_prefilter')
for method in methods:
    deps = (root / 'build' / f'{method}.d').read_text()
    assert f'codes/dag-composition-methods/{method}/hnswlib/hnswlib/hnswalg.h' in deps
    assert 'codes/directory-aware-vector-attr-search/deps/hnswlib' not in deps
    for other in methods:
        if other != method:
            assert f'codes/dag-composition-methods/{other}/' not in deps
print('PASS independent hnswlib compile dependencies')
PY
python3 "$exp/analyze_dag_pareto.py" --dir "$out" --out "$out/analysis" --nq "$nq"
