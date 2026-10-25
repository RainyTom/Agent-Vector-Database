# 选择率对属性过滤索引效率的影响（SIFT100k + 1M）

日期：2026-10-26。方向：`01-directory-aware-acceleration` 的测量诊断，不是对 2026-10-19 停止独立立项结论的翻案。代码：`production/nested-attr-vector-search/src/{selectivity_bench.cpp,selectivity_harness.py,analyze_selectivity.py,plot_selectivity.py,plot_scale_compare.py,export_sift1m.py}`。协议：[`experiments/selectivity-benchmark.md`](experiments/selectivity-benchmark.md)；FAISS 接口证据：[`experiments/faiss-attribute-filter-support.md`](experiments/faiss-attribute-filter-support.md)。

## 1. 结论

1. **FAISS 没有属性过滤索引。** 它只提供 ID 级过滤钩子 `IDSelector` + `SearchParameters.sel`，由调用方先把属性条件编译成可行位图/ID 集合；Flat、IVF、HNSW（以及 PathSeer fork 的 ACORN/PathSeer）都消费这个钩子。HNSW 的 selector **过滤结果堆、不剪枝遍历**，所以固定 `efSearch` 下召回随选择率 `s` 下降。
2. **选择率把执行策略分成两段。** 在 SIFT100k 上，精确枚举可行 ID（posting list + prefetch）在 $s\le0.03$ 全面胜出；BF-HNSW 在 $s\ge0.1$ 胜出；交叉点约 $s^\*\approx0.06$（对数插值）。
3. **维持 r95 所需的 `efSearch` 近似按幂律增长**：拟合 $\mathrm{ef}\approx23.4\,s^{-0.774}$。从 $s=1$ 的 `ef=32` 到 $s=0.001$ 的 `ef=8192`，查询延迟从 57 µs 升到 27.7 ms（约 490×），而精确扫描在 $s=0.001$ 只需 5.1 µs。
4. **post-filter 在 100k 上从不是最优。** 它需要约 $k/s$ 的全局候选池；$s=0.001$ 时 10.2 QPS、p50 98 ms，比精确扫描慢约 1.9 万倍；$s=1$ 时召回只有 89.9%，连 r95 都达不到。1M 上 post 在 $s=0.1$ 首次略胜 HNSW（1,774 vs 1,605），但 $s=0.3/1$ 仍因召回不足不可用（§5）。
5. **表示方式本身就是选择率相关成本。** 同一可行集，按 posting list 枚举比按 12.5 KB 位图枚举快 2.5×（$s=0.001$）、1.9×（$s=0.01$）；差异主要来自冷内存随机 gather，而不是 popcount。
6. **目录解析成本与选择率近似无关**：TrieHI 式 inclusive 位图拷贝 0.7–1.6 µs；PE-Online 式子树的逐节点 OR 从 4 µs 增长到 933 µs（$s=0.3$），在高选择率下成为端到端主项。低选择率下解析只占精确扫描的 10–30%。
7. **交叉点随规模左移。** SIFT1M 上交叉点约 $s^\*\approx0.019$（100k 为 0.0605）。精确扫描从 100k 到 1M 慢 12–14×，HNSW 在 $s=1$ 只慢 3.8×；更大规模应更早切到精确枚举。1M 的 ef 前因子从 23.4 增到 50.6，指数仍约 $-0.67$。

## 2. 实验设置

| 项 | 设置 |
|---|---|
| 向量 | SIFT100k，`n=100000`，`d=128`，L2，1000 条真实查询向量 |
| 层级 | 1365 节点 CCS 形状树，1024 叶，每对象最多 5 个标签；30146 个对象无标签；合成校准，非真实 CCS |
| 谓词 | `random`：每查询均匀抽 `round(s·n)` 个 ID，$s\in\{0.001,0.003,0.01,0.03,0.1,0.3,1.0\}$；`sub`/`exact`：树节点的 `Inc(v)`/`Local(v)`，按实际 $s$ 分桶 |
| 查询数 | 每桶最多 200 条谓词，随机打乱后前 100 调参、后 100 评估；层级桶不足 200 时用全部 |
| 方法 | `exact`（位图枚举）、`exact_list`（posting list + prefetch 8）、`hnsw`（FAISS `IndexHNSWFlat` M=16, efc=200 + bitmap selector）、`post`（全局 top-ef 后筛） |
| ANN 参数 | `hnsw` 的 `ef` 在调参半集上取满足 Recall@10 ≥ 0.95 的最小值；`post` 初始 `ef=max(16,ceil(2k/s))`，不足时 ×4，上限 16384 |
| 硬件/线程 | i5-12600KF，WSL2，单线程查询，`OMP_NUM_THREADS=1`；索引构建一次并缓存 |
| 主指标 | `gmean_qps_r95`：七个 random 桶的 per-bucket 最优 gated QPS 的几何平均 |

**Oracle 与校验。** `exact_topk` 与 `faiss::IndexFlat + IDSelectorBitmap` 在 $s=0.01$ 和 $s=1$ 上逐 ID 对比通过；所有 `exact`/`exact_list` 行的评估召回必须 ≥0.999，否则 `valid=false`；selector 方法检查重复/不可行 ID；tune/eval 分半且按组随机打乱（先前顺序分半会让 SIFT 前 100 条查询偏易，导致 $s=1$ 的评估召回被人为压低 2.3pp）。

**规模与成本。** HNSW M=16 冷构建 25.4 s，索引 62.6 MB；层级 dense bitmap 元数据 32.5 MB（1365 节点 × 12.5 KB × 2 套 inclusive/local）。构建时间不计入 QPS。

## 3. 结果

### 3.1 吞吐 vs 选择率

![选择率曲线](figures/selectivity_curve.svg)

主结果（run #82，复现 run #81；两次主指标 22.43k / 21.84k QPS，差异 2.6%）：

| s | 精确位图 QPS | posting list QPS | list 增益 | BF-HNSW ef | BF-HNSW QPS | 召回 | post QPS | post 召回 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.001 | 76,982 | **192,313** | 2.50× | 8192 | 36.1 | 99.6% | 10.2 | 99.6% |
| 0.003 | 38,367 | **82,034** | 2.20× | 2048 | 302.8 | 98.4% | 49.8 | 100% |
| 0.01 | 13,266 | **21,483** | 1.89× | 512 | 1,412 | 95.9% | 299 | 100% |
| 0.03 | 5,363 | **7,861** | 1.39× | 256 | 2,754 | 97.7% | 1,022 | 99.9% |
| 0.1 | 1,947 | 2,356 | 1.27× | 128 | **4,988** | 98.2% | 3,146 | 99.4% |
| 0.3 | 568 | 785 | 1.44× | 64 | **9,965** | 96.4% | 7,570 | 96.7% |
| 1.0 | 457 | 438 | 1.07× | 32 | **17,894** | 96.2% | 20,610 | 89.9% ✗ |

- $s=0.001$：精确扫描 5.1 µs/query，BF-HNSW 27.7 ms/query（`ef=8192`）；差距约 5,400×。
- $s=0.1$：BF-HNSW 首次反超（4,988 vs 2,356 QPS）；$s=0.3$ 时约 12.7×；$s=1$ 时约 40.9×。
- post-filter 在 $s=0.1$ 仍略快于精确扫描（3,146 vs 2,356），但始终慢于 BF-HNSW；$s=1$ 因候选池只有 20 个而漏掉可行近邻，召回 89.9%。

**交叉点。** 对 $s=0.03$ 与 $s=0.1$ 的对数 QPS 做线性插值，精确扫描与 BF-HNSW 的交叉点约 $s^\*=0.0605$。这是本数据/索引参数下的值，不是普适常数。

### 3.2 固定 ef 的召回坍塌

| s | ef=16 | ef=64 | ef=256 | ef=1024 | ef=4096 |
|---:|---:|---:|---:|---:|---:|
| 0.001 | 2.8% | 8.6% | 25.9% | 58.4% | 94.4% |
| 0.003 | 7.1% | 20.7% | 53.9% | 91.6% | 99.9% |
| 0.01 | 18.3% | 46.8% | 85.3% | 99.9% | 100% |
| 0.03 | 37.2% | 74.1% | 97.7% | 100% | 100% |
| 0.1 | 55.1% | 92.7% | 99.8% | 100% | 100% |
| 0.3 | 71.9% | 96.4% | 100% | 100% | 100% |
| 1.0 | 87.5% | 99.2% | 100% | 100% | 100% |

固定 `ef=64` 时，召回从 $s=1$ 的 99.2% 掉到 $s=0.001$ 的 8.6%。这不是结果后过滤的排序问题，而是 HNSW 的 `efSearch` 预算按全部候选计数、可行候选只占其中约 `s` 的比例；要凑够 k 个可行近邻，候选预算需按 $1/s$ 放大。

拟合 $\mathrm{ef}\approx23.4\,s^{-0.774}$（七个点，log-log 最小二乘）。指数小于 1，说明在极低选择率下单纯放大 ef 的收益递减；此时精确枚举可行集更便宜。

### 3.3 目录解析成本

`sub`（递归目录范围）的解析成本与端到端：

| s | TrieHI 解析 µs | PE-Online 解析 µs | exact list QPS（预物化） | BF-HNSW ef | BF-HNSW QPS | 召回 |
|---:|---:|---:|---:|---:|---:|---:|
| 0.0011 | 1.59 | 5.10 | 173,199 | 8192 | 35.8 | 97.9% |
| 0.0027 | 1.57 | 10.16 | 72,110 | 4096 | 114.6 | 95.5% |
| 0.0070 | 1.23 | 18.48 | 37,743 | 1024 | 681 | 96.8% |
| 0.0261 | 1.14 | 70.46 | 9,326 | 512 | 1,452 | 99.7% |
| 0.1011 | 0.90 | 269.95 | 2,083 | 128 | 5,190 | 98.0% |
| 0.3142 | 0.80 | 933.11 | 755 | 128 | 4,482 | 100% |

- TrieHI 的 inclusive 位图拷贝几乎恒定（0.8–1.6 µs），因为代价是 12.5 KB 的拷贝，与 $s$ 无关。
- PE-Online 的逐子树 OR 随作用域节点数增长；$s=0.1/0.3$ 时解析成本（270/933 µs）与 BF-HNSW 搜索（约 200 µs）同量级或更高。端到端 QPS 从 5,190 掉到 861（$s=0.3$ 的 `peonline` 变体）。
- 低选择率下，TrieHI 解析只占精确扫描的 10–30%；但在 $s=0.001$ 的 5.3 µs 精确扫描上，1.6 µs 已不可忽略。若属性索引直接存 inclusive posting list，拷贝 100 个 ID 约 0.4 µs，可进一步降低。

`exact`（非递归 Local）的解析更便宜：Local 位图拷贝 0.6–1.6 µs；但 `exact` 作用域通常很小（$s\le0.1$），BF-HNSW 即使在 `ef=16384` 也常达不到 r95（$s=0.001$ 召回 92.9%），精确枚举更合适。

### 3.4 主指标与策略

- **可达前沿** `gmean_qps_r95`：**22,426 QPS**（run #81），复现 **21,839**（run #82）。
- **固定策略** `s<0.01 → exact_list；s≥0.01 → BF-HNSW`：**12,743 QPS**。它低于前沿，因为 $s=0.01/0.03$ 的最优仍是精确扫描；阈值应放在约 0.06，而不是 0.01。
- 按交叉点校正的策略（$s<0.06$ 精确、否则 BF-HNSW）预计接近前沿；未单独记录为指标，留作后续。

## 4. 对 Directory-Aware 方向的含义

1. **目录层不是瓶颈，但表示形式是。** TrieHI 的 inclusive 位图解析在 µs 级，选择率变化不改变它的量级；真正随 $s$ 变的是向量执行器。PE-Online 的子树展开在高 $s$ 才成为瓶颈（$s=0.3$ 时 933 µs）。
2. **“过滤索引效率”必须分三段计时**：谓词求值/位图解析、候选扫描/图遍历、距离计算。只报端到端会把低 $s$ 的冷 gather、高 $s$ 的目录展开混在一起。
3. **低选择率的正确执行器是可行集精确扫描，不是把 HNSW 的 ef 拉满。** 在 $s=0.01$，精确扫描 21.5k QPS，HNSW 需 `ef=512` 才 1.4k QPS；差距 15×。把 ef 拉到 4096 只会更慢（114 QPS）。
4. **属性索引应同时维护位图与 posting list。** 位图适合交并/高选择率过滤；posting list 适合低选择率枚举。两者不是互斥表示，而是按 $s$ 选择。本研究只测了查询侧成本，未计双表示的构建/更新/空间成本。
5. **不要用单一选择率调参。** FAISS HNSW 的 selector 是 filter-agnostic 的；跨 $s$ 使用固定 `ef` 会在低 $s$ 丢召回，在高 $s$ 浪费预算。至少要按 $s$ 估 `ef`，或在 $s$ 低于交叉点时切换执行器。

## 5. 1M 规模复测

为检验交叉点是否随 $n$ 漂移，在同一协议下把向量集扩到 SIFT1M（`n=1000000`，`d=128`，10k 条真实查询向量）。只跑 `random` 谓词；每桶 200 条（100 调参 / 100 评估），`max_ef=8192`。HNSW 仍为 M=16、efc=200；索引 625.9 MB，冷构建 499.8 s（查询 QPS 使用缓存索引，构建不计入）。层级/目录模式未在 1M 重跑，因为它们不是这组随机谓词实验的必要条件。

![100k 与 1M 选择率对比](figures/selectivity_scale_compare.svg)

| s | exact list QPS | HNSW@r95 QPS（ef） | 召回 | post QPS | post 召回 |
|---:|---:|---:|---:|---:|---:|
| 0.001 | **15,749** | —（需 ≥8192，未测到 gated 点） | 94.4% ✗ | 28 | 77.1% ✗ |
| 0.003 | **5,787** | 183（2048） | 98.4% | 38 | 99.9% |
| 0.01 | **1,760** | 412（1024） | 98.7% | 170 | 99.7% |
| 0.03 | 575 | **1,533**（256） | 95.1% | 519 | 99.7% |
| 0.1 | 163 | 1,605（256） | 99.0% | **1,774** | 98.1% |
| 0.3 | 62 | **2,846**（128） | 98.4% | 4,194 | 93.8% ✗ |
| 1.0 | 32 | **4,652**（64） | 96.7% | 9,614 | 82.8% ✗ |

- **交叉点**：$s^\*\approx0.0193$（0.01 与 0.03 对数插值；exact 1,759.7/575.2，HNSW 412/1,533）。100k 为 0.0605，左移约 3.1×。汇总里的 `crossover_s=0.015` 只是首个 HNSW 胜出桶的下界。
- **成本缩放**：$s=0.001$ 的 exact list 从 192k 降到 15.7k QPS（12.2×）；$s=1$ 从 438 降到 32（13.7×）。HNSW 在 $s=1$ 从 17.9k 降到 4.65k（3.8×）。索引 62.6→625.9 MB（10×），构建 25.4→499.8 s（19.7×）。
- **ef 缩放**：按每桶最小 gated ef 拟合，1M 为 $\mathrm{ef}\approx50.6\,s^{-0.665}$；100k 为 $23.4\,s^{-0.774}$。前因子约 2.2×，指数更平；低 $s$ 仍需更大的绝对 ef（$s=0.01$ 需 1024，$s=0.001$ 需 8192）。
- **post-filter 在中选择率变强**：$s=0.1$ 时 1,774 QPS，首次超过 HNSW 的 1,605；但 $s=0.3$ 召回 93.8%、$s=1$ 召回 82.8%，仍不能作为高 $s$ 方案。100k 上 post 从未赢过。
- **主指标**：`gmean_qps_r95=3,446`（run #85）；同一配置的上一轮 run #84 为 3,243，差异 6.3%。`gmean_policy_qps_r95=3,397`（策略 $s<0.02$ 用 exact list，否则 HNSW），已接近前沿。

**解释。** 精确扫描成本约按 $n$ 线性增长，且是冷内存 gather 主导；HNSW 查询成本随 $n$ 亚线性增长，但低 $s$ 需要更大 ef。因此 $n$ 增大时 exact 的相对劣势来得更早，交叉点左移。100k 的 0.06 阈值不能直接搬到 1M；1M 上应在约 0.02 附近切换。

**测量注意。** $s=0.001/0.01$ 的 HNSW 调参 ef 在评估半集分别只有 94.4%/93.2%，没有 gated 点；表中用固定 ef 网格里最小的 gated 点（$s=0.01$ 的 1024）替代，$s=0.001$ 的 8192 是下一格、未直接测到。$s=0.03$ 的 95.1% 贴线，重复运行 95.1% 仍通过，但该桶对采样敏感。1M 的 gmean 与 100k 不可直接比较，只比较趋势。

## 6. 局限

- **数据规模**：100k 与 1M SIFT、单一数据集；10M 未测。100k 有合成层级，1M 只有随机谓词；交叉点与拟合指数不能直接外推到 10M。
- **谓词相关性**：`random` 与几何独立；`sub`/`exact` 有部分语义相关性，但不是真实 CCS 标注。相同 $s$ 下，属性与向量的相关性会改变图的可导航性，本研究只覆盖了两端。
- **方法范围**：只测 FAISS `IndexHNSWFlat + sel`、精确扫描、post-filter；ACORN/PathSeer/NaviX 等属性感知图未进入本轮，不能据此评价它们。IVF 也未测。
- **单谓词**：未测多节点 AND/OR、否定、更新与并发。
- **表示成本**：posting list 的构建/更新/空间未计；dense bitmap 元数据 32.5 MB 是简化表示，Roaring 会更小。
- **测量**：单线程、WSL2；HNSW 构建后立即测量的首轮会受热降频影响，本报告使用缓存索引的 100k run #81/#82 与 1M run #85。固定 ef 行只用于诊断，不参与 ef 选择。
- **调参/评估**：ef 在 100 条调参谓词上选，评估用另外 100 条；样本量仍有限，$s=0.01$ 的 HNSW 评估召回 95.9% 贴近门槛。前沿指标按评估结果选每桶最优方法，是“可达前沿”，不是线上策略。1M 上 $s=0.001/0.01$ 的 HNSW 调参 ef 在评估半集未达 r95；表中用固定 ef 网格的最小 gated 点，$s=0.001$ 的 8192 为推断的下一格。1M 只测 random 谓词，未重跑层级/目录模式；1M 的 HNSW 仍只有 M=16 一个构型。

## 7. 复现

```bash
# 当前 segment（1M）：bash autoresearch.sh 等价于下面的 1M 命令
bash autoresearch.sh

# SIFT100k 复测
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out-dir production/nested-attr-vector-search/work/selectivity \
  --nq-per-bucket 200 --max-ef 16384 --policy-s 0.06

# 表格与图
python3 production/nested-attr-vector-search/src/analyze_selectivity.py
python3 production/nested-attr-vector-search/src/plot_selectivity.py

# 1M 规模复测（h5py 使用 nested-attr work venv）
production/nested-attr-vector-search/work/venv/bin/python \
  production/nested-attr-vector-search/src/export_sift1m.py
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_1m \
  --out-dir production/nested-attr-vector-search/work/selectivity_1m \
  --modes random --nq-per-bucket 200 --max-ef 8192 --policy-s 0.02
python3 production/nested-attr-vector-search/src/plot_scale_compare.py
```

原始数据：100k 为 [`data/selectivity_curve_20261026.csv`](data/selectivity_curve_20261026.csv)、[`data/summary_20261026.json`](data/summary_20261026.json)、[`data/selectivity_20261026.jsonl`](data/selectivity_20261026.jsonl)（run #82，复现 run #81）；1M 为 [`data/selectivity_1m_curve_20261026.csv`](data/selectivity_1m_curve_20261026.csv)、[`data/summary_1m_20261026.json`](data/summary_1m_20261026.json)、[`data/selectivity_1m_20261026.jsonl`](data/selectivity_1m_20261026.jsonl)（run #85，复现 run #84）。

## 8. 下一步

1. 在 1M/10M 上加入层级与目录模式，确认目录解析成本在高 $n$ 下是否仍可忽略。
2. 加入属性-几何相关谓词（k-means 簇/邻域），分离“选择率”与“相关性”的贡献。
3. 加入 IVF 与 ACORN/PathSeer 基线，检验 filter-aware 图能否把交叉点推向更低 $s$。
4. 实现“位图 + posting list”双表示与按 $s$ 的切换策略，计入构建/更新/空间成本。
5. 把目录解析与向量执行器的计时拆成稳定的三段，报告 p50/p95 与内存带宽。
