#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 10).
# cluster-far: compare global HNSW/exact with partition-clustered flat scan.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity_cluster_partflat \
  --modes cluster --metric-mode cluster --nq-per-bucket 200 --max-ef 16384 \
  --policy-s 0.06 --tune-margin 0.01 --repeats 3 --clusters 256 \
  --part-flat --parts 128 --part-seed 777
