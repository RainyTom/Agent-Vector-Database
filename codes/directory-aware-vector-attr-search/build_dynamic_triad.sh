#!/usr/bin/env bash
# Standalone dynamic-triad runner; never changes the frozen research binaries.
set -euo pipefail
cd "$(dirname "$0")"
REV_EXPECTED=ca426729609b3221563047ceb269cc1213e0d376
if [ ! -d deps/hnswlib/.git ] && [ ! -f deps/hnswlib/.git ]; then
    echo "error: deps/hnswlib submodule missing; run: git submodule update --init" >&2
    exit 1
fi
REV_ACTUAL=$(git -C deps/hnswlib rev-parse HEAD)
if [ "$REV_ACTUAL" != "$REV_EXPECTED" ]; then
    echo "error: deps/hnswlib at $REV_ACTUAL, expected $REV_EXPECTED" >&2
    exit 1
fi
mkdir -p build
"${CXX:-g++}" -O3 -march=native -std=c++17 -Wall -Wextra -o build/dynamic_triad \
    src/dynamic_triad.cpp -Isrc -Ideps/hnswlib -lpthread
echo "built build/dynamic_triad (hnswlib $REV_ACTUAL)"
