# Fast Label-Filtering Approximate Nearest Neighbor Search via Progressive Label Set Stratification

核查日期：2026-10-18。等级 B，可迁移借鉴；非 vector join。分析版本：2609.15058v1。

## 问题与方法

对象标签集合、查询标签集合、向量与 k；支持 equality/containment/overlap。

Label-stratified similarity graph；Jaccard 标签距离分层；向量和标签双空间剪枝；MinHash/IVF 标签候选；跨 tier 候选复用。

## 保证与限制

§3.2 MinHash 碰撞概率；§4 显式标签模型下的 stepwise probability 分析；不是任意标签分布下端到端召回保证。

索引分层与剪枝的完整复杂度本轮未核实。

标签独立/特定分布条件不能直接移植到祖先闭包标签。

tiers 为标签集合相似度层级而非语义树；祖先展开带来分布依赖与元数据变化。

## 实验核查

数据：SIFT、GIST、TripClick、LAION、YTB-Video、YTB-Audio、Wikipedia、YFCC-1M/10M。基线：ACORN、Packing、RWalks、UNG、ELI、FAISS、VSAG、Milvus、PGVector、pre-filtering。指标：Recall@10、QPS、distance computations、index size、indexing time。

§6 Table 3–4、QPS–recall 曲线覆盖三种集合谓词；未复现，不外推为 taxonomy 收益。未独立复现。

## 本项目启发与证据

overlap 直接支持 Desc+(u) 查询标签集；祖先展开后 singleton containment 精确表达当前语义。

正文定位：2.1 (Definitions 2–3), 3, 4, 6；PDF 页 1, 3, 4–9, 19。见 [PDF](paper.pdf)、[完整文本提取](paper.md)、[alphaXiv 页证据](alphaxiv-evidence-2026-10-18.xml) 和 [创新性比较](../../../researches/nested-attr-vector-search/novelty-analysis.md)。

## 2026-10-19 包含语义与复杂度补核

§5 PDF p.8 已核：图边空间 O(mnT)，另需 IVF/MinHash；查询代价为 Cscope+O(Hη)，条件期望式依赖单调搜索/§4 stepwise proxy，不将遍历对数估计当作无条件总查询成本。§6.1 仅保留至少 k 个合格答案的查询；UniFilter/Curator 未纳入主性能评测。Table 2 的 Vamana containment 标记与 Filtered-DiskANN 原文主问题单标签/OR、多AND留future work存在粒度差异，不能按该表重写前序能力。详情见[包含过滤文档](../../../researches/containment-filtered-vector-search/index.md)。
