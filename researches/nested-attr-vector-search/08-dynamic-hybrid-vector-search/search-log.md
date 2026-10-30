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


## 2026-10-29 表示变换与过滤剪枝

- 先查总库/关系/本地材料，先搜`vector similarity join hash`和`inner-product similarity join filter`，再搜`FlyHash multi-vector`、`FlyHash MaxSim ColBERT`、`multi vector search fruit fly hashing sparse representation`。前几次窄词未定位，宽检索找到arXiv2412.03301v2；核v1/v2原文、正式出版方/机构书目和作者仓库关键代码，定位BioVSS/BioVSS++。
- 机制检索：`filtered vector search lower bounds`、`predicate geometric pruning vector index`、`nearest neighbor sparse vector residual norm upper bound inverted index dense pruning`。核读MUVERA、Seismic、L2AP、Curator、FINGER相关正文；FCVI §3是soft filter objective，iFVS §2是条件codebook打分，均不等于原L2安全界。
- 主要发现：普通稀疏倒排＋残差证书有L2AP直接前序；几何posting组摘要有Seismic前序。优先问题改为不扫描eligible、不逐谓词物化而取得条件extrema/区间排除证书。该问题新颖性尚未充分排查。
- 来源失败保留：FCVI声明DOI的Crossref404；VA-file PDF提取/查询失败；L2AP正式页动态内容未给可读书目，本轮不补猜DOI。BioVSS正式ICDE身份由其他原始来源交叉核实。
- 待补：VA-file原文、range-min/successor/wavelet/bitmap条件极值数据结构、dense-to-sparse覆盖与signed/un-normalized L2转换、条件证书的属性更新复杂度。没有搜索到同名方案不是不存在证明。
- 详证：[新机制判断](2026-10-29-exps/pruning-index-directions.md)、[前序对照](2026-10-29-exps/pruning-prior-art-2026-10-29.md)。本轮只运行合成逻辑smoke，无新ANN矩阵。


## 2026-10-29 七篇指定论文的全文机制学习

- 先查总库30条与27关系及原文；直接join范围检索使用`"vector similarity join" "hierarchical" OR "high-dimensional similarity join" "attributes" OR "inner-product similarity join"`，返回DiskJoin/PODS/FastAVJ/PHiDJ等范围线索，未当成新的join综述。随后按七篇精确题名、DOI、arXiv/作者主页/仓库核查。
- MSG读正式SCIS全文；Hubness读正式PACMMOD镜像全文；VikingMem/Blend读官方PVLDB正式全文；Flash读arXiv v1并核正式元数据；Directory-Aware复核已有v1全文。CDMG正式ACM各入口403，经GitHub历史恢复2025-05-08技术报告，未冒称正式29页精读。
- 发现MSG仍有指数级组合邻接，CDMG理想路径/融合近似/实际GreedySearch保证不可混同，Hubness不能简化成均匀入度更好，Blend相关性融合不等于属性过滤。用户再次明确层级＋多属性硬过滤，已写入任务书。
- 详见[七篇原文复核](2026-10-29-exps/seven-paper-inspiration-review.md)；候选A/B均未完成最近前序证明级查重。待核：条件遮挡/剪枝证据、共享适用域的FilteredVamana/UNG/Packing等；条件极值的range/wavelet前序；CDMG正式版差异。
- 新增6条、修订1条，2条原文引用；仅运行5项合成语义反例。没有作者代码复现或新ANN性能网格。

## 2026-10-30 真实诊断的上游归因复核

- 先核本分支论文/关系与既有机制审读，完整读7756064报告、机制审查、协议、阅读清单、复现及诊断源码。服务器与七篇审读分支共同祖先01ef7d6，不覆盖任一分支记录；服务器稀疏B与上游条件遮挡B不同。
- 直接join范围检索：`"vector similarity join" "pivot" OR "inner-product similarity join" "norm"`；再检索`Omni family all purpose access methods similarity queries 2001 pivot distance lower bound pdf`、`"Pivot-based metric indexing" "pdf"`、`product quantization distance lower bounds exact nearest neighbor subspace minimum sum admissible`。搜索回答只作线索，不把AI推导当论文保证。
- 原文核查PVLDB2017 Pivot-based Metric Indexing §2.3、§5.1–5.2，确认三角不等式pivot下界、球界及距离区间索引为经典控制。ACM入口403，改用官方PVLDB全文成功。OMNI2001出版页无可读正文，未冒称全文精读；PQ论文线索未核正文，不据此主张新的量化界或实施PQ。
- 新数据分析仅用既有64行root/scope-only控制，去basis重复为32个case；WIKI/SIFT无过滤oracle分别88.4710%/92.0365%，阈值与过滤pooled不同，不直接相减。未重跑服务器失败/诊断或新ANN。具体来源、脚本、限制见[上游报告](../2026-10-30-exps/report.md)。
- 下一步E0为同最终τ下D_B/D_H/D_F归因；有机会才E1嵌套联合阶数及范数/固定中心控制。候选仍未通过新颖性门；经典下界或范围查询不换名立项，但不要求所有基础原语全新。
- 文献仅加章节证据候选，未新增完整总库记录；本分支仍36工作/29关系，与服务器36/31是不同集合，待显式整合后才能报联合计数。

## 2026-10-30 用户纠偏：从证书单线扩展跨机制族

- 先查总索引/既有机制审读，复核ACORN v1 §5.2–5.3、Curator v3 §4.2原文。直接join范围检索`"vector similarity join" "index" "filter" OR "inner-product similarity join" "index"`，再检索`"filtered vector search" "bitmap" "index" OR "hybrid" "Trie" "ANN"`，定位SIEVE及其他综述线索。
- 新读SIEVE arXiv2507.11907v2 §1–3、§4.1开头；其明确复用HNSW/bitmap和集合选择，将贡献定位到size/time/recall模型与服务机制。仅保存相关节选，正式版与全部实验未核，不新增完整精读条目；新搜到的survey/system-analysis仅作线索，不冒称精读。
- 修正过强门禁：基础组件已有不等于不能使用；不要求所有方法产生安全L2下界或先完成证书E0/E1。新增六族研究卡与跨族筛选协议，区别探索门和贡献门，详见[机制方案](../2026-10-30-exps/index-mechanism-portfolio.md)。
- 同时收紧旧“新观察”：过滤遮挡点造成剪枝路径失效已在ACORN正文明确论述，可作设计起点，不可当首次发现；可研究具体共享/恢复/更新机制，不能据前序存在关闭整个图族。
- 无新ANN或真实数据筛选实验；所有新机制卡未执行。待补查项按入选具体方案展开：F1资格接口与联合视图、F2共享集合/层级布局、F3候选恢复、F4双空间分区、F5代理候选覆盖；不一次穷尽全领域才允许小探索。
