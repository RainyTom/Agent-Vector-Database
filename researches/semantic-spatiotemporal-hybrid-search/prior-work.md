# 前序工作与证据边界

更新日期：2026-10-23。本文只记录已核对到摘要或正文片段的工作；未精读全文的条目明确标注。`未核实` 表示书目/版本/实验尚未核对，`未提供` 表示原文没有该内容。

## 1. 单对象过滤/时空检索：执行机制参考，不参与问题新颖性对照

本节工作检索的是单个向量或对象，与“查询 episode → 返回轨迹及 episode 区间”不是同一问题。保留它们只为追踪可迁移的过滤图搜索、分区与路由机制；不把它们列为直接相关工作或问题级基线。

| 工作 | 问题 | 与本方向的关系 | 证据状态 |
|---|---|---|---|
| ACORN, PACMMOD 2024, [arXiv:2403.04871](https://arxiv.org/abs/2403.04871) | 向量 + 任意结构化谓词；谓词无关的图搜索 | 属性是位图/谓词，不表达顺序、持续时间或轨迹 | 本项目已有全文与复现笔记（`papers/filtered-ann/2403.04871-acorn`） |
| Filtered-DiskANN, WWW 2023, [DOI](https://doi.org/10.1145/3543507.3583552) | 标签过滤 + ANN | 可借鉴磁盘图布局与标签过滤；不是 episode 检索基线 | 已有全文记录（`papers/filtered-ann/10.1145-3543507.3583552-filtered-diskann`） |
| CubeGraph, arXiv:2604.06616 (2026) | 向量 + 空间约束；层级网格 + 单元图动态拼接 | 只处理空间范围，不处理轨迹/序列；标题含 temporal 但摘要只描述空间分区 | 摘要级；全文未精读 |
| LIST, VLDB J. 2025, [arXiv:2403.07331](https://arxiv.org/abs/2403.07331) | 空间关键词查询 + embedding；学习索引 | 查询是点 + 文本，不是轨迹；无绝对时间窗 | 摘要/HTML 级；全文未精读 |
| Grant, arXiv:2610.11563 (2026) | 多属性范围过滤 ANN | 通用范围过滤，不利用轨迹结构 | 书目索引已收录；全文未精读 |
| Filtered ANN survey, [arXiv:2505.06501](https://arxiv.org/abs/2505.06501) | 过滤 ANN 综述 | 术语与基线清单 | 摘要级 |

## 2. 时间感知单向量检索：术语边界，不参与问题新颖性对照

这些工作用于区分“对象有效性/新鲜度”与“episode 时间可行性”。它们不回答轨迹序列检索问题，其全文状态不阻塞本方向的查重结论。

| 工作 | 问题 | 与本方向的关系 | 证据状态 |
|---|---|---|---|
| TANNS / Timestamp Graph, IEEE TKDE 2026, [DOI:10.1109/TKDE.2026.3722749](https://doi.org/10.1109/TKDE.2026.3722749) | 查询在时间 $t$ 或区间 $[t_1,t_2]$ 内有效的向量；时间戳图 + historic neighbor tree | 检索单个向量；时间决定有效性，不检索序列/ episode | 摘要与作者页书目已核对；全文未获取 |
| TANGO / TDVS, [arXiv:2609.00548](https://arxiv.org/abs/2609.00548) | 时间衰减向量搜索；语义相似度 + 新鲜度联合打分 | 单向量、单调衰减；不做序列对齐 | 摘要级 |
| Re3, ACL 2026, [ACL Anthology](https://aclanthology.org/2026.acl-long.1180/) | 相关性 + 新近性检索，面向 temporal IR/RAG | 文档级时间信号；不是轨迹/episode | 摘要级 |
| LiveVectorLake, [arXiv:2601.05270](https://arxiv.org/abs/2601.05270) | 版本化向量知识库、流式更新与时间检索 | 单向量版本管理；不做序列 | 摘要级 |

## 3. 轨迹相似检索

| 工作 | 查询/输出 | 时间 | 语义 | 与本方向的差异 | 证据状态 |
|---|---|---|---|---|---|
| **ETQ: Top-k Exemplar Trajectory Queries**, ICDE 2017, [DOI](https://doi.org/10.1109/ICDE.2017.114) | 查询点序列 + 每点关键词 → top-k 轨迹 | 时间戳不是查询约束 | TF-IDF 关键词 | 不定位连续 episode；无绝对时间窗/持续时间 | 已读定义 1–7、算法 1–2；实验未复现 |
| **TISIS / TISIS\***, BDA 2024, [arXiv:2409.11301](https://arxiv.org/abs/2409.11301) | POI 查询轨迹 + 阈值 → 完整轨迹集合 | 只用于序列顺序 | TISIS* 用 Word2Vec POI embedding + $\epsilon$ 匹配 | LCSS threshold；不定位 episode，不是 top-k | 已核 arXiv v2 问题定义与 contextual POI 章节 |
| **ST2Vec**, KDD 2022, [DOI](https://doi.org/10.1145/3534678.3539375) | 完整路网轨迹 → learned distance；top-k 为 case study | 空间与时间共同进入软相似度 | 无 POI/text 语义 | 整轨迹表示；无硬时间窗/episode | 已核正文目标与正式 DOI；arXiv 题名是早期版本 |
| **REPOSE**, ICDE 2021, [DOI](https://doi.org/10.1109/ICDE51399.2021.00067) | 查询轨迹 → 分布式 top-k 完整轨迹 | 依所选轨迹距离；无绝对窗口 | 无 | 支持多种几何/序列距离，但不是语义 episode | 已核 Definition 3、索引结构与正式书目 |
| **N-tree**, ACM T-SAS 2025, [DOI](https://doi.org/10.1145/3716825) | exact kNN / metric range 完整轨迹 | DistanceAvg 是连续时空 metric | 无 | 精确整轨迹 metric index；不做 episode | 已核定义、精确查询与正式 DOI |
| **STKST-I**, ESWA 2023, [ScienceDirect](https://www.sciencedirect.com/science/article/pii/S0957417423005663) | 关键词 + 时间 + 位置 → 语义轨迹 top-k | 有时间相关性 | 关键词 | 不做稠密序列对齐与 episode 定位 | 摘要级；正式 DOI/卷期待复核 |
| TrajPrism, [arXiv:2605.10782](https://arxiv.org/abs/2605.10782) | 文本/指令 → 语义轨迹 benchmark | 轨迹带时间 | 多模态/文本 | 整轨迹协议是否含硬窗口待核 | 摘要级 |
| OmniTraj, KDD 2025, [arXiv:2505.17437](https://arxiv.org/abs/2505.17437) | 多模态轨迹 → 轨迹 | 弱 | 学习表示 | 整轨迹；无已核 episode 协议 | 摘要级 |
| Distributed Semantic Trajectory Similarity Search, 2025, [IEEE](https://ieeexplore.ieee.org/document/11207137) | 空间邻近 + 语义相似 → 轨迹 | 弱 | 有 | 分布式整轨迹检索；非 episode/时间窗 | 摘要级 |
| **Spatio-Temporal Trajectory Similarity Measures: A Comprehensive Survey and Quantitative Study**, TKDE 2024, [DOI](https://doi.org/10.1109/TKDE.2023.3323535) | 25种轨迹 measure 的 taxonomy 与 benchmark | 系统比较时空 measure | POI/text 语义不是主分类轴 | 距离与评测框架，不是查询方法 | 已核 taxonomy、Table 1 与正式书目 |

## 4. 子轨迹与 representative subtrajectory 搜索

| 工作 | 问题 | 与本方向的差异 | 证据状态 |
|---|---|---|---|
| **GTRSS: Graph-based Top-k Representative Similar Subtrajectory Query**, arXiv 2025, [arXiv:2507.05542](https://arxiv.org/abs/2507.05542) | 对每条数据轨迹求与查询最相似的连续子轨迹，再在不同轨迹间取 top-k；GARI + CNDI 近似图搜索 | **输出语义与本方向同形**；原距离为几何 DTW/EDR/ERP，无语义 POI/text、绝对时间窗、持续时间与空间硬约束 | 已核 Definitions 1–4、距离与算法框架 |
| **Efficient Learning-based Top-k Representative Similar Subtrajectory Query (TRSSQ)**, ICDE 2024, [DOI](https://doi.org/10.1109/ICDE60146.2024.00335), pp. 4396–4408 | 每条轨迹取最相似子轨迹 → 全局 top-k；RSSE（Bi-GRU + attention 匹配）估计相似度，取 top-M 后用 ExactS/PSS/RLS 验证 | 与本方向同为“每轨一个代表片段 + 全局 top-k”；度量 DTW/EDR/ERP/LCSS，时间维在建模中省略，无 POI/text 语义与时间/空间硬约束 | **全文13页已核**（`papers/trajectory-search/10.1109-icde60146.2024.00335-trssq/`） |
| Efficient Non-Learning Similar Subtrajectory Search, PVLDB 16, [PDF](https://www.vldb.org/pvldb/vol16/p3111-cheng.pdf) | 几何子轨迹相似搜索 | 坐标距离；无语义 embedding/绝对时间窗 | 摘要级 |
| SimSub / deep RL, [arXiv:2003.02542](https://arxiv.org/abs/2003.02542) | 查询轨迹 → 最相似子轨迹 | 几何/学习表示；输出范围需按具体变体区分 | 摘要级 |
| Fast Subtrajectory Similarity Search in Road Networks, PVLDB 13, [PDF](http://www.vldb.org/pvldb/vol13/p2188-koide.pdf) | 路网加权编辑距离下的子轨迹 | 路网几何；无 semantic/time-window 可行性 | 摘要级 |

## 5. 轨迹混合范围查询：同一数据域、不同算子

| 工作 | 问题 | 与本方向的关系 | 证据状态 |
|---|---|---|---|
| **Towards Quantum Range Query for Spatial-Temporal-Semantic Trajectory Data**, arXiv:2609.29612v1 | 将轨迹段表示为 temporal/spatial/semantic 多维记录；窗口查询返回全部精确匹配段的量子叠加态 | 属于轨迹+时间戳混合查询，不能按 ACORN 式单对象过滤排除；但它是 range reporting，不做 top-k 相似度、每轨迹最佳 episode 或去重 | 已核全文4页；Table 1 只实测 Time/Space/Space-time，结论将 semantic keys 列为 future work |

## 6. 差异判断

**已经被覆盖**：

1. GTRSS/TRSSQ 已覆盖连续子轨迹定位、每条轨迹一个代表片段和全局 top-k；不能把 episode 区间或轨迹级去重作为新意。
2. TISIS* 已覆盖 POI embedding 序列匹配，ETQ 已覆盖语义点序列到 top-k 轨迹。
3. ST2Vec 与 DistanceAvg 已让时间进入整轨迹相似度；Quantum Range Query 已处理轨迹段上的时空范围约束。
4. TRSSQ（ICDE 2024）已用学习模型 RSSE 做 representative subtrajectory 的候选估计与 filter-validate；“学习型相似度估计 + 代表片段”同样不是新意。

**仍待验证的候选增量**：

1. 绝对时间窗、持续时间与空间范围作为不可放宽的 episode 可行性；
2. 在该可行域内按稠密语义序列距离排序；
3. 比 `hard filter → exact/approximate TRSSQ` 更有效的联合访问路径。

因此当前定位应是 **constrained semantic representative subtrajectory query**，不是“首次 episode 检索”。单对象 filtered ANN 仍只提供执行原语。

## 7. 待补查

- ~~TRSSQ 全文~~：已由用户提供 13 页正式版 PDF 并完成精读；度量集合、RSSE 结构、候选预算与“无约束接口”均已核实（见 `papers/trajectory-search/10.1109-icde60146.2024.00335-trssq/evidence-2026-10-23.md`）。剩余：其代码未公开，实验数字未复现。
- ETQ、GTRSS、TISIS*、STKST-I 的前后向引用闭包。
- TrajPrism/OmniTraj 的时间窗与 episode 评测协议。
- `constrained representative subtrajectory query`、`semantic subtrajectory retrieval`、`time-windowed subtrajectory top-k`。
- 语义轨迹 join（REST 等）与单查询 episode top-k 的边界。
- 当前仍不能声称“无人研究”；若已有方法覆盖全部约束，应 pivot。
