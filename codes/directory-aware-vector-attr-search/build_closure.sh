#!/usr/bin/env bash
# Independent closure runner; HNSW_DIR may point to an existing pinned checkout.
set -euo pipefail
cd "$(dirname "$0")"
REV_EXPECTED=ca426729609b3221563047ceb269cc1213e0d376
HNSW_DIR=${HNSW_DIR:-deps/hnswlib}
if [ ! -d "$HNSW_DIR/.git" ] && [ ! -f "$HNSW_DIR/.git" ]; then
    echo "error: HNSW_DIR must be a git checkout; initialize deps/hnswlib or set HNSW_DIR" >&2
    exit 1
fi
REV_ACTUAL=$(git -C "$HNSW_DIR" rev-parse HEAD)
if [ "$REV_ACTUAL" != "$REV_EXPECTED" ]; then
    echo "error: HNSW_DIR at $REV_ACTUAL, expected $REV_EXPECTED" >&2
    exit 1
fi
mkdir -p build
"${CXX:-g++}" -O3 -march=native -std=c++17 -Wall -Wextra -pthread \
    src/closure_research.cpp -Isrc -I"$HNSW_DIR" -o build/closure_research
echo "built build/closure_research (hnswlib $REV_ACTUAL)"
