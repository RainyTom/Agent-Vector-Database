#!/usr/bin/env bash
# Builds the Directory-Aware baseline: global HNSW + trie bitmaps.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/DirectoryAware/build
FLAGS="-std=c++17 -O3 -march=native -fopenmp"
INC="-I codes/shared -I codes/third_party"
g++ $FLAGS $INC codes/DirectoryAware/src/directory_aware.cpp codes/shared/dataset.cpp \
  -o codes/DirectoryAware/build/directory_aware
echo built codes/DirectoryAware/build/directory_aware
