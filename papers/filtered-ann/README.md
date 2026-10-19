# 过滤近邻检索（Filtered ANN）

带标签、数值范围、图范围或质量约束的单查询近似近邻搜索；这些论文自身不研究 vector join。

当前收录 12 篇已保存全文的论文。每篇只在一个主主题存放；跨主题可检索，不复制材料。

## 子方向

系统实现资料另存研究笔记：[Weaviate filtering](../../researches/containment-filtered-vector-search/systems/weaviate-filtering.md)。产品文档不冒充论文条目；Weaviate定制ACORN、原论文ACORN与已核读PathSeer原文分别追踪。

- 通用过滤与共享结构：ACORN、Curator、RWalks、PathSeer。
- 标签感知构图与索引选择：Filtered-DiskANN、TFANNS/Packing、ELI、LSSG。
- 范围过滤：iRangeGraph、Graph Range Filters/DLH、Grant。
- 召回控制：VADER。

UNG 与 UniFilter 原论文全文仍缺，作者海报/代码/综述和候选记录保存在[研究证据](../../researches/containment-filtered-vector-search/evidence/)，不计入下列完整全文索引。

| 论文 | 年份与版本状态 | 本地材料 |
|---|---|---|
| [ACORN: Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data](https://doi.org/10.1145/3654923) | 2024；Proceedings of the ACM on Management of Data 2(3), Article 120 (2024) | [目录](2403.04871-acorn/)、[阅读报告](2403.04871-acorn/paper.report.md) |
| [iRangeGraph: Improvising Range-dedicated Graphs for Range-filtering Nearest Neighbor Search](https://doi.org/10.1145/3698814) | 2024；Proceedings of the ACM on Management of Data 2(6), Article 239 (2024); SIGMOD 2025 | [目录](2409.02571-irangegraph/)、[阅读报告](2409.02571-irangegraph/paper.report.md) |
| [Curator: Efficient Vector Search with Low-Selectivity Filters](https://doi.org/10.1145/3786635) | 2026；Proceedings of the ACM on Management of Data 4(1) (2026); SIGMOD 2026 | [目录](2601.01291-curator/)、[阅读报告](2601.01291-curator/paper.report.md) |
| [Fast Label-Filtering Approximate Nearest Neighbor Search via Progressive Label Set Stratification](https://arxiv.org/abs/2609.15058) | 2026；arXiv; 注释声明 SIGMOD 2027 accepted，正式出版/DOI 未核实 | [目录](2609.15058-lssg/)、[阅读报告](2609.15058-lssg/paper.report.md) |
| [Approximate Nearest Neighbor Search with Graph Range Filters](https://arxiv.org/abs/2607.00727) | 2026；arXiv; 正式 venue 未核实 | [目录](2607.00727-graph-range-filters/)、[阅读报告](2607.00727-graph-range-filters/paper.report.md) |
| [Grant: A Framework for Approximate Nearest Neighbor Search with Multi-Attribute Range Filters](https://arxiv.org/abs/2610.11563) | 2026；arXiv; 正式 venue 未核实 | [目录](2610.11563-grant/)、[阅读报告](2610.11563-grant/paper.report.md) |
| [VADER: Filtered Vector Search with Declarative Recall](https://arxiv.org/abs/2609.38053) | 2026；arXiv; 页面自述 VLDB 2027，正式卷期/DOI 未核实 | [目录](2609.38053-vader/)、[阅读报告](2609.38053-vader/paper.report.md) |
| [Elastic Index Selection for Label-Hybrid AKNN Search](https://doi.org/10.14778/3785297.3785304) | 2025；Proceedings of the VLDB Endowment 19(4):617–630 (2025); VLDB 2026 | [目录](2505.03212-eli/)、[阅读报告](2505.03212-eli/paper.report.md) |
| [Filtered-DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with Filters](https://doi.org/10.1145/3543507.3583552) | 2023；Proceedings of the ACM Web Conference 2023 (WWW 2023), 3406–3416 | [目录](10.1145-3543507.3583552-filtered-diskann/)、[阅读报告](10.1145-3543507.3583552-filtered-diskann/paper.report.md) |
| [Tag-Filtered Approximate Nearest Neighbor Search](https://doi.org/10.1109/ICDE65448.2025.00272) | 2025；2025 IEEE 41st International Conference on Data Engineering (ICDE), 3642–3654 | [目录](10.1109-ICDE65448.2025.00272-tag-filtered-ann/)、[阅读报告](10.1109-ICDE65448.2025.00272-tag-filtered-ann/paper.report.md) |
| [RWalks: Random Walks as Attribute Diffusers for Filtered Vector Search](https://doi.org/10.1145/3725349) | 2025；Proceedings of the ACM on Management of Data 3(3), Article 212 (2025); SIGMOD 2025 | [目录](10.1145-3725349-rwalks/)、[阅读报告](10.1145-3725349-rwalks/paper.report.md) |
| [PathSeer: Adaptive Neighbor Handling for Efficient Filtered ANN Search](https://doi.org/10.1145/3802098) | 2026；PACMMOD4(3),Article221／SIGMOD2026 | [目录](10.1145-3802098-pathseer/)、[阅读报告](10.1145-3802098-pathseer/paper.report.md) |

从项目根目录检索本主题：

```bash
python tools/research_index.py search '' --topic filtered-ann
```

索引字段、证据等级和维护方式见[论文库总入口](../README.md)。A/B/C 是相对 vector join 主线的相关性等级，不是物理目录分类。
