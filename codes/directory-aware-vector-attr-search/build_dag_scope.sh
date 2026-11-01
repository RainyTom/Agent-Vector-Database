#!/usr/bin/env bash
# Build the DAG scope probes: metadata-only resolution, and resolution feeding a
# filtered HNSW search.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
${CXX:-g++} -O2 -std=c++17 -o build/dag_scope_probe src/dag_scope_probe.cpp
${CXX:-g++} -O2 -std=c++17 -Isrc -Ideps/hnswlib -o build/dag_scope_ann src/dag_scope_ann.cpp -lpthread
echo "built build/dag_scope_probe and build/dag_scope_ann"
