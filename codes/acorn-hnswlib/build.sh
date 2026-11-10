#!/usr/bin/env bash
# Build the ACORN baseline (one global HNSW + predicate-aware level-0 traversal).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/acorn-hnswlib/build
FLAGS="-std=c++17 -O3 -march=native"
INC="-I codes/shared -I codes/third_party"
g++ $FLAGS $INC -pthread \
    codes/acorn-hnswlib/src/acorn_index.cpp \
    codes/acorn-hnswlib/src/acorn_search.cpp \
    codes/acorn-hnswlib/src/acorn_main.cpp \
    codes/shared/dataset.cpp \
    -o codes/acorn-hnswlib/build/acorn_main
echo built codes/acorn-hnswlib/build/acorn_main