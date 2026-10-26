#!/usr/bin/env bash
# Paired cluster-far/near selectivity grid with filtered IVF.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity_cluster_ivf_100k \
  --modes cluster,cluster_near --metric-mode cluster --nq-per-bucket 200 --max-ef 16384 \
  --policy-s 0.06 --tune-margin 0.01 --repeats 3 --clusters 256 \
  --ivf --ivf-nlist 1024 --ivf-max-nprobe 1024
