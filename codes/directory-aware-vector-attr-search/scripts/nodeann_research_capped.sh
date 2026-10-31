#!/usr/bin/env bash
# Wrapper: cap virtual memory so the expected WIKI B0 (T=0) bad_alloc from
# 363,467 x hnswlib 65536-mutex tables fails cleanly instead of OOM-ing the box.
set -euo pipefail
ulimit -v 41943040  # 40 GiB virtual
exec "$(dirname "$0")/../build/nodeann_research" "$@"
