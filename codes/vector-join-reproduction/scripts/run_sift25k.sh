#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_dir="$(cd "${project_dir}/../.." && pwd)"
build_dir="${project_dir}/build"
data_dir="${workspace_dir}/datasets/sift25k"

cmake -S "${project_dir}" -B "${build_dir}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}"

for method in independent hard soft mi mi-adapt; do
  "${build_dir}/vector_join_bench" \
    --base "${data_dir}/base.f32" \
    --queries "${data_dir}/queries.f32" \
    --dim 128 \
    --threshold "${THRESHOLD:-90000}" \
    --base-limit "${BASE_LIMIT:-25000}" \
    --query-limit "${QUERY_LIMIT:-1000}" \
    --ground-truth-limit "${GROUND_TRUTH_LIMIT:-100}" \
    --method "${method}"
done
