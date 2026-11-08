#!/usr/bin/env bash
# Build the NestedAttrVectorRetrieval benchmark (attr_skeleton).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/NestedAttrVectorRetrieval/build
FLAGS="-std=c++17 -O3 -fno-tree-vectorize -fno-tree-slp-vectorize -DNO_MANUAL_VECTORIZATION"
INC="-I codes/NestedAttrVectorRetrieval/src -I codes/shared -I codes/third_party"
g++ $FLAGS $INC codes/NestedAttrVectorRetrieval/src/attr_skeleton_bench.cpp \
  -o codes/NestedAttrVectorRetrieval/build/attr_skeleton
echo built codes/NestedAttrVectorRetrieval/build/attr_skeleton
