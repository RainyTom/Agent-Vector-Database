#!/usr/bin/env bash
# Current entry: isolated methods, recursive/direct semantics, 24/120 queries.
set -euo pipefail
exec bash "$(dirname -- "${BASH_SOURCE[0]}")/run_dag_composition_validation.sh"
