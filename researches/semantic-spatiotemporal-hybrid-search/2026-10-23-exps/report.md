# 2026-10-23：问题对照范围纠正

## 当日目标

纠正“时间窗约束的语义 episode 检索”的相关工作分类，避免用输入输出语义不同的单对象过滤检索支撑问题新颖性。

## 核查方法

按四个字段重新划分工作：查询单位、数据单位、返回单位、排序/约束语义。只有共享“轨迹、序列或子轨迹检索”语义的工作进入问题新颖性矩阵。

本轮是文献分类纠正，没有运行新的性能实验，也没有新增结果数字。

## 本轮新增核查

用户指出 Quantum Range Query 属于轨迹+时间戳混合搜索，并提供六篇轨迹相似检索论文。下载并逐篇核对原始 PDF；书目信息以 arXiv 与 Crossref 正式记录交叉确认。

| 工作 | 核查结果 | 对当前问题的影响 |
|---|---|---|
| Towards Quantum Range Query…，arXiv:2609.29612v1 | 轨迹段记录含 temporal/spatial/semantic 分量；窗口内精确 range reporting；实验只做 Time/Space/Space-time | 从“非轨迹过滤”纠正为“同数据域、不同算子”；semantic 实现仍不能确认 |
| TISIS / TISIS*，arXiv:2409.11301v2 | POI 轨迹 LCSS threshold；TISIS* 明确使用 Word2Vec POI embeddings | 语义序列匹配已有直接前序，但无 episode/top-k/时间硬约束 |
| REPOSE，ICDE 2021 | 分布式完整轨迹 top-k，支持多种轨迹距离 | 扩展性/索引参考，不是 episode 输出 |
| N-tree，ACM T-SAS 2025 | exact whole-trajectory kNN/range；DistanceAvg 为时空 metric | 时间感知精确轨迹检索前序 |
| ST2Vec，KDD 2022 | 学习完整路网轨迹的空间+时间软相似度 | “时间进入轨迹相似度”不能作为新意 |
| GTRSS，arXiv:2507.05542v1 | 每条数据轨迹先取最佳连续子轨迹，再对不同轨迹做 top-k | **输出语义与当前问题同形**；episode 定位、轨迹级去重和 top-k 已有 |
| TKDE 2024 trajectory similarity survey | 25种 measure taxonomy 与统一评测维度 | 用于距离/benchmark 设计，不是直接方法 |

## 纠正结果

直接问题家族现在分三层：

1. **输出语义直接前序**：GTRSS/TRSSQ。已覆盖查询轨迹到 representative subtrajectory、每轨迹一个结果、全局 top-k。
2. **语义/时间组件前序**：ETQ、TISIS*、ST2Vec、STKST-I。分别覆盖语义点序列、POI embedding+LCSS、时空软相似度、关键词+时间+位置。
3. **同数据域但不同算子**：Quantum Range Query。它解决轨迹段时空范围查询，不解决语义相似度 top-k。

ACORN、Filtered-DiskANN、TANNS/TANGO、CubeGraph、LIST 仍是单对象检索，只作为执行机制参考。

## 对研究判断的影响

此前“连续 episode + 轨迹级去重 + episode 区间”候选增量被 GTRSS/TRSSQ 否定。当前可检验增量收缩为：

- 绝对时间窗、持续时间与空间范围的硬 episode 可行性；
- 在该可行域内进行稠密语义序列排序；
- 比 `hard filter → exact/approximate TRSSQ` 更有效的联合访问路径。

因此当前方向应称为 **constrained semantic representative subtrajectory query**。不能宣称“首次 episode 检索”；若硬约束没有改变算法或不能优于直接适配，应 pivot。

## 持久化

- 新增 `papers/trajectory-search/` 主题与7条结构化论文记录；
- 保存7份原始 PDF、日期化元数据与逐篇问题定义证据；
- 更新问题对照、新颖性审计、前序工作、检索日志和研究入口；
- 新增 survey→REPOSE、survey→ST2Vec、GTRSS→REPOSE 三条原文关系。

## 局限

- 没有运行新性能实验，也未复核论文报告的性能数字；
- ICDE 2024 `Efficient Learning-based Top-k Representative Similar Subtrajectory Query` 原文尚未获取/精读；
- GTRSS/TRSSQ、ETQ、TISIS* 的完整前后向引用闭包尚未完成；
- Quantum Range Query 当前为 arXiv v1，正式发表状态未核实；
- 当前仍不能声称完整组合无人研究或足以构成独立问题。

## 下一步

1. 获取并精读 ICDE 2024 TRSSQ 原文，核对 learned distance、输出去重、约束接口、代码与 DOI；
2. 实现/复现 `hard filter → exact TRSSQ`，再评估 GTRSS 式近似搜索；
3. 用相同语义距离、硬可行性、global trajectory recall 与资源预算对比当前方案；
4. 做 GTRSS/TRSSQ、ETQ、TISIS*、STKST-I 引用闭包；若发现全覆盖工作则 pivot。
