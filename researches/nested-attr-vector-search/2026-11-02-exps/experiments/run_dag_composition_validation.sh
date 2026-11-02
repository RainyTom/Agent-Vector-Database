#!/usr/bin/env bash
# Independent executables and hnswlib source trees; common inputs/GT/metrics.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.."
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
out=${DAG_VALIDATION_OUT:-work/dag-composition-validation-independent}
methods=codes/dag-composition-methods
base=work/packed/sift-fvecs-run1/sift_base.fvecs
queries=work/packed/sift-fvecs-run1/sift_query.fvecs
dag=work/dag-scope/workload-run1
mkdir -p "$out/build"
for file in "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv"; do
    test -s "$file" || { printf 'Missing fixture: %s\n' "$file" >&2; exit 1; }
done
sha256sum "$base" "$queries" "$dag/objects.txt" "$dag/edges.tsv" "$dag/queries.tsv" > "$out/input-sha256.txt"
for method in dag_merge nodeann acorn1 global_prefilter; do
    g++ -std=c++17 -O3 -march=native -pthread -MMD -MF "$out/build/$method.d" \
        -I"$methods/$method/hnswlib" -Icodes/directory-aware-vector-attr-search/src \
        "$methods/$method/main.cpp" -o "$out/build/$method"
    sha256sum "$methods/$method/main.cpp" "$methods/$method/hnswlib/hnswlib/"*.h \
        > "$out/build/$method-source-sha256.txt"
done
g++ --version > "$out/build/compiler.txt"
for nq in 24 120; do
    for semantics in recursive direct; do
        run="$out/nq$nq-$semantics"
        mkdir -p "$run"
        for method in dag_merge nodeann acorn1 global_prefilter; do
            mkdir -p "$run/$method"
            mode=arms
            if [[ "$method" == dag_merge && "$nq" == 24 ]]; then mode=all; fi
            printf 'Running method=%s queries=%s semantics=%s\n' "$method" "$nq" "$semantics"
            taskset -c "${DAG_VALIDATION_CPU:-3}" "$out/build/$method" "$base" "$queries" "$dag" \
                "$run/$method" "$nq" "$mode" "$semantics" | tee "$run/$method/output.txt"
        done
    done
done
python3 - "$out" <<'PY'
import csv, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
methods = ('dag_merge', 'nodeann', 'acorn1', 'global_prefilter')
for nq in (24, 120):
    for sem in ('recursive', 'direct'):
        run = root / f'nq{nq}-{sem}'
        combined, fieldnames, manifest = [], None, None
        for method in methods:
            source = run / method
            with (source / f'arms-{sem}.csv').open() as f:
                reader = csv.DictReader(f)
                if fieldnames is None: fieldnames = reader.fieldnames
                assert reader.fieldnames == fieldnames
                combined.extend(reader)
            current = (source / f'queries-{sem}.csv').read_text()
            if manifest is None: manifest = current
            assert current == manifest, 'method query/GT manifests differ'
        with (run / f'arms-{sem}.csv').open('w') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader(); writer.writerows(combined)
        for name in (f'queries-{sem}.csv', f'variants-{sem}.csv', f'kernels-{sem}.csv',
                     f'scope-{sem}.csv', f'bitmap-amortisation-{sem}.csv'):
            (run / name).write_bytes((run / 'dag_merge' / name).read_bytes())
# Compiler-generated dependency lists prove actual include resolution, not just flags.
for method in methods:
    deps = (root / 'build' / f'{method}.d').read_text()
    for other in methods:
        if other != method:
            assert f'codes/dag-composition-methods/{other}/' not in deps, 'cross-method include'
    assert f'codes/dag-composition-methods/{method}/hnswlib/hnswlib/hnswalg.h' in deps
    assert 'codes/directory-aware-vector-attr-search/deps/hnswlib' not in deps
print('PASS independent compilation and identical per-method query/GT manifests')
PY
python3 researches/nested-attr-vector-search/2026-11-02-exps/experiments/analyze_dag_validation.py \
    --dir "$out" --out "$out/summary.json"
