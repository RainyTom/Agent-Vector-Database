# Curator: Efficient Vector Search with Low-Selectivity Filters

核查日期：2026-10-18。等级 B，可迁移借鉴；非 vector join。分析版本：2601.01291v3。

## 问题与方法

带标签的向量集合、单标签或复杂谓词与查询向量/k。

共享 hierarchical k-means base index；嵌入 per-label buffers 与 Bloom 路由；复杂谓词临时索引；与图索引结合。

## 保证与限制

未核实一般数据下时间或召回界；正文主要为结构、搜索和实验。

底层向量共享；标签成员 buffers 与搜索结构成本须分开，精确渐近界本轮未核实。

§4 以 beam/best-first 和启发式停止权衡召回；不是无条件逐查询保证。

聚类层级来自向量分布，不是外部 taxonomy；低 selectivity 场景定位；跨层级收益需实测。

## 实验核查

数据：YFCC-10M、arXiv。基线：Per-label IVF/HNSW、Shared IVF/HNSW、Parlay-IVF、Filtered DiskANN、ACORN、pgvector IVF/HNSW。指标：Recall@10、latency、memory、construction time、update cost。

§6.1–6.6 比较共享、独立索引、复合谓词及更新；使用原始版本细节，不沿用后文简化描述。未独立复现。

## 本项目启发与证据

祖先展开后单标签查询为直接强基线；共享树、共享向量和不同过滤集合专用结构已有。

正文定位：3.1–3.2, 4.1–4.2, 6.1–6.6；PDF 页 1–7, 8–14。见 [PDF](paper.pdf)、[完整文本提取](paper.md)、[alphaXiv 页证据](alphaxiv-evidence-2026-10-18.xml) 和 [创新性比较](../../../researches/nested-attr-vector-search/novelty-analysis.md)。

## 2026-10-18 Overlap 与端到端代价补核

v3 §4.2、PDF pp.6–7：任意复杂谓词匹配子集被视为 virtual label，输入为按向量 ID 排序的合格对象列表。ID 与共享聚类树子树连续范围对齐，Algorithm 2 通过边界二分和递归划分构造临时 per-predicate index，不重新对向量做一次完整聚类。原文也提出只对预计搜索的子树进行过滤/临时构造的延迟优化；其实现与实验采用程度未核实，不把 eager 全构造当作唯一能力。

§6.2、Fig.9、PDF pp.10–11：实际复合查询实验为两标签 OR 和两标签 AND，YFCC-10M 上各 100 个组合、每组合 100 查询。方法可表达一般谓词，不等于论文已实验覆盖大标签集合或 taxonomy compact query。OR 的语义为至少一个标签命中，直接对应 overlap；不是“交集大小至少 t”。

该节分别画 Curator 与 Curator (Idx)：后者为已预建 virtual-label 索引的版本，前者在查询时构造临时索引。两者不是同一运行轨迹简单扣除计时，比较需同时保留。§4.2 将外部关系库产生的有序合格 ID 列表作为算法输入，因此不能由包含临时建索引时间推断已包含整个谓词生成/bitmap 运算成本。新研究应报告这两部分实际耗时；不得声称 Curator 完全不支持未知 OR/只能处理预建标签。

作者 low selectivity 指合格对象占比低。其他论文可能用 high selectivity 表示同一现象，统一用 pass ratio=合格对象数/总对象数避免混淆。本项目新增约束是评价口径与工作负载，不据此宣称原方法无法执行。
