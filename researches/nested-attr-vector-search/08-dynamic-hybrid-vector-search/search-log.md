# 08 方向检索日志

## 2026-10-29 阶段 1 三路并行检索

| 线 | 覆盖 | 主要检索词 | 篇数 | 边界 |
|---|---|---|---|---|
| 动态/可更新 ANN | FreshDiskANN、SPFresh、IP-DiskANN、Creator、LSM-VEC、EMA、MCI、Curator、DIGRA、RangePQ+、DSG、HNSW(TPAMI)、Milvus/Qdrant/Weaviate 一致性文档、Cutting/Lester/Büttcher、MicroNN、Slipstream、FGIM | dynamic/updatable graph ANN, streaming DiskANN, vector index deletion/tombstone, LSM vector index, attribute update without rebuild, MVCC vector database | 19 | 层级属性更新协议专项检索（hierarchical label/taxonomy update/reassign + ANN）无命中；同义词复核（taxonomy relabeling、category reassignment、directory move、subtree migration）**待补一轮** |
| 多属性混合过滤 | ACORN、TFANNS、Curator、Filtered-DiskANN、ELI、LSSG、RWalks、SeRF、UNIFY、DSG、NHQ×2、AnalyticDB-V、Milvus、VBase、iFVS、UniFilter、UNG、HQANN、iRangeGraph、VADER、Grant、Graph Range Filters、PathSeer、Zhu et al. survey (PVLDB 19(2)'25) | filtered ANN multi-attribute, hybrid query vector, set containment/overlap filtering, attribute-vector joint index, pre/in/post filter survey | 20 | UniFilter 原文未读；set containment join/LSH Ensemble × ANN 结合未展开；joint embedding 严肃 benchmark 未找到（标尚未充分排查） |
| 联邦/分片搜索 | CORI、ReDDE、Broder tiered、Lu&Callan P2P、CRCS、Shokouhi&Zobel、Montague&Aslam、RRF、SUSHI、Taily、Selective Search(Kulkarni&Callan)、Rank-S、SHiRE、cascade ranking、query-biased partitioning、LTR resources、Dynamic Shard Cutoff、Lin eCom FFN、Facebook EBR、Milvus、Vespa、Gottesbüren graph partitioning、SHINE、Semord、PLAID SHIRTTT、Hierarchical BM25、TA/NRA、ES routing | federated search shard selection, Taily/Rank-S/CRCS, selective search, per-shard budget allocation, early stopping top-k aggregation, distributed HNSW routing, hierarchical shard | 28 | Hierarchical BM25 (2608.00229) 与 Semord (2609.25514) 仅核题录未读正文；sample-ANN 路由可能在工业 RAG 多库路由中以工程形式存在（尚未充分排查） |

## 待补查

1. 层级更新协议同义词复核轮（G1 空白的保留条款）。
2. UniFilter（PACMMOD 3(6)'25）全文；Hierarchical BM25、Semord 全文。
3. LSH Ensemble / set containment join × ANN 的结合（G6 的相邻领域）。
4. "RFD" 任务书术语未定位到标准文献（按 RRF + sample-based 覆盖该语义）。

## 2026-10-29 独立交接审计与前沿机制复核

先查`papers/papers.jsonl`、`relations.jsonl`、现有全文/研究笔记及完整交接。先检索`vector similarity join hierarchical`、`vector join attribute`、`inner-product similarity join`，再查`PathSeer Adaptive Neighbor 2026`、`vector search subtree maintenance taxonomy reparenting`、`vector search adaptive partition Quake`；直接join首轮为范围核对，未完成全部同义词和引用闭包。本任务属于B类单查询filtered ANN，不是几何vector join。

原始来源：TrieHI arXiv、ELI正式PVLDB全文、Quake OSDI正式稿；ACORN/Curator/iRangeGraph/Filtered-DiskANN核本地原文与Crossref；PathSeer ACM403，使用已有正式PDF镜像文本；VADER v2与EMA v1为预印本，正式发表状态不推断。所读章节及链接见[机制审计](2026-10-29-exps/literature-mechanisms-review.md)、[独立判断](2026-10-29-exps/independent-review.md)。

纠正旧检索断言：TrieHI已有MOVE/MERGE，Curator已有Bloom删除重算、复杂布尔临时索引/缓存；Quake已有动态多层预算，VADER已有filter-aware召回预测。没有“更复杂图才能创新”或“没查到即空白”的结论。新增Quake/EMA的章节证据保存在机制审计，不因尚未完成统一主题归档、全书目/版本/实验字段就补造完整论文记录；作为重点候选待入总库。

范围缺口：sample-ANN路由的完整引用链、taxonomy relabeling/subtree migration同义词、工业多库路由、MVCC二级索引、reverse-containment专用加速及SeRF/UNG/FreshDiskANN本轮独立正文复核仍待补。没有执行作者实现或新的ANN性能矩阵。

