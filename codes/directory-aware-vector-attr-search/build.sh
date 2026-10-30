#!/usr/bin/env bash
# Build the Directory-Aware reproduction benchmark.
set -euo pipefail
cd "$(dirname "$0")"
FAISS=../../deps/PathSeer/faiss
mkdir -p build
g++ -O3 -march=native -std=c++17 -w -o build/repro src/repro.cpp \
  -I../../deps/croaring/include -I"$FAISS" \
  -L"$FAISS/build/faiss" -lfaiss \
  -L../../deps/croaring/build -l:libcroaring-singleheader-source-lib.a -lpthread
echo "built build/repro"
g++ -O3 -march=native -std=c++17 -w -o build/hlg_tool src/hlg_tool.cpp \
  -I../../deps/croaring/include -I"$FAISS" \
  -L"$FAISS/build/faiss" -lfaiss \
  -L../../deps/croaring/build -l:libcroaring-singleheader-source-lib.a -lpthread
g++ -O3 -march=native -std=c++17 -w -o build/wqr_research src/wqr_research.cpp \
  -Ideps/hnswlib -lpthread
echo "built build/wqr_research"
