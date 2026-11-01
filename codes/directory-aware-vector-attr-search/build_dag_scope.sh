#!/usr/bin/env bash
# Build the DAG scope probes: metadata-only resolution, and resolution feeding a
# filtered HNSW search.
set -euo pipefail
cd "$(dirname "$0")"
REV_EXPECTED=ca426729609b3221563047ceb269cc1213e0d376
if [ ! -d deps/hnswlib/.git ] && [ ! -f deps/hnswlib/.git ]; then
    echo "error: deps/hnswlib submodule missing; run: git submodule update --init" >&2
    exit 1
fi
REV_ACTUAL=$(git -C deps/hnswlib rev-parse HEAD)
if [ "$REV_ACTUAL" != "$REV_EXPECTED" ]; then
    echo "error: deps/hnswlib at $REV_ACTUAL, expected $REV_EXPECTED" >&2
    exit 1
fi
mkdir -p build
${CXX:-g++} -O2 -std=c++17 -o build/dag_scope_probe src/dag_scope_probe.cpp
${CXX:-g++} -O2 -std=c++17 -Isrc -Ideps/hnswlib -o build/dag_scope_ann src/dag_scope_ann.cpp -lpthread
echo "built build/dag_scope_probe and build/dag_scope_ann"
