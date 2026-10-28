#!/usr/bin/env bash
# Build the boundary_probe helper (independent of repro.cpp / build.sh).
set -euo pipefail
cd "$(dirname "$0")"
FAISS=../../deps/PathSeer/faiss
mkdir -p build
g++ -O3 -march=native -std=c++17 -w -o build/boundary_probe src/boundary_probe.cpp \
  -I"$FAISS" -L"$FAISS/build/faiss" -lfaiss -lpthread
echo "built build/boundary_probe"
