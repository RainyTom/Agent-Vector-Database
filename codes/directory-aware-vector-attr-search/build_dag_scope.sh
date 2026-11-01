#!/usr/bin/env bash
# Build the DAG scope-resolution probe (no hnswlib dependency: metadata only).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
${CXX:-g++} -O2 -std=c++17 -o build/dag_scope_probe src/dag_scope_probe.cpp
echo "built build/dag_scope_probe"
