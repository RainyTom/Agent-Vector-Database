#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 4).
# Attribute-filter selectivity sweep on SIFT100k + CCS-shaped hierarchy.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/nested-attr-vector-search/src/selectivity_harness.py "$@"
