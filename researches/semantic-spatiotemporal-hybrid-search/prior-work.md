# 前序工作与证据边界

更新日期：2026-10-22。本文只记录已核对到摘要或正文片段的工作；未精读全文的条目明确标注。`未核实` 表示书目/版本/实验尚未核对，`未提供` 表示原文没有该内容。

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

## 3. 语义轨迹检索

| 工作 | 查询/输出 | 时间 | 序列 | 与本方向的差异 | 证据状态 |
|---|---|---|---|---|---|
| **ETQ: Top-k Exemplar Trajectory Queries**, ICDE 2017, [DOI](https://doi.org/10.1109/ICDE.2017.114) | 查询点序列 + 每点关键词 → top-k 轨迹 | 时间戳存在，但不是查询约束 | 是，点级空间+TF-IDF 聚合；可选顺序敏感扩展 | 不定位连续 episode、无绝对时间窗/持续时间、非稠密 embedding | 已读论文定义 1–7、算法 1–2 与相关工作表；实验未复现 |
| TISIS / TISIS*, BDA 2024, [arXiv:2409.11301](https://arxiv.org/abs/2409.11301) | 完整查询轨迹 → 相似完整轨迹；LCSS 阈值 | 仅时间戳存在，不用于约束 | 是，POI embedding + 顺序 | 不定位 episode、不支持绝对时间窗、不是 top-k 连续排序 | 已读 HTML 问题定义与算法 1–2；实验未逐项核对 |
| TrajPrism, [arXiv:2605.10782](https://arxiv.org/abs/2605.10782) | 文本/指令 → 语义轨迹检索 benchmark | 轨迹带时间，但检索协议未以时间窗为核心 | 是 | 整轨迹检索；不做 episode 定位与硬时间窗 | 摘要级 |
| OmniTraj, KDD 2025, [arXiv:2505.17437](https://arxiv.org/abs/2505.17437) | 多模态（原始轨迹/拓扑/路段/区域）→ 轨迹 | 弱 | 学习表示 | 整轨迹检索；无 episode/时间窗 | 摘要级 |
| STKST-I, Expert Systems with Applications 2023, [ScienceDirect](https://www.sciencedirect.com/science/article/pii/S0957417423005663) | 关键词 + 时间 + 位置 → 语义轨迹 top-k | 有 | 语义关键词 | 关键词匹配，不是 embedding 序列对齐；不定位 episode | 摘要级；正式 DOI/卷期待复核 |
| Distributed Semantic Trajectory Similarity Search, 2025, [IEEE](https://ieeexplore.ieee.org/document/11207137) | 空间邻近 + 语义相似 → 轨迹 | 弱 | 是 | 分布式整轨迹检索；非 episode/时间窗 | 摘要级 |
| DASS, IJCAI 2025, [PDF](https://www.ijcai.org/proceedings/2025/0833.pdf) | 学习轨迹相似度 | 时空相邻关系 | 学习表示 | 相似度学习，不是索引/查询执行 | 摘要级 |

## 4. 子轨迹与序列搜索

| 工作 | 问题 | 与本方向的差异 | 证据状态 |
|---|---|---|---|
| Efficient Non-Learning Similar Subtrajectory Search, PVLDB 16, [PDF](https://www.vldb.org/pvldb/vol16/p3111-cheng.pdf) | 几何子轨迹相似搜索 | 坐标距离，非语义 embedding；无绝对时间窗 | 摘要级 |
| SimSub / deep RL, [arXiv:2003.02542](https://arxiv.org/abs/2003.02542) | 查询轨迹 → 最相似子轨迹 | 几何/学习表示；非 top-k 多轨迹 episode 检索 | 摘要级 |
| Fast Subtrajectory Similarity Search in Road Networks, PVLDB 13, [PDF](http://www.vldb.org/pvldb/vol13/p2188-koide.pdf) | 路网加权编辑距离下的子轨迹 | 路网几何；无 embedding/时间窗 | 摘要级 |
| Trajectory Similarity Measurement: An Efficiency Perspective, PVLDB 17, [PDF](https://www.vldb.org/pvldb/vol17/p2293-qi.pdf) | 轨迹相似度效率综述 | 提供基线与度量分类 | 摘要级 |

## 5. 初步差异判断

**可检验的差异候选**：

1. 查询是**语义序列**而非单个向量；候选是**轨迹中的连续 episode**而非整条轨迹。
2. episode 必须满足**绝对时间窗、持续时间和空间范围**三个硬约束；近似只作用于距离排序。
3. 返回**轨迹级 top-k + episode 区间**，而不是阈值内的全部轨迹或单点近邻。
4. 若系统继续使用点级 ANN 生成候选，可借鉴过滤图、时间分区与空间分区；这些只是执行原语，不是问题级对照。

**不能声称的**：

- 不能把单对象过滤/时空向量检索与语义轨迹检索统称为“直接工作”；本问题的新颖性只能相对后者和子轨迹检索判断。
- 不能把 TISIS* 的 LCSS 直接当作本问题的基线而不说明其不支持时间窗/episode 定位。
- 不能把 CubeGraph 的“空间单元图”与轨迹 episode 索引混为一谈。
- 不能在没有实测的情况下声称本问题一定比全扫描更值得索引；时间窗选择性可能让扫描足够快。

## 6. 待补查

- ETQ 全文（已读定义/算法，实验未复现）：确认其顺序敏感扩展是否覆盖连续 episode 与时间约束。
- SimSub / 代表子轨迹（ICDE 2024/2025）：确认 top-k 输出语义、时间处理与索引。
- ACORN、Filtered-DiskANN、TANNS/TANGO、CubeGraph、LIST 已排除出问题新颖性审计；只有在采用其执行机制时才需要补读实现细节。
- TrajPrism/OmniTraj 全文：确认检索协议是否已有时间窗或 episode 级评测。
- STKST-I 正式 DOI/作者/实验。
- 语义轨迹 join（REST 等）与 episode 检索的边界。
- 时空关键词搜索（STKS）与路线约束检索的先前工作。
