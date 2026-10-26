# 动机实验：选择率对 FAISS 过滤 HNSW 效率的影响

日期：2026-10-26。目的：为 Directory-Aware 方向提供选择率成本的直接证据。问题不是“FAISS 能不能过滤”，而是“过滤后的向量搜索效率如何随选择率变化”。

## 结论

1. **FAISS 没有属性过滤索引。** 核心 FAISS 只有 `IDSelector`/`SearchParameters.sel`；调用方先把属性条件编译成可行位图，再交给 Flat/IVF/HNSW。HNSW 的 selector 只过滤结果堆、不剪枝遍历，所以固定 `efSearch` 的召回随选择率 $s$ 下降。
2. **效率在选择率上分成两段。** SIFT100k 上，精确枚举可行 ID 在 $s=1\%$/$5\%$ 时仍是 BF-HNSW 的 14.3×/1.6×；从 $s=10\%$ 起 BF-HNSW 反超，交叉点约 $s^*\approx0.065$。
3. **固定 ef 的召回在低 $s$ 崩塌。** $ef=64$ 在 $s=30\%$ 时 Recall@10=97.3%，在 $s=1\%$ 只有 44.1%。要维持 r95，必须按 $s$ 放大 `ef`，本数据上 $\mathrm{ef}\approx35.9\,s^{-0.60}$。
4. **post-filter 只在高 $s$ 接近 BF-HNSW，不能替代精确枚举。** $s=1\%$ 时 post 需要把全局候选池扩到 2000 才凑够 10 个可行点，QPS 223；$s=30\%$ 时 post 7,354 QPS，接近 HNSW 的 7,718，但召回只有 97.5%，更脆弱。

## 实验设置

- 向量：SIFT100k，$n=100000$，$d=128$，L2，1000 条真实查询向量。
- 谓词：每查询独立随机选取 `round(s·n)` 个 ID，$s\in\{1\%,5\%,10\%,15\%,20\%,25\%,30\%\}$；每桶 200 条，随机打乱后前 100 调参、后 100 评估，3 次评估取中位数。
- 索引：FAISS `IndexHNSWFlat` M=16, efc=200，62.6 MB，缓存加载；`exact` 为可行集精确枚举（posting list + prefetch），`post` 为全局 top-`ef` 后筛。
- 目标：Recall@10 ≥ 0.95；`ef` 在调参半集上取最小达标值（目标带 1pp margin）。
- Oracle：`exact_topk` 与 `faiss::IndexFlat + IDSelectorBitmap` 在 $s=0.01$ 和 $s=1.0$ 上逐 ID 对比通过。
- 环境：单线程，WSL2，`OMP_NUM_THREADS=1`。

## 结果

![选择率动机曲线](../figures/selectivity_motivation.svg)

| 选择率 | exact list QPS | BF-HNSW QPS（ef） | Recall@10 | post-filter QPS | post Recall |
|---:|---:|---:|---:|---:|---:|
| 1% | **18,443** | 1,294（512） | 95.5% | 223 | 100% |
| 5% | **3,958** | 2,523（256） | 99.7% | 1,517 | 100% |
| 10% | 1,981 | **4,148（128）** | 98.3% | 3,114 | 99.4% |
| 15% | 1,219 | **4,523（128）** | 98.8% | 3,211 | 98.8% |
| 20% | 819 | **4,706（128）** | 99.6% | 4,551 | 99.0% |
| 25% | 415 | **6,077（64）** | 97.3% | 5,176 | 98.8% |
| 30% | 473 | **7,718（64）** | 97.3% | 7,354 | 97.5% |

固定 `ef` 的 Recall@10：

| s | ef=16 | ef=64 | ef=256 | ef=1024 |
|---:|---:|---:|---:|---:|
| 1% | 18.6% | 44.1% | 83.7% | 99.2% |
| 5% | 42.7% | 83.5% | 99.7% | 99.9% |
| 10% | 56.6% | 91.2% | 99.5% | 100% |
| 15% | 66.1% | 94.7% | 99.6% | 100% |
| 20% | 69.5% | 96.3% | 100% | 100% |
| 25% | 73.3% | 97.3% | 100% | 100% |
| 30% | 74.6% | 97.3% | 99.9% | 100% |

主指标 `gmean_qps_r95=6,066`；固定策略 `s<0.06 → exact`、`s≥0.06 → BF-HNSW` 在该网格上已达同一前沿（`gmean_policy_qps_r95=6,066`）。

## 解释

FAISS 的 `IDSelector` 是 filter-agnostic：HNSW 仍遍历不可行节点，只是不把它们放入结果堆；`efSearch` 的预算按全部候选计数。选择率低时，为了凑够 10 个可行近邻，`efSearch` 必须约按 $1/s$ 放大，延迟从几十微秒升到几毫秒；此时直接枚举可行 ID 做精确扫描更便宜。选择率高时，可行点遍布全图，BF-HNSW 用很小的 `ef` 就能达标，精确扫描反而要遍历大量点。

因此，一个基于全局向量索引的属性过滤系统不能忽略选择率：要么在低 $s$ 切换到可行集精确扫描，要么让属性索引把可行集显式枚举出来。Directory-Aware 的设计目标正是把属性条件编译成可行的执行器选择，而不是把过滤简单下推给全局 HNSW。

## 局限

- 谓词与几何独立（随机子集）；真实属性通常与向量分布相关，相关方向会进一步改变结果（见同目录 cluster-far/cluster-near 结果）。
- 只测 SIFT100k、单线程、FAISS HNSW M=16/efc=200；未测 IVF/ACORN/PathSeer。
- 精确枚举是下界，不是 ANN；交叉点数值只对本数据/硬件有效。
- 调参/评估分半，样本量有限；$s=1\%$ 的 HNSW 评估召回 95.5% 贴近门槛。

## 复现

```bash
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity_grid \
  --modes random --metric-mode random --nq-per-bucket 200 --max-ef 16384 \
  --policy-s 0.06 --tune-margin 0.01 --repeats 3

python3 production/nested-attr-vector-search/src/plot_selectivity_motivation.py
```

数据：[`data/selectivity_grid_curve_20261026.csv`](../data/selectivity_grid_curve_20261026.csv)、[`data/summary_grid_20261026.json`](../data/summary_grid_20261026.json)。
