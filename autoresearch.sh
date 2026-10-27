#!/usr/bin/env bash
# Full WIKI directory-constrained ANN benchmark; no network access.
set -euo pipefail
cd "$(dirname "$0")"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
export BLIS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1
exec production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/scripts/autoresearch_benchmark.py
