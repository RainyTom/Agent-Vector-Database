# iRangeGraph: Improvising Range-dedicated Graphs for Range-filtering Nearest Neighbor Search

核查日期：2026-10-18。等级 B，可迁移借鉴；非 vector join。分析版本：2409.02571v1。

## 问题与方法

带数值属性的向量集合、查询向量、数值区间与 k。

物化一组 elemental graphs；查询时在线组织范围专用邻接；共享分段范围的图结构。

## 保证与限制

§3 Theorem 3.1–3.2 分析构建与在线邻接代价；不是任意数据的 ANN 召回保证。

v1 §3：空间 O(n*m*log n)；单对象在线构边摊销 O(m+log n)，m 为图邻居参数，不能混为向量维度。

在线图不必与为查询区间从头构建的 RNG 相同；端到端误差界未核实。

原生单数值区间不直接表示多标签对象；elemental graphs 可能缺少范围专用图的边。

## 实验核查

数据：WIT-Image、TripClick、Redcaps、YouTube-Rgb、YouTube-Audio。基线：SeRF、HNSW、其他比较方案见 §5，未在本轮逐项复核。指标：recall@10、QPS、index size、construction time。

§5 在真实数值属性与多种范围比例上评测；性能未独立复现。未独立复现。

## 本项目启发与证据

单标签 taxonomy 用 DFS preorder 精确转为区间，应作为强基线；多标签展开需 distinct-object top-k 与成本计入。

正文定位：2, 3 (Theorems 3.1–3.2), 5；PDF 页 3–6, 8–10。见 [PDF](paper.pdf)、[完整文本提取](paper.md)、[alphaXiv 页证据](alphaxiv-evidence-2026-10-18.xml) 和 [创新性比较](../../../researches/nested-attr-vector-search/novelty-analysis.md)。
