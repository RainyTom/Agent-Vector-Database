#!/usr/bin/env bash
# Build the NodeANN baseline (one HNSW graph per attribute, then merge).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/NodeANN/build
FLAGS="-std=c++17 -O3 -fno-tree-vectorize -fno-tree-slp-vectorize -DNO_MANUAL_VECTORIZATION"
INC="-I codes/shared -I codes/third_party -I codes/NodeANN/src"
g++ $FLAGS $INC \
    codes/NodeANN/src/nodeann_index.cpp \
    codes/NodeANN/src/nodeann_search.cpp \
    codes/NodeANN/src/nodeann_main.cpp \
    codes/shared/dataset.cpp \
    -o codes/NodeANN/build/nodeann
echo built codes/NodeANN/build/nodeann