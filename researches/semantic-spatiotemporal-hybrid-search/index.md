# 语义时空混合向量搜索

更新日期：2026-10-23（Asia/Shanghai）。

> **状态：新主线，探索阶段。** 2026-10-22 关闭[层级属性检索](../nested-attr-vector-search/index.md)，暂停[前缀包含](../containment-filtered-vector-search/index.md)与[vector join](../vector-join/roadmap.md)。本方向以高维向量检索为主线，研究带真实语义的时空属性（地点、轨迹、时间戳）如何改变混合搜索的问题定义、索引与执行。

## 新颖性状态

**当前更接近“constrained semantic TRSSQ”，不能再称为尚无输出前序的新问题。** [新颖性审计](novelty-audit.md)已确认：TRSSQ（ICDE 2024）定义“每条轨迹的最佳连续子轨迹 → 全局 top-k”，并用 RSSE（Bi-GRU + 点级 attention）做学习型过滤，全文已核；GTRSS 只是其图索引后继。TISIS* 覆盖 POI embedding 序列匹配，ST2Vec 覆盖时间感知完整轨迹相似度，Quantum Range Query 覆盖轨迹段上的时空混合范围查询。TRSSQ 已核为无时间/空间/语义约束，因此尚未核到的仍是“稠密语义序列 + 绝对时间窗/持续时间 + 空间硬约束”的同一实现。下一步必须对比 `hard filter → TRSSQ`；只有硬约束导致新的算法结构或显著收益，独立贡献才成立。

## 应用背景

本方向不以“合成 workload 上的索引调参”为目标。第一轮问题对应以下真实需求：

1. **交通/出行行为分析（主场景）**：分析者给出一个出行 episode（例如早高峰“地铁 → 咖啡 → 办公室”），希望在同城、可比季节/时间窗内找其他用户的相似 episode，用于出行模式对比、需求建模和异常出行发现。返回 episode 的具体日期/时段是分析可追溯性的前提，不能只给一个相似度分数。
2. **行程/活动推荐**：用户给出当前或历史行程片段，系统检索相似的历史活动序列，用于行程补全、旅游路线推荐和“像我这样的人还去了哪里”。
3. **共享出行/路线匹配**：给定司机或乘客的时空轨迹片段，检索相似路线与时间窗，用于拼车匹配、路线偏好分析和 ETA 解释。
4. **公共卫生/应急（仅聚合或授权数据）**：在合规前提下，检索与病例活动片段时空重叠的轨迹，辅助接触风险分析；当前工作不处理隐私与授权，明确不能直接用于个人数据。

当前基准用 Massive-STEPS 签到轨迹作为**代理数据**：它提供真实地点、类别、时间戳和用户轨迹，但采样频率低于 GPS/公交刷卡/网约车轨迹。50 km bbox 与 1 年窗是压力测试参数，不是领域用户给出的真实窗口；下一轮必须用领域查询日志或访谈确定的窗口/粒度替换。

## 为什么换问题

本方向不是从过滤 ANN 的“能力缺口”推导出来的，而是从 mobility 用户的问题单位出发：

- 单对象检索问“哪个 POI、文档或商品与查询向量最相似”；
- 本问题问“哪条轨迹中的哪一段连续行程与查询 episode 相似”。

地点有空间度量与层级，轨迹是有序可变长序列，时间戳决定先后、持续时间与区间可行性。把它们压成对象级 `lat/lon/timestamp` 谓词会改变查询语义。ACORN、Filtered-DiskANN、CubeGraph、LIST、TANNS、TANGO 只作为候选生成、过滤或分区机制的参考，不作为本问题的新颖性对照。

## 第一轮问题：时间窗约束的语义 episode 检索

给定长轨迹（用户在城市中的签到序列）和一条短查询 episode（若干带相对时间的语义停留点），在指定绝对时间窗与空间范围内，找 top-k 条轨迹中包含的、语义序列最相似的连续 episode。形式定义见[问题定义](problem-definition.md)。

与最接近工作的差异（详见[前序工作](prior-work.md)与[场景化问题对照](problem-comparison.md)）：

| 工作 | 用户问题 | 返回 | 已覆盖本问题的部分 | 剩余差异 |
|---|---|---|---|---|
| **TRSSQ** (ICDE 2024) | 每条长轨迹中哪段最像查询？ | 每轨迹代表子轨迹 top-k；RSSE 学习型过滤 | 连续片段、去重、top-k、区间 | 已核：无时间/空间/语义约束 |
| **GTRSS** (2025) | 同上 | 每轨迹最佳子轨迹，再取 top-k（图索引） | 同上，另加图索引 | 无 POI/text 语义与硬窗口 |
| ETQ | 哪些轨迹包含这些地点和活动？ | top-k 完整轨迹 | 语义点序列到 top-k | 匹配点可分散；无 episode 可行性 |
| TISIS* | 哪些完整 POI 序列足够相似？ | 阈值内完整轨迹 | Word2Vec POI 匹配 + LCSS | 非 top-k；不定位 episode |
| ST2Vec | 哪些完整路线在空间和时间节奏上相似？ | 完整轨迹 embedding/top-k | 时间进入软相似度 | 无语义 episode 与硬窗口 |
| Quantum Range Query | 哪些轨迹段落入给定时空窗口？ | 全部精确匹配段/量子叠加态 | 轨迹+时间戳的混合范围访问 | range reporting，非相似度 top-k；semantic 未实测 |
| REPOSE / N-tree | 哪些完整轨迹最近或落入距离范围？ | 完整轨迹 | 分布式 top-k / exact metric index | 不做连续 episode |

ACORN 等单对象过滤检索仍排除出问题新颖性审计。当前候选增量只有：**在绝对时间窗、持续时间和空间范围硬可行域内，按稠密语义序列距离选择 representative subtrajectory，并联合执行约束与相似度搜索。** 这必须相对 GTRSS/TRSSQ + filter 证明，而不是相对 filtered ANN 的表面差异。

## 首轮结果（2026-10-22）

6 城 Massive-STEPS（63,084 用户、1,838,793 点）、1 年时间窗、50 km 空间约束、500 条 episode 查询、Recall@10 门槛 0.95：

| 方法 | qps | Recall@10 | p95 |
|---|---:|---:|---:|
| 精确扫描 | 29.1 | 1.000 | 66.0 ms |
| 全局季度 HNSW + 验证 | 49.69 | 0.9678 | 40.3 ms |
| **城市+季度 HNSW + 验证（topn=300, cand=1250, ef=80）** | **93.25** | **0.9522** | **24.5 ms** |

相对精确扫描 3.2x；三次重复 92.87/93.15/93.25 qps。应用型 90 天“同季节”窗口（topn=600, cand=2500）为 142.84 qps、Recall 0.9856、p95 13.8 ms。完整设置、消融与负结果见[首轮报告](2026-10-22-exps/report.md)。**限制**：召回余量仅 0.22pp；50 km bbox 仍粗；embedding 是可复现的 PPMI-SVD + name hash，不是神经文本模型；查询为采样而非真实用户查询。下一轮必须补更细的空间分区、真实 embedding 与真实查询，才能判断这是否构成独立方法贡献。

## 研究路线

0. **领域验证前置（进行中）**：先用 5–10 个真实出行/分析查询验证 episode 定义、时间窗/空间粒度与返回字段；如果领域用户只需要整轨迹相似度或单一分数，就收缩问题，不继续堆索引。详见[RQ4](research-questions.md)。
1. **基准与瓶颈测量（已完成首轮）**：用 Massive-STEPS 真实签到轨迹构造 episode 查询与精确 oracle，测全扫描 DTW、时间/空间过滤、倒排索引、整轨迹表示 kNN 的延迟与候选规模。首轮结论见[报告](2026-10-22-exps/report.md)。
2. **直接基线与瓶颈测量**：补 `hard filter → exact/approximate TRSSQ`、GTRSS/TRSSQ 适配、ETQ-embedding 与 TISIS*-LCSS；在相同语义距离、硬可行性和召回定义下对比候选规模、延迟与内存。
3. **执行计划**：在轨迹谓词、时间窗、空间范围与向量 sketch 之间做代价选择；对齐召回定义与预算。
4. **跨领域验证**：与 mobility/urban 数据或研究问题结合，验证查询是否有实际用途，而不是只做合成 workload。

## 当前文件

- [问题定义（v3：不相交轨迹段划分）](problem-definition.md)
- [问题定义对照（本问题 vs 现有论文）](problem-comparison.md)
- [新颖性审计](novelty-audit.md)
- [前序工作与证据边界](prior-work.md)
- [研究问题与最小实验](research-questions.md)
- [检索日志](search-log.md)
- [数据集与下载状态](datasets.md)
- [2026-10-22 首轮实验](2026-10-22-exps/report.md)
- [2026-10-23 问题对照范围纠正](2026-10-23-exps/report.md)
- [TRSSQ (ICDE 2024) 全文记录](../../papers/trajectory-search/10.1109-icde60146.2024.00335-trssq/)
- [TRSSQ 获取与核对记录](evidence/trssq-2026-10-23.md)
- 代码：`production/semantic-mobility-search/`

## 边界

- 不把“语义谓词判定”与“几何距离阈值”混为一谈；本方向的相似度是 embedding 空间中的序列距离，不是 LLM 判定。
- 不把“每条查询返回 k 个近邻”与“全局 top-k pairs/join”混为一谈；episode 检索是单查询 top-k，不是 join。
- 时间窗、空间范围与 episode 持续时间是硬约束；近似只放宽距离排序，不放宽可行性。
- 未精读的论文只写摘要级证据；未运行的实验不写结果。
