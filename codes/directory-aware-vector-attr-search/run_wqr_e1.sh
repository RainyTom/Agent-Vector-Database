#!/usr/bin/env bash
# WQR E1 grid: arms {G,B,W,E} × L {1,2,4,8,16} × portal_ef {max(64,4L), max(256,4L)}.
# GT caches must already exist (prime runs). Output: work/wqr-e1/<data>-<arm>-L<L>-ef<ef>.*
set -euo pipefail
cd "$(dirname "$0")"
BIN=build/wqr_research
OUT=/tmp/wqr-impl/work/wqr-e1
mkdir -p "$OUT"
WIKI=/home/daihua/projs/2026040404-yiming/Agent-Vector-Database/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/work/dynamic_triad/wiki-topology/wiki-topology-s20261029-q456-f7046c642b34/root-3626
SIFT=/home/daihua/projs/2026040404-yiming/Agent-Vector-Database/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/work/dynamic_triad/crossover

run_one() {
    local tag=$1 data=$2 trace=$3 gt=$4 arm=$5 L=$6 ef=$7
    local out="$OUT/${tag}-${arm}-L${L}-ef${ef}"
    if [ -s "$out.summary.txt" ]; then return 0; fi
    "$BIN" --data "$data" --trace "$trace" --arm "$arm" --probe-blocks "$L" \
        --portal-ef "$ef" --local-ef 1024 --gt-cache "$gt" --out "$out" > "$out.log" 2>&1
    echo "done $tag $arm L=$L ef=$ef"
}
export -f run_one
export BIN OUT

jobs_list() {
    for arm in G B W E; do
        for L in 1 2 4 8 16; do
            for ef in 64 256; do
                echo "wiki $WIKI/native $WIKI/traces/qu1.txt $OUT/wiki-qu1.gt $arm $L $ef"
                echo "sift $SIFT/datasets/sift-n12000-r1000-q64-d3-s20261029 $SIFT/traces/n12000-s20261029-1d584b6d2829/sift-d3-uniform-mixed-r0p2-qu1.txt $OUT/sift-r0p2-qu1.gt $arm $L $ef"
            done
        done
    done
}

jobs_list | xargs -P 14 -n 7 bash -c 'run_one "$@"' _
echo "ALL E1 RUNS DONE"
