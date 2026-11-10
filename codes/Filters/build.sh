#!/usr/bin/env bash
# Builds the prefilter / postfilter baselines (one binary, two modes).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/Filters/build
FLAGS="-std=c++17 -O3 -march=native -fopenmp"
INC="-I codes/shared -I codes/third_party"
g++ $FLAGS $INC \
  codes/Filters/src/filters_main.cpp \
  codes/shared/dataset.cpp \
  -o codes/Filters/build/filters
echo built codes/Filters/build/filters
