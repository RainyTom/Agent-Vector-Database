# Grant: A Framework for Approximate Nearest Neighbor Search with Multi-Attribute Range Filters

核查日期：2026-10-18。等级 B，可迁移借鉴；非 vector join。分析版本：2610.11563v1。

## 问题与方法

向量与 m>=2 数值属性、每属性闭区间、查询向量/k。

属性空间平衡树；叶子图索引；相交叶子搜索后验证过滤；probabilistic node verification；可选历史查询加速。

## 保证与限制

§4 Theorem 1 的 kd-tree 次线性界依赖固定分区参数 c、维度处理与底层 ANNS 复杂度模型，不能解读为 taxonomy 多标签召回保证。

§4 给底树与底图参数化空间及查询成本；不泛化为无条件高维次线性。

ANN 候选/部分相交叶子仍需检查最终过滤后的召回与缺额；无条件质量保证未核实。

原文数值属性划分不等于语义树；理论条件不能直接用于任意 ANN 底图和多标签去重任务。

## 实验核查

数据：Deep、SIFT、SPACEV、T2I；各 10M，属性使用合成幂律值。基线：BF、Pre-filter、IVF、Post-filter HNSW、ACORN-1、Grant ablations、workload-aware variants。指标：recall、QPS、construction time、space。

§5 对多属性范围、阈值宽度和可选历史查询进行比较；未在语义分类树上核实。未独立复现。

## 本项目启发与证据

近期 tree+graph 的直接前序，结构组合本身不足创新；需与单范围专门方案分别定位。

正文定位：2, 4.1–4.4, 5.1；PDF 页 1–8。见 [PDF](paper.pdf)、[完整文本提取](paper.md)、[alphaXiv 页证据](alphaxiv-evidence-2026-10-18.xml) 和 [创新性比较](../../../researches/nested-attr-vector-search/novelty-analysis.md)。
