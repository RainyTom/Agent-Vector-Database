# Trajectory Search

轨迹相似检索、子轨迹检索、时空轨迹距离与轨迹混合范围查询。该主题与单对象 filtered ANN 分开：查询/结果单位是完整轨迹、连续子轨迹或轨迹段。

| 记录 | 查询算子 | 数据/相似度 | 输出 | 证据状态 |
|---|---|---|---|---|
| [Quantum Range Query](2609.29612-quantum-range-query/) | 多维 range | 轨迹段 + 时间/空间；semantic 仅形式模型 | 精确匹配段的量子叠加态 | 全文4页已核；语义扩展是 future work |
| [TISIS / TISIS*](2409.11301-tisis/) | LCSS threshold | POI 序列；TISIS* 用 POI embedding | 完整轨迹集合 | 问题定义与 embedding 章节已核 |
| [REPOSE](2101.08929-repose/) | top-k | 多种几何/序列距离 | 完整轨迹 | 问题定义与分布式框架已核 |
| [N-tree](2408.07650-n-tree/) | exact kNN / metric range | DistanceAvg 时空 metric | 完整轨迹 | 定义、精确性与正式书目已核 |
| [ST2Vec](2112.09339-st2vec/) | similarity learning；top-k case study | 路网位置 + 时间 | 完整轨迹 | 时空学习目标与正式书目已核 |
| [GTRSS](2507.05542-gtrss/) | top-k representative subtrajectory | DTW/EDR/ERP，几何轨迹 | 每轨迹一个最佳连续子轨迹，再取 top-k | 定义与图搜索框架已核 |
| [Trajectory Similarity Survey](2303.05012-trajectory-similarity-survey/) | survey / benchmark | 25种时空轨迹 measure | taxonomy 与统一评测 | taxonomy 与正式书目已核 |

## 与当前语义 episode 问题的关系

- **输出语义最近**：GTRSS。它已经定义“每条轨迹取一个最佳连续子轨迹，再做全局 top-k”；episode 定位和轨迹级去重不能作为新意。
- **语义序列最近**：TISIS*。它已使用 Word2Vec POI embedding + LCSS，但只返回阈值内完整轨迹。
- **时间相似最近**：ST2Vec 与 N-tree/DistanceAvg。时间进入软相似度，但不是绝对时间窗与持续时间硬约束。
- **混合硬约束最近**：Quantum Range Query。它做轨迹段时空范围报告，不做 top-k 相似度；实验尚未实现 semantic keys。
- **规模化完整轨迹 top-k**：REPOSE。它提供分布式索引参考，但没有语义 episode 输出。

当前候选只能定位为：在绝对时间窗、持续时间和空间范围硬约束下，对语义连续 episode 做 top-k representative-subtrajectory 检索。是否足以成为独立新问题仍未确认。
