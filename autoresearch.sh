#!/usr/bin/env bash
# Extended selectivity grid on SIFT1M with FAISS IVF + selector.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_1m \
  --out-dir production/nested-attr-vector-search/work/selectivity_grid_ext_1m_ivf \
  --modes random --metric-mode random --nq-per-bucket 200 --max-ef 8192 \
  --policy-s 0.02 --tune-margin 0.01 --repeats 3 \
  --ivf --ivf-nlist 1024 --ivf-max-nprobe 1024
