#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 6).
# 1M-scale selectivity sweep, 3 eval repeats (median) for stable cold-gather timing.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_1m \
  --out-dir production/nested-attr-vector-search/work/selectivity_1m \
  --modes random --nq-per-bucket 200 --max-ef 8192 --policy-s 0.02 --repeats 3
