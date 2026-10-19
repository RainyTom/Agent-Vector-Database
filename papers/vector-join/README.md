# 几何向量连接

直接研究向量集合之间的连接执行、复杂性或基数估计。普通 ANN 与 filtered ANN 不归入此目录。

当前收录 7 篇已保存全文的论文。每篇只在一个主主题存放；跨主题可检索，不复制材料。

[综述](../../researches/vector-join/survey.md)与[复现路线](../../researches/vector-join/roadmap.md)保留在研究目录。可迁移的过滤 ANN、前缀检索和一般相似连接分别存放于兄弟主题目录，通过[总索引](../papers.jsonl)与[跨主题关系](../relations.jsonl)关联。

| 论文 | 年份与版本状态 | 本地材料 |
|---|---|---|
| [Fast Approximate Similarity Join in Vector Databases](https://doi.org/10.1145/3725403) | 2025；Proceedings of the ACM on Management of Data 3(3), Article 158 (SIGMOD 2025) | [目录](10.1145-3725403-simjoin/)、[阅读报告](10.1145-3725403-simjoin/paper.report.md) |
| [Fast Approximate Vector Joins via Offline-Online Co-Design](https://arxiv.org/abs/2603.16360) | 2026；arXiv (cs.DB)；正式 venue 未核实 | [目录](2603.16360-fast-approximate-vector-joins/)、[阅读报告](2603.16360-fast-approximate-vector-joins/paper.report.md) |
| [DiskJoin: Large-scale Vector Similarity Join with SSD](https://doi.org/10.1145/3769780) | 2025；Proceedings of the ACM on Management of Data 3(6), Article 315；SIGMOD 2026 | [目录](2508.18494-diskjoin/)、[阅读报告](2508.18494-diskjoin/paper.report.md) |
| [Xling: A Learned Filter Framework for Accelerating High-Dimensional Approximate Similarity Join](https://arxiv.org/abs/2402.13397) | 2024；arXiv (cs.DB, cs.AI)；正式 venue 未核实 | [目录](2402.13397-xling/)、[阅读报告](2402.13397-xling/paper.report.md) |
| [GPU Accelerated Similarity Self-Join for Multi-Dimensional Data](https://doi.org/10.1145/3329785.3329920) | 2019；DaMoN 2019, Article 6, 9 pages | [目录](1809.09930-gpu-similarity-self-join/)、[阅读报告](1809.09930-gpu-similarity-self-join/paper.report.md) |
| [On the Complexity of Inner Product Similarity Join](https://doi.org/10.1145/2902251.2902285) | 2016；PODS 2016, pp. 151–164 | [目录](1510.02824-inner-product-similarity-join-complexity/)、[阅读报告](1510.02824-inner-product-similarity-join-complexity/paper.report.md) |
| [Similarity Join Size Estimation using Locality Sensitive Hashing](https://doi.org/10.14778/1978665.1978666) | 2011；Proceedings of the VLDB Endowment 4(6), pp. 338–349 | [目录](1104.3212-lsh-join-size-estimation/)、[阅读报告](1104.3212-lsh-join-size-estimation/paper.report.md) |

从项目根目录检索本主题：

```bash
python tools/research_index.py search '' --topic vector-join
```

索引字段、证据等级和维护方式见[论文库总入口](../README.md)。A/B/C 是相对 vector join 主线的相关性等级，不是物理目录分类。
