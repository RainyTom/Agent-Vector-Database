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


## 2026-10-30 条件证书直接前序与真实信息诊断

- 延续先查总库和关系的流程，直接join回溯L2AP，再核读BSI算术、Boolean potential-answer extrema、wavelet range quantile/next-value/交集、dynamic sequence rank/select。正文六篇已归档：L2AP在vector-join，BSI/论文/sequence/wavelet在data-indexing，Seismic在sparse-vector-search；不把所有组件归为filtered ANN。
- L2AP DOI现由正式书目交叉核为10.1109/ICDE.2014.6816700；BSI两DOI别名对应同一work。Navarro/Nekrich分析的是arXiv v2/SODA2013，2014同名期刊映射仍单列而非强行合并。新增BioVSS日期化正文审读，保留尺度、同成员绑定及版本歧义。
- 直接前序否定“共享排序+F极值”或“部分倒排+残差”本身为新机制；普通wavelet区间复杂度不能直接套任意Boolean F。BSI/range前序不代表所有联合条件证书都已解决。
- 先冻结[协议](2026-10-30-exps/conditional-geometry-protocol.md)，后跑真实WIKI11869/SIFT12000、各8query的K/basis/F诊断。9216记录不是9216独立query。SIFT有条件信息但WIKI独立坐标界弱；当前取得成本和新颖性未过门，不进入C++新索引。
- run1计时漏普通比较的失败保留；run2修正并完整重跑，信息/非时间工作量逐项相同。没有新ANN性能或动态更新实验。见[报告](2026-10-30-exps/report.md)、[复现](2026-10-30-exps/experiments/reproduction.md)。
- 仍待核：VA-file原始正文、任意F与多顺序条件查询的更广前序、共享联合证书是否能填补WIKI真实块距离oracle与独立轴界鸿沟；不声称穷尽文献或已发明该结构。

## 2026-10-30 第2轮：联合几何、投影损失与更近前序

- 父代理先发出`"similarity join" "lower bounds" "subspaces"`，窄词结果多不直接相关；换`"vector similarity join" OR "high-dimensional similarity join" "projection"`找到Ma/Jia/Zhang的2017候选DOI10.1002/cpe.3952，正文未读，列待办，不填造方法/实验。
- 两个只读研究代理并行查joint cells/VA/PQ/受限NN与metric pivots/Hilbert；父代理核读IMI journal、CNN2001、simplex v1关键正文并归档三份完整reader文本。后者查询`"High-Dimensional Simplexes for Supermetric Search" DOI`并以Crossref核正式2017/96–109；CNN Crossref初次命中同名2017百科，按2001日期/作者过滤后核真正proceedings DOI，未混同。
- VA-file已获得原始论文mirror转录§4.1，非原PDF；VA+已有UCSB2000-10原PS关键段，不能套CIKM不同页码。Omni PDF提取失败，仅摘要核；Ptolemaic/Hilbert线索未全部归档。更广dynamic任意F联合emptiness查重仍有缺口。
- [事前协议](2026-10-30-exps/joint-information-protocol.md)后实际运行两进程。WIKI同对象64维oracle仍0排除，纠正“只缺联合见证”；SIFT联合收益需扣掉wholeB。全部r/g/order/control保留，不只报最佳点。
- [结构分析](2026-10-30-exps/joint-certificate-analysis.md)说明联合tuple/余空间范数有直接前序；ANN返回近似min是上界，不能冒充安全下界。无新取得操作则不接入HNSW，不回退维护路线。
- [报告](2026-10-30-exps/joint-certificate-report.md)保留计数instrumentation错误与完整run2重跑；173568记录只有16源query，不是ANN性能或动态更新结果。
