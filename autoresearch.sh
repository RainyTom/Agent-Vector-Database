#!/usr/bin/env bash
# Canonical benchmark entrypoint (autoresearch Phase 1).
# Builds the candidate, runs the fixed hierarchical-filtered top-k workload,
# and prints METRIC lines. Deterministic workload, fixed seed, no network.
set -euo pipefail
cd "$(dirname "$0")"
exec tools/.venv/bin/python production/nested-attr-vector-search/src/harness.py "$@"
