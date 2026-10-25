#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 5).
# 1M-scale selectivity sweep: random predicates only, cached HNSW M=16.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_1m \
  --out-dir production/nested-attr-vector-search/work/selectivity_1m \
  --modes random --nq-per-bucket 100 --max-ef 8192
