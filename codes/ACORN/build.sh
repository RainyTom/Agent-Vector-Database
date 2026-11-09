#!/usr/bin/env bash
# Build the ACORN baseline (one global HNSW + predicate-aware level-0 traversal).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/ACORN/build
FLAGS="-std=c++17 -O3"
INC="-I codes/shared -I codes/third_party"
g++ $FLAGS $INC -pthread \
    codes/ACORN/src/acorn_index.cpp \
    codes/ACORN/src/acorn_search.cpp \
    codes/ACORN/src/acorn_main.cpp \
    codes/shared/dataset.cpp \
    -o codes/ACORN/build/acorn_main
echo built codes/ACORN/build/acorn_main