# 向量数据库技术知识库

目标：把文献、源码和实验中的技术知识整理为可检索、可验证、能指导下一轮研究的学习路径。**不替代论文总索引，也不把知识整理当新方案已胜出。**

组织参考：[Multimodal Lakehouse Handbook · 学习路径](https://wangyong9999.github.io/lakehouse-wiki/learning-paths/)。借鉴“先读什么、做什么、能回答什么”，不转载其正文或直接继承其产品性能/复杂度结论。

## 从哪里开始

| 顺序 | 知识章 | 读完应能回答 |
|---|---|---|
| 01 | [查询语义与表示](query-semantics.md) | L2/IP/cosine何时等价？过滤、hybrid、多向量、vector join分别输出什么？ |
| 02 | [索引机制与成本](index-mechanisms.md) | Flat/HNSW/IVF/PQ/OPQ/DiskANN究竟省哪类访问？候选遗漏与量化误差怎样分开？ |
| 03 | [过滤执行与层级结构](filtering-and-hierarchy.md) | pre-filter是否一定扫描？不合格点是否可导航？树的laminar性质在哪些条件成立？ |
| 04 | [生命周期与系统语义](lifecycle-and-systems.md) | 写确认、可见、索引完成有何不同？删除、压实、WAL、事务、副本分别保证什么？ |
| 05 | [评测与完整成本](evaluation-and-cost.md) | 如何对齐质量、分母、缓存和维护账本？什么实验只能支持组件结论？ |

实际学习顺序、动作与自测见[学习路径](learning-paths.md)；本轮阅读范围、勘误与下次队列见[持续学习记录](learning-log.md)；来源版本见[资料台账](sources.jsonl)。

## 当前证据状态

- 五章是首轮已写成的知识，不是五份未完成占位页；每章有机制、边界、来源和自测。
- 本轮已读参考站点的总路径、一周、一月AI、一季度及向量数据库页；已核相关官方资料和指定源码。没有声称读完整个参考站点或完整覆盖向量数据库。
- 已运行：[数学语义探针](2026-10-30-exps/data/semantic-probes.json)、[固定版本hnswlib API探针](2026-10-30-exps/data/hnsw-lifecycle-probe.json)。只有构造输入，不能推广成真实数据的速度/召回结果。
- 首轮本地未运行pgvector/Milvus/Qdrant/Weaviate服务、故障恢复、并发隔离或SSD ANN；后续已接收服务器WIKI/SIFT闭包实验，见[2026-10-31学习记录](2026-10-31-exps/report.md)。来源阅读、本地探针与服务器结果分开，不填造产品实验。
- 原始命令、构建输出、SHA与限制见[本轮report](2026-10-30-exps/report.md)及[复现说明](2026-10-30-exps/experiments/reproduction.md)。

## 与已有研究连接，不另造论文库

| 已有材料 | 知识库中的用途 |
|---|---|
| [论文总索引](../../papers/README.md) | 统一身份、版本、分类、全文和关系，不重复下载/新建同一论文 |
| [Filtered ANN](../../papers/filtered-ann/README.md) | ACORN/Curator/PathSeer及范围图等机制前序 |
| [Vector ANN](../../papers/vector-ann/README.md) | 图构建、hubness、OOD及路由 |
| [目录感知检索笔记](../../papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md) | Local/Inclusive集合、树更新、scope与ANN职责分离 |
| [多向量主题](../../papers/multi-vector-search/README.md) | 集合距离、组合空间、表示转换的不同问题语义 |
| [当前层级实验合同](../nested-attr-vector-search/2026-10-30-exps/closure-server-plan.md) | 将知识落实为同池消融、同质量速度与动态总成本，不能反过来预定实验成功 |
| [Vector join研究](../vector-join/research-questions.md) | 复用查询工作/维护思路时保留join的输入输出与pair语义 |

## 如何持续维护

每次后续研究先读`learning-log.md`未完成队列和`sources.jsonl`查重，再按一个可回答的问题学习。追加来源的版本/章节，修订对应知识页，做能区分误解的最小操作，追加日期化report与证据；未运行项保持“待验证”。

不是只累计链接：每轮至少产出一个新解释、一个反例/可执行观察，或一项有依据的勘误。旧结果不覆盖；活文档变化不能偷偷改写成历史部署能力。

**未部署自动学习、定时抓取或后台服务。** “持续”通过之后实际任务中的增量更新实现。下一优先项是固定版本SQL执行计划和隔离可见性小实验，范围与验收已写入[学习路径](learning-paths.md)。
