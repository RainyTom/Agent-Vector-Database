#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 15).
# Extended grid at 100k with FAISS IVF + selector added.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity_grid_ext_ivf \
  --modes random --metric-mode random --nq-per-bucket 200 --max-ef 16384 \
  --policy-s 0.06 --tune-margin 0.01 --repeats 3 \
  --ivf --ivf-nlist 1024 --ivf-max-nprobe 1024
