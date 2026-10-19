# Approximate Nearest Neighbor Search with Graph Range Filters

核查日期：2026-10-18。等级 B，可迁移借鉴；非 vector join。分析版本：2607.00727v1。

## 问题与方法

对象关联到无向过滤图节点；查询向量、查询节点、hop 半径 r 与 k。

Distance-aware labeling；Hash/Bloom 压缩集合相交；DLH-M 复用查询节点中间索引。

## 保证与限制

正文分析图距离标签与 Bloom 近似集合过滤；端到端严格过滤保证需核最终验证。

本轮未完整核查预处理、标签大小与查询复杂度界。

Bloom 压缩可能引入误判；移植本项目需显式精确过滤复核。

无向 r-hop 范围不等于有向子树；每对象单节点关联不同于多标签存在语义；Definition 1 全局/可行集措辞与 Definition 2/例子需注意，后者明确可行集近邻。

## 实验核查

数据：SIFT、GIST、DEEP、YFCC；每组 1M，过滤图由 Erdos-Renyi 合成。基线：Pre-Filter、Post-Filter HNSW、ACORN、Navix。指标：recall、throughput、extra storage。

§5.1 在合成无向图/hop 范围与向量数据组合上实验；不能称真实 taxonomy 实验。未独立复现。

## 本项目启发与证据

说明图结构过滤与向量联合已被研究；不得把一般结构化过滤称为尚未出现。

正文定位：2.1, 3–4, 5.1；PDF 页 1–3, 5, 9–10, 12–13。见 [PDF](paper.pdf)、[完整文本提取](paper.md)、[alphaXiv 页证据](alphaxiv-evidence-2026-10-18.xml) 和 [创新性比较](../../../researches/nested-attr-vector-search/novelty-analysis.md)。
