# 稀疏向量检索

当前1篇：Bruch、Nardini、Rulli、Venturini的 [Seismic](https://doi.org/10.1145/3626772.3657769)，SIGIR2024；本地读 [arXiv2404.18812v1全文](2404.18812-seismic/paper.md)。它是单查询learned sparse inner-product检索，不是vector join，也不是动态硬属性过滤。

核心可迁移点：posting几何分组与块摘要。完整max摘要上界需要非负query；实际posting截断、压缩、query-cut和heap_factor不自动保守。没有复现作者性能。

[本轮原文差异与边界](../../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/mechanism-assessment.md)、[总索引](../papers.jsonl)。
