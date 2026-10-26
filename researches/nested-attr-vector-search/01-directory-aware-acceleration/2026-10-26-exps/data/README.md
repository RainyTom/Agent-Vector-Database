# 数据说明：选择率扫描（2026-10-26）

来源：`production/nested-attr-vector-search/src/selectivity_bench.cpp`；100k random 为 session
`filtered-ann-selectivity-sweep`，1M 为 segment `filtered-ann-selectivity-1m-200q`，
100k cluster-far 为 segment `filtered-ann-selectivity-cluster-100k`，cluster-near 为
`filtered-ann-selectivity-cluster-near-margin`，partition-flat 为
`filtered-ann-selectivity-cluster-partflat`，百分网格为 `filtered-ann-selectivity-grid-100k`。

- `selectivity_curve_20261026.csv`：SIFT100k run #82 的逐行结果（131 行，表头 1 行）。
- `selectivity_20261026.jsonl`：同一结果的 JSONL 版本。
- `summary_20261026.json`：run #82 的汇总；run #81 的主指标为 22,425.97 QPS（best kept），run #82 复现为 21,839.33 QPS。CSV 来自 #82。
- `selectivity_1m_curve_20261026.csv`：SIFT1M run #88 的逐行结果（随机谓词，200/bucket，3 次评估取中位数）。
- `selectivity_1m_20261026.jsonl`：同一结果的 JSONL 版本。
- `summary_1m_20261026.json`：run #88 的汇总；主指标 3,510.91 QPS，上一轮 run #87 为 3,376.30（差异 4.0%）。单次评估的 run #86 因 s=0.001 冷 gather 离群（2,769.60）被弃用。1M 与 100k 的主指标不可直接比较。
- `selectivity_cluster_curve_20261026.csv` / `.jsonl` / `summary_cluster_20261026.json`：SIFT100k cluster 模式（k-means k=256，紧凑可行簇，查询独立），200/bucket，3 次评估中位数，run #89；主指标 14,446.65 QPS，policy 0，crossover 桶 0.5。
- `selectivity_cluster_probe_32768_20261026.csv` / `summary_cluster_probe_32768_20261026.json`：同一 cluster 模式的 ef=32768 探针，50 条/桶、单次评估；用于确认提高 ef 仍不能恢复 r95。
- `selectivity_cluster_pathseer_full_20261026.csv` / `summary_cluster_pathseer_full_20261026.json`：PathSeer filter-aware 基线（M=16，M_exp=32，M_exp_search=16，ef≤4096），200/bucket、3 次重复；用于确认扩展图在当前参数下未修复远簇召回。
- `selectivity_cluster_near_curve_20261026.csv` / `.jsonl` / `summary_cluster_near_20261026.json`：SIFT100k cluster-near（同一 k-means 可行集，查询=簇中心；tune margin 0.01），200/bucket、3 次重复，run #91；主指标 35,140.42 QPS，policy 30,546.42，crossover 桶 0.015。与 cluster-far 只差查询向量。
- `selectivity_cluster_partflat_curve_20261026.csv` / `.jsonl` / `summary_cluster_partflat_20261026.json`：SIFT100k cluster-far 的属性分区 flat 扫描（分区 k=128 seed 777，独立于谓词生成器；49.2 MB 分区副本），200/bucket、3 次重复，run #92；主指标 14,378.20 QPS，只在 s=0.3 略胜精确扫描。
- `selectivity_cluster_partgather_smoke_20261026.csv` / `summary_cluster_partgather_smoke_20261026.json`：同一分区布局但只 gather 可行点；10 条/桶 smoke，s≤0.1 仍慢于全局 exact list，s=0.3 快 26%。
- `selectivity_grid_curve_20261026.csv` / `summary_grid_20261026.json`：SIFT100k 随机谓词在 1%/5%/10%/15%/20%/25%/30% 的选择率网格，200/bucket、3 次重复、margin 0.01，run #93；主指标 6,066.30 QPS，交叉点约 0.065，作为动机实验。

## CSV 列

| 列 | 含义 |
|---|---|
| `mode` | `random`（均匀随机谓词）、`sub`（递归目录 `Inc(v)`）、`exact`（非递归 `Local(v)`） |
| `bucket` | 目标/实际选择率桶：0.001, 0.003, 0.01, 0.03, 0.1, 0.3, 1.0 |
| `method` | `exact`（位图枚举）、`exact_list`（posting list + prefetch）、`hnsw`（调参 ef）、`hnsw_fixed`（固定 ef 诊断）、`post`（后过滤） |
| `variant` | `precomputed`、`triehi`（inclusive 位图拷贝）、`peonline`（子树逐节点 OR）、`local`（Local 位图拷贝） |
| `ef` | HNSW 的 `efSearch`；`post` 行为初始 ef；`exact` 为 0 |
| `s` | 实际选择率 `|feasible|/n` 的均值 |
| `nq` | 评估半集的查询数（调参半集数量相同或略多） |
| `qps` | 评估半集吞吐（`nq / 查询时间`，单线程） |
| `recall` | 评估半集 Recall@10 |
| `recall_tune` | 调参半集 Recall@10（仅 `hnsw` 行；用于选 ef，不参与评估） |
| `p50_us`, `p95_us` | 每查询延迟分位数（含目录解析，若适用） |
| `resolve_us` | 平均目录解析时间（每查询） |
| `ok` | `recall>=0.95` 且无重复/不可行 ID |

## 复现

```bash
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --nq-per-bucket 200 --max-ef 16384
python3 production/nested-attr-vector-search/src/analyze_selectivity.py
python3 production/nested-attr-vector-search/src/plot_selectivity.py
```

`work/selectivity*/summary.json` 与 `work/selectivity*/selectivity.csv` 每次运行会覆盖；本目录保存的是报告对应的快照。1M 索引（625.9 MB）与 fixtures 在 `work/`，被 `.gitignore` 排除；用 `export_sift1m.py` 重建。

## 未持久化

- 100k SIFT 基础向量、HNSW 索引（62.6 MB）与层级 fixtures 在 `production/nested-attr-vector-search/work/`，被 `.gitignore` 排除；按 `prep_ccs.py` 与 harness 可重建。
- HNSW 构建时间 25.4 s 来自冷构建 run #80；本目录的 QPS 使用缓存索引（`build_s=0`）。
