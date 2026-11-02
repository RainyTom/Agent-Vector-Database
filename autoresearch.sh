#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
exec bash researches/nested-attr-vector-search/2026-11-02-exps/experiments/run_dag_pareto.sh
