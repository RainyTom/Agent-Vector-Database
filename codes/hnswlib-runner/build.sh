#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/hnswlib-runner/build
g++ -std=c++17 -O3 -march=native -I codes/shared -I codes/third_party -pthread \
  codes/hnswlib-runner/src/hnsw_runner.cpp -o codes/hnswlib-runner/build/hnsw_runner
echo built codes/hnswlib-runner/build/hnsw_runner
