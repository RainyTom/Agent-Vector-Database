#!/usr/bin/env bash
# Trunk of the official ACORN implementation (guestrin-lab/ACORN, SIGMOD'24).
#
# Only the files needed by IndexACORNFlat are kept: the ACORN index itself plus
# the flat storage and the distance/heap utilities it calls. The upstream CMake
# project, GPU code, Python bindings, tests and benchmarks were removed, so the
# build is a plain compile of an explicit source list.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p codes/acorn-official-driver/build

FLAGS="-std=c++17 -O3 -fopenmp -DFINTEGER=int -DBOOST_MATH_DISABLE_FLOAT128"
INC="-I codes/acorn-official -I codes/acorn-official-driver/build/include"

SOURCES=(
  codes/acorn-official/faiss/IndexACORN.cpp
  codes/acorn-official/faiss/Index.cpp
  codes/acorn-official/faiss/IndexFlat.cpp
  codes/acorn-official/faiss/IndexFlatCodes.cpp
  codes/acorn-official/faiss/impl/ACORN.cpp
  codes/acorn-official/faiss/impl/AuxIndexStructures.cpp
  codes/acorn-official/faiss/impl/FaissException.cpp
  codes/acorn-official/faiss/impl/IDSelector.cpp
  codes/acorn-official/faiss/utils/distances.cpp
  codes/acorn-official/faiss/utils/distances_simd.cpp
  codes/acorn-official/faiss/utils/extra_distances.cpp
  codes/acorn-official/faiss/utils/partitioning.cpp
  codes/acorn-official/faiss/utils/random.cpp
  codes/acorn-official/faiss/utils/sorting.cpp
  codes/acorn-official/faiss/utils/utils.cpp
)

# faiss reads two generated headers; they are normally produced by CMake.
mkdir -p codes/acorn-official-driver/build/include/faiss
cat > codes/acorn-official-driver/build/include/faiss/faiss.h <<'EOF'
#pragma once
EOF
printf '#pragma once\n#define FAISS_VERSION_MAJOR 1\n#define FAISS_VERSION_MINOR 8\n#define FAISS_VERSION_PATCH 0\n' \
  > codes/acorn-official-driver/build/include/faiss/version.h

OBJ=codes/acorn-official-driver/build/obj
mkdir -p "$OBJ"
for source in "${SOURCES[@]}"; do
  object="$OBJ/$(echo "${source#codes/acorn-official/}" | tr '/' '_').o"
  if [ ! -f "$object" ] || [ "$source" -nt "$object" ]; then
    g++ $FLAGS $INC -c "$source" -o "$object"
  fi
done

g++ $FLAGS $INC \
  codes/acorn-official-driver/acorn_official_run.cpp \
  "$OBJ"/*.o -lopenblas \
  -o codes/acorn-official-driver/build/acorn_official
echo built codes/acorn-official-driver/build/acorn_official
