#!/usr/bin/env bash
# Builds the attribute-skeleton index tool (subcommands: build, search).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/NestedAttrVectorRetrieval/build
FLAGS="-std=c++17 -O3 -march=native -fopenmp"
INC="-I codes/NestedAttrVectorRetrieval/src -I codes/shared -I codes/third_party"
g++ $FLAGS $INC \
  codes/NestedAttrVectorRetrieval/src/asi_main.cpp \
  codes/NestedAttrVectorRetrieval/src/asi_index.cpp \
  codes/NestedAttrVectorRetrieval/src/asi_search.cpp \
  codes/shared/dataset.cpp \
  -o codes/NestedAttrVectorRetrieval/build/asi
echo built codes/NestedAttrVectorRetrieval/build/asi
