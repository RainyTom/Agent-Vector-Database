#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 8).
# 100k cluster-near: feasible region compact around the query (centroid query).
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity_cluster_near \
  --modes cluster_near --metric-mode cluster_near --nq-per-bucket 200 \
  --max-ef 16384 --policy-s 0.06 --repeats 3 --clusters 256
