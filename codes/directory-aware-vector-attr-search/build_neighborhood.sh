#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
expected=ca426729609b3221563047ceb269cc1213e0d376
actual=$(git -C deps/hnswlib rev-parse HEAD)
if [ "$actual" != "$expected" ]; then
    echo "Pinned hnswlib mismatch: $actual != $expected" >&2
    exit 1
fi
mkdir -p build
"${CXX:-g++}" -O3 -march=native -std=c++17 -Wall -Wextra \
    src/neighborhood_research.cpp -Isrc -Ideps/hnswlib -lpthread -o build/neighborhood_research
echo "built build/neighborhood_research (hnswlib $actual)"
