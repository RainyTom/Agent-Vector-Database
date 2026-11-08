#!/usr/bin/env bash
# Build both benchmarks. Requires g++ with C++17; expects the CCS+SIFT dataset
# at datasets/ccs-sift-dag (see scripts/prepare_large.py and scripts/prepare_pq.py).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/NestedAttrVectorRetrieval/build
FLAGS="-std=c++17 -O3 -fno-tree-vectorize -fno-tree-slp-vectorize -DNO_MANUAL_VECTORIZATION"
INC="-I codes/NestedAttrVectorRetrieval/src -I codes/NestedAttrVectorRetrieval/third_party"
g++ $FLAGS $INC codes/NestedAttrVectorRetrieval/src/attr_skeleton_bench.cpp -o codes/NestedAttrVectorRetrieval/build/attr_skeleton
g++ $FLAGS $INC codes/NestedAttrVectorRetrieval/src/nodeann_bench.cpp -o codes/NestedAttrVectorRetrieval/build/nodeann_measured
echo built codes/NestedAttrVectorRetrieval/build/{attr_skeleton,nodeann_measured}
