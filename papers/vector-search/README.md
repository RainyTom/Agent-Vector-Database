# Vector / constrained geometric search

本主题收录原问题为单查询几何/向量搜索、而非join的工作；精确与近似明确分开。当前2条。

| 工作 | 原问题与保证 | 本地证据 |
|---|---|---|
| Ferhatosmanoglu, Stanoi, Agrawal, El Abbadi, Constrained Nearest Neighbor Queries, SSTD2001 | 凸几何约束NN，交集区域mindist与witness上界修正；非任意Boolean元数据 | [全文](2001-constrained-nearest-neighbor/paper.md)、[来源](2001-constrained-nearest-neighbor/metadata-2026-10-30.json) |
| Babenko, Lempitsky, The Inverted Multi-Index, TPAMI2015 | product-cell posting、多序列枚举、ANN候选与PQ精排；centroid排序不是raw-vector安全界 | [全文](2015-inverted-multi-index/paper.md)、[来源](2015-inverted-multi-index/metadata-2026-10-30.json) |

IMI journal明确扩展CVPR2012，不强合并成同一索引别名；CNN2001不与同名2017百科条目合并。任意F、层级更新、共同FP32认证及作者性能均未复现。当前用户允许的量化组件仍为IGP，收录IMI只用于查重，不因此实施PQ。见[联合机制边界](../../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/joint-certificate-analysis.md)。
