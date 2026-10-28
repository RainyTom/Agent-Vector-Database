#!/usr/bin/env bash
# Build the NodeANN hnswlib research runner (independent of repro.cpp/FAISS).
# hnswlib is pinned as a git submodule at deps/hnswlib, revision
# ca426729609b3221563047ceb269cc1213e0d376.
set -euo pipefail
cd "$(dirname "$0")"

REV_EXPECTED=ca426729609b3221563047ceb269cc1213e0d376
if [ -d deps/hnswlib/.git ] || [ -f deps/hnswlib/.git ]; then
    REV_ACTUAL=$(git -C deps/hnswlib rev-parse HEAD)
    if [ "$REV_ACTUAL" != "$REV_EXPECTED" ]; then
        echo "error: deps/hnswlib at $REV_ACTUAL, expected $REV_EXPECTED" >&2
        exit 1
    fi
else
    echo "error: deps/hnswlib submodule missing; run: git submodule update --init" >&2
    exit 1
fi

mkdir -p build
g++ -O3 -march=native -std=c++17 -w -o build/nodeann_research \
    src/nodeann_research.cpp -Isrc -Ideps/hnswlib -lpthread
echo "built build/nodeann_research (hnswlib $REV_ACTUAL)"
