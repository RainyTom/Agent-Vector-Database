#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 11).
# Motivation: FAISS filtered ANN on SIFT100k at selectivity 1%,5%,10%,15%,20%,25%,30%.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity_grid \
  --modes random --metric-mode random --nq-per-bucket 200 --max-ef 16384 \
  --policy-s 0.06 --tune-margin 0.01 --repeats 3
