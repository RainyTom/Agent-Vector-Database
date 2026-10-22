# 研究日志 2026-10-22：语义 episode 检索首轮基准

方向：[语义时空混合向量搜索](../index.md)。本日志只记录已运行的实验；未运行的内容写在“下一步”，不作为结论。

## 0. 当日目标

1. 关闭层级/前缀/vector join 三条旧线，保留证据；
2. 用真实 mobility 数据建立“时间窗约束的语义 episode 检索”基准；
3. 测全扫描、类别倒排、整轨迹均值 kNN 的延迟/召回，判断问题是否成立；
4. 决定是否值得实现序列 sketch 索引，以及需要什么规模的数据。

## 1. 数据与预处理

数据：Massive-STEPS New York（SIGSPATIAL 2026，[arXiv:2505.11239](https://arxiv.org/abs/2505.11239)），CSV 字段含用户、POI、经纬度、类别、名称、时间戳。

- 原始 272,368 行，6,929 用户，92,041 trail，45,804 POI，585 类别；
- 丢弃 19,834 行缺少经纬度/名称/类别的记录，保留 252,534 行；
- 用户级轨迹按时间排序；长度 mean 36.4 / median 18 / max 2,689；
- 时间跨度 2012-04 至 2018-10，包含 2013 与 2018 两段。

Embedding（pilot，可复现，非神经模型）：

- 64 维：类别转移共现矩阵的 PPMI + truncated SVD（579 类）；
- 64 维：venue name 的 signed hashed bag-of-words；
- 拼接后 L2 归一化，$d=128$。

这是一个**基线表示**，不是“最佳语义模型”。它的作用是让检索协议可运行；后续换文本/多模态编码器时，查询与索引协议不变。绝对延迟和召回会随编码器变化，但三种执行方式的相对趋势是当前观察对象。

## 2. 查询与 oracle

- 从用户轨迹中采样连续 episode，长度 $m\in[3,6]$，同一日历日内完成；
- 查询窗口 $W=[t_a,t_a+\text{window}]$，window 取 24h/168h/720h/2160h/8760h；
- 持续时间容差 $\max(30\text{min},25\%)$；
- episode 长度带 $[m-2,m+2]$（pilot 限制；见局限）；
- 空间约束：本轮无（bbox 为全球）；后续加 2km/5km；
- 每条查询排除源用户，要求精确 top-10 满 10 条才计入有效查询；
- 距离：DTW，$\lambda=0$，按 $\max(m,n)$ 归一化；
- 精确 oracle 由全用户扫描 + 可行 episode 枚举得到。

## 3. 方法

| 方法 | 说明 |
|---|---|
| Exact scan | 对每个用户二分定位时间窗内的点，枚举可行 episode，算 DTW，取 top-10 |
| Inverted | 取查询中出现过的类别，合并包含这些类别的用户，再做同样的 episode 验证 |
| Mean kNN | 每条用户轨迹求 embedding 均值，按余弦取 top-200，再做 episode 验证 |

三种方法使用同一 oracle 定义与同一查询；Inverted/Mean 的 Recall@10 以 Exact 的 top-10 用户集合为分母。所有方法单线程。

## 4. 结果

原始逐查询数据：[`data/ny_*_results.csv`](data/)；汇总：[`data/summary.csv`](data/summary.csv)。

| 窗口 | 有效查询 | Exact mean / p95 (ms) | Exact 候选用户 | Exact DTW/查询 | Inverted mean (ms) | Inverted Recall@10 | Mean-kNN mean (ms) | Mean-kNN Recall@10 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 24h | 440/500 | 0.92 / 1.60 | 377 | 242 | 0.52 | 0.693 | 0.61 | 0.137 |
| 1w | 498/500 | 1.76 / 3.90 | 979 | 1,558 | 1.07 | 0.743 | 0.65 | 0.203 |
| 1mo | 499/500 | 4.52 / 11.68 | 1,858 | 5,726 | 2.95 | 0.786 | 0.82 | 0.236 |
| 1q | 499/500 | 8.55 / 23.30 | 2,680 | 12,003 | 5.63 | 0.822 | 1.08 | 0.254 |
| 1y | 499/500 | 16.98 / 44.45 | 4,016 | 26,460 | 11.14 | 0.850 | 1.59 | 0.282 |

> 注：此表在修复用户 ID 映射后重新生成；与修复前的旧表数值略有差异，以本表为准。Inverted/Mean 的 Recall 相对 Exact top-10。

观察：

1. **时间窗是主成本驱动**：Exact 的 DTW 次数从 24h 的 242 增到 1y 的 26,460（约 109×），延迟从 0.92 ms 增到 16.98 ms（约 18×）。候选用户数从 377 增到 4,016，但 DTW 增长更快，说明长窗口下每个用户的可行 episode 数增加。
2. **小规模下全扫描已经很快**：6,929 用户、1 年窗口的 p95 仍只有 44 ms；24h 窗口 p95 1.6 ms。按用户数线性外推到 8 万用户，1 年窗口 mean 约 200 ms、p95 约 500 ms；这才进入需要索引的区间。当前 NY 单城不足以证明索引收益。
3. **类别倒排不是免费加速**：它把延迟降低约 35–40%，但 Recall@10 只有 0.693–0.850；24h 窗口最差查询只有 0.1。语义相似不要求类别相同（如 Café vs Coffee Shop、Park vs Garden），纯类别过滤会漏掉跨类别匹配。
4. **整轨迹均值 kNN 很快但召回很低**：0.137–0.282。把一条长轨迹压成一个均值向量，丢失了 episode 的位置与顺序；这直接支持“需要序列级表示/索引”，而不是再做一种整轨迹 ANN。
5. **窗口越大越容易凑满 top-10**：24h 有 60/500 查询不足 10 条；1y 只有 1/500。评估时必须区分“召回”与“欠填”，不能把空结果算作正确。

## 4.1 多城市规模（6 城，1 年窗）

合并 New York、Istanbul、Jakarta、Petaling Jaya、São Paulo、Moscow 的 Massive-STEPS CSV：

- 63,084 用户，1,838,793 个有效签到点，724 个类别；
- 620,025 行因缺少经纬度/名称/类别被丢弃（原始 2,097,813 行）；
- 500 条 episode 查询，window=8760h，无空间约束，精确 top-10 满 10 条全部有效。

| 方法 | mean / p50 / p95 (ms) | 候选用户 | DTW/查询 | Recall@10 |
|---|---:|---:|---:|---:|
| Exact scan | 99.58 / 90.51 / 253.26 | 37,080 | 177,977 | 1.000 |
| Category inverted | 60.43 / 51.11 / 150.21 | 15,498 | 102,602 | 0.852 |
| Mean-kNN (top-200) | 6.39 / 6.13 / 7.87 | 200 | 853 | 0.186 |

原始数据：[`data/all6_8760h_results.csv`](data/all6_8760h_results.csv)、[`data/all6_8760h_summary.json`](data/all6_8760h_summary.json)。总览图：[`figures/pilot-overview.png`](figures/pilot-overview.png)。

## 4.2 时间分月点级 ANN + DTW 验证

方法：按 UTC 月建 HNSW（M=16，efConstruction=100），查询时只搜索与 $W$ 重叠的月份；每个查询点取 top-N 近邻，按用户聚合（保留每个查询点的最小距离），用距离和作为 DTW 下界排序；取前 C 个用户做精确 episode 验证。

| 配置 | mean / p95 (ms) | 生成 / 验证 (ms) | Recall@10 | 说明 |
|---|---:|---:|---:|---|
| topn=100, C=2000 | 19.47 / — | 4.98 / 14.48 | 0.8804 | 2.0x，未达门槛 |
| topn=300, C=2000 | 31.06 / — | 12.72 / 18.34 | 0.9250 | 3.1x，未达门槛 |
| topn=500, C=3000 | **43.92 / 89.26** | 18.61 / 25.30 | **0.9576** | **2.16x，达到 0.95 门槛** |
| topn=400, C=2500 | 37.54 / — | 15.58 / 21.96 | 0.9440 | 2.5x，略低于门槛 |
| 按 (user, day) 分组, topn=100, C=2000 | 16.24 / — | 5.82 / 10.42 | 0.8624 | 更快但召回更低；跨日 episode 被排除 |

**结果**：topn=500/C=3000 在 63k 用户、1 年窗口上达到 Recall@10 0.9576，mean 43.9 ms、p95 89.3 ms；相对精确扫描 99.6 ms/253 ms，mean 2.16x、p95 2.84x。索引构建 36.5 s，1.84M 点（HNSW 未持久化，构建时间每次计入但查询 qps 不计构建）。

原始数据：[`data/all6_ann_topn500_results.csv`](data/all6_ann_topn500_results.csv)、[`data/all6_ann_topn500_summary.json`](data/all6_ann_topn500_summary.json)。

**风险**：召回只比门槛高 0.76 个百分点；逐查询 Recall 最低 0.4，说明尾部查询仍会失败。该结果只能说明“点级 ANN + 精确验证”是一个可用的强基线，不能证明新索引已经优于它。

## 4.3 精确下界剪枝（负结果）

尝试对每个用户计算 $LB_u=\sum_j\min_i d(q_j,p_i)/(m+2)$，按 LB 升序验证并在 $LB_u\ge$ 当前第 k 名分数时停止。理论上该 LB 是归一化 DTW 的下界，但实现后：

- 6 城、1 年窗：LB 方法 220 ms/查询，精确扫描 94 ms/查询；`same_topk` 438/500。
- NY、1 年窗：LB 方法 36 ms，精确扫描 16.6 ms；`same_topk` 487/499。

结论：LB 计算本身（对所有用户、所有窗口内点）比精确 episode 枚举更贵；且当前实现的 top-k 结果与精确扫描不一致（去重后仍有差异），不能作为精确方法使用。该路径暂时放弃；若要继续，需要更便宜的预过滤或更强的初始阈值。

## 5. 结论

- NY 单城（6.9k 用户）上时间窗过滤 + 全扫描仍很快（1 年窗 mean 17 ms）；单城不足以证明索引收益。
- 扩到 6 城（63k 用户、1.84M 点）后，精确扫描 1 年窗 mean 99.6 ms、p95 253 ms；问题成立。
- 类别倒排降低延迟约 39% 但 Recall 只有 0.852；整轨迹均值 kNN 只有 0.186 召回。两者分别暴露“过滤太硬”和“表示太粗”的失败模式。
- 时间分月点级 ANN + 精确 DTW 验证在 topn=500/C=3000 达到 Recall@10 0.9576、mean 43.9 ms、p95 89.3 ms，相对精确扫描 mean 2.16x、p95 2.84x。**这是当前达到门槛的最好结果**，但只比门槛高 0.76pp，且尾部查询 Recall 可低至 0.4。
- 该结果是一个强基线，不是最终方法：它没有利用 episode 的顺序/段结构，候选生成仍是单点 ANN + 用户聚合；下一步的序列 sketch 索引必须同时提高召回余量和降低 p95，才能构成独立贡献。

## 6. 局限

1. **合成/派生查询**：查询从数据中采样，不是领域用户真实查询；可能存在“查询与候选同分布”的乐观偏差。
2. **Pilot embedding**：PPMI-SVD + name hash 不是神经文本编码器；类别共现可能放大类别相似、低估细粒度语义。绝对 Recall 不可外推到生产 embedding。
3. **Episode 长度带**：限制 $[m-2,m+2]$ 是为了控制 oracle 成本；更长 episode 可能改变排序。该限制是当前查询定义的一部分，需在论文中明确或后续放宽。
4. **单线程、单机、内存驻留**：没有并发、没有 SSD、没有更新成本；不能与向量数据库的吞吐直接比较。
5. **时间语义**：把 naive 时间戳当 UTC；跨时区/夏令时未处理。Massive-STEPS 的城市本地时间可能受 DST 影响。
6. **空间约束缺省**：本轮 bbox 为全球；加 2km/5km 后候选与延迟会下降，但也可能让更多查询欠填。
7. **有效性筛选**：只保留精确 top-10 满 10 条的查询；这会排除稀疏日期/小城市，改变 workload 分布。

## 7. 下一步

1. **序列 sketch 索引**：按用户-时间窗构造段级均值/首尾/类别 sketch，先用暴力 sketch 距离测候选召回与剪枝率，再建 HNSW；目标是在 Recall@10 ≥0.97 时把 p95 降到 40 ms 以下。
2. **提高 ANN 召回余量**：当前 0.9576 只高门槛 0.76pp；需要分析 0.4 召回的尾部查询，判断是候选缺失还是验证排序问题。可尝试 per-query adaptive topn、按时间桶分层、或 query-point 加权。
3. **空间索引**：按城市/网格裁剪用户，分离“空间过滤收益”与“序列剪枝收益”；当前无空间约束，p95 被全球候选拉高。
4. **替换 embedding**：用文本编码器（sentence-transformers 或多模态模型）对 venue name + category 编码；当前 PPMI-SVD + name hash 只是可复现基线。
5. **真实查询**：与 mobility/urban 研究者收集 5–10 条查询，检查当前定义缺了哪些约束；用户可协助联系合作方与下载 Yelp/Foursquare 数据。

## 8. 复现命令

```bash
# 数据下载见 ../datasets.md
python3 production/semantic-mobility-search/src/prep.py \
  --csv datasets/massive-steps/ny_checkins.csv \
  --out production/semantic-mobility-search/work/ny_24h \
  --queries 500 --window-hours 24 --spatial-km 0

g++ -O3 -march=native -std=c++17 \
  production/semantic-mobility-search/src/scan.cpp \
  -o production/semantic-mobility-search/work/scan

production/semantic-mobility-search/work/scan \
  --dir production/semantic-mobility-search/work/ny_24h --k 10 --mean-n 200
```

窗口替换为 168/720/2160/8760 小时。`prep.py` 的随机种子固定为 20261022；`scan` 不引入随机性。

多城市 1 年窗与 ANN 基线：

```bash
python3 production/semantic-mobility-search/src/prep.py \
  --csv datasets/massive-steps/ny_checkins.csv --city ny \
  --csv datasets/massive-steps/istanbul_checkins.csv --city istanbul \
  --csv datasets/massive-steps/jakarta_checkins.csv --city jakarta \
  --csv datasets/massive-steps/petaling_jaya_checkins.csv --city petaling_jaya \
  --csv datasets/massive-steps/sao_paulo_checkins.csv --city sao_paulo \
  --csv datasets/massive-steps/moscow_checkins.csv --city moscow \
  --out production/semantic-mobility-search/work/all6_8760h \
  --queries 500 --window-hours 8760 --spatial-km 0

g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib \
  production/semantic-mobility-search/src/ann.cpp \
  -o production/semantic-mobility-search/work/ann

production/semantic-mobility-search/work/ann \
  --dir production/semantic-mobility-search/work/all6_8760h \
  --k 10 --topn 500 --cand 3000 --ef 150 --threads 4
```

`ann` 的 jitter 仅用于绕开当前 hnswlib 版本在大量重复 POI embedding 上的崩溃；幅度 0.01（8 维），不改变语义排序。
