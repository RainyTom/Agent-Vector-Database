#!/usr/bin/env bash
# Build the NodeANN baseline (per-attribute HNSW, then merge).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/NodeANN/build
FLAGS="-std=c++17 -O3 -fno-tree-vectorize -fno-tree-slp-vectorize -DNO_MANUAL_VECTORIZATION"
INC="-I codes/shared -I codes/third_party"
g++ $FLAGS $INC codes/NodeANN/src/nodeann_bench.cpp -o codes/NodeANN/build/nodeann_measured
echo built codes/NodeANN/build/nodeann_measured
