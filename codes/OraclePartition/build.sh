#!/usr/bin/env bash
# Build the OraclePartition baseline (one HNSW graph per attribute, then merge).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/OraclePartition/build
FLAGS="-std=c++17 -O3 -march=native -fopenmp"
INC="-I codes/shared -I codes/third_party -I codes/OraclePartition/src"
g++ $FLAGS $INC \
    codes/OraclePartition/src/nodeann_index.cpp \
    codes/OraclePartition/src/nodeann_search.cpp \
    codes/OraclePartition/src/nodeann_main.cpp \
    codes/shared/dataset.cpp \
    -o codes/OraclePartition/build/nodeann
echo built codes/OraclePartition/build/nodeann