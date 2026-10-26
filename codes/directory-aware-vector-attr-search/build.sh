#!/usr/bin/env bash
# Build the Directory-Aware reproduction benchmark.
set -euo pipefail
cd "$(dirname "$0")"
FAISS=../../deps/PathSeer/faiss
mkdir -p build
g++ -O3 -march=native -std=c++17 -w -o build/repro src/repro.cpp \
  -Ideps/croaring/include -I"$FAISS" \
  -L"$FAISS/build/faiss" -lfaiss \
  -Ldeps/croaring/build -l:libcroaring-singleheader-source-lib.a -lpthread
echo "built build/repro"
