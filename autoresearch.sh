#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 7).
# 100k correlated-cluster predicates: compact feasible regions, independent queries.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity_cluster \
  --modes cluster --metric-mode cluster --nq-per-bucket 200 --max-ef 16384 \
  --policy-s 0.06 --repeats 3 --clusters 256
