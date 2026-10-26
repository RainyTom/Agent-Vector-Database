#!/usr/bin/env bash
# 数据集幂等下载脚本（迁移用）。已存在的文件跳过；curl -C - 断点续传。
# 用法: bash datasets/download_all.sh [--with-melbourne]
# D 节（Google Drive）与 E/F 节不在本脚本内，见 SOURCES.md。
set -euo pipefail
cd "$(dirname "$0")/.."

dl() { # dl <url> <out>
  local url="$1" out="$2"
  if [ -s "$out" ]; then echo "SKIP  $out"; return 0; fi
  echo "GET   $out"
  mkdir -p "$(dirname "$out")"
  curl -fSL -C - --retry 5 --retry-delay 2 -o "$out" "$url"
}

echo "== A. ann-benchmarks HDF5 =="
for f in sift-128-euclidean mnist-784-euclidean fashion-mnist-784-euclidean \
         glove-25-angular glove-50-angular glove-100-angular glove-200-angular \
         nytimes-256-angular lastfm-64-dot kosarak-jaccard movielens10m-jaccard; do
  dl "http://ann-benchmarks.com/$f.hdf5" "datasets/$f.hdf5"
done

echo "== C. Massive-STEPS =="
dl "https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-New-York/resolve/main/new_york_checkins.csv" \
   "datasets/massive-steps/ny_checkins.csv"
dl "https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-Istanbul/resolve/main/istanbul_checkins.csv" \
   "datasets/massive-steps/istanbul_checkins.csv"
dl "https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-Jakarta/resolve/main/jakarta_checkins.csv" \
   "datasets/massive-steps/jakarta_checkins.csv"
dl "https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-Petaling-Jaya/resolve/main/petaling_jaya_checkins.csv" \
   "datasets/massive-steps/petaling_jaya_checkins.csv"
dl "https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-Sao-Paulo/resolve/main/sao_paulo_checkins.csv" \
   "datasets/massive-steps/sao_paulo_checkins.csv"
dl "https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-Moscow/resolve/main/moscow_checkins.csv" \
   "datasets/massive-steps/moscow_checkins.csv"
if [ "${1:-}" = "--with-melbourne" ]; then
  dl "https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-Melbourne/resolve/main/melbourne_checkins.csv" \
     "datasets/massive-steps/melbourne_checkins.csv"
fi

echo "== B/D/E/F 节需手工处理，见 datasets/SOURCES.md =="
echo "完成。校验: du -sh datasets/ && cat datasets/SOURCES.md"
