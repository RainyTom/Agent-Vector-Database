#!/usr/bin/env bash
# Canonical benchmark entrypoint (segment 2).
# Semantic episode retrieval on the six-city Massive-STEPS workload.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 production/semantic-mobility-search/src/harness.py "$@"
