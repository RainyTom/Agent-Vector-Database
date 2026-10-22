# 检索日志

日期：2026-10-22。检索目的：确认“时间窗约束的语义 episode 检索”是否已有直接工作，并定位最近邻基线。

## 检索范围

来源：Google Scholar 风格网页检索（经 `web_search`）、arXiv API/HTML、IEEE/ACM/Springer 页面、ScienceDirect、SIGSPATIAL/KDD/NeurIPS 页面。未做系统引用闭包，未检索中文数据库，未覆盖所有学位论文与工业系统文档。

## 检索词与命中

| 检索词 | 主要命中 | 判断 |
|---|---|---|
| spatio-temporal filtered approximate nearest neighbor search vector embeddings | Filtered ANN surveys, TANNS, CubeGraph | 时空过滤 ANN 已有多条线；需序列级差异 |
| semantic trajectory similarity search vector embedding retrieval | TISIS, DASS, OmniTraj, TrajPrism, semantic trajectory surveys | 语义轨迹检索活跃；多为整轨迹 |
| time-aware vector search temporal filtered ANN recency ranking | TANGO/TDVS, Re3, TEMPS | 时间衰减/新近性已有直接工作；不能作为新意 |
| geo-aware approximate nearest neighbor search location filter embeddings | LIST, Location Matters, JAG | 空间感知 embedding/索引已有 |
| spatio-temporal retrieval augmented generation geospatial vector database mobility embeddings | CubeGraph, DFR-Gemma, MobilityDB | 时空 RAG 已出现；CubeGraph 最接近 |
| trajectory vector database nearest neighbor search index learned trajectory embeddings | TISIS, trajectory similarity surveys, TrajCL | 学习轨迹表示成熟；索引多为整轨迹 |
| spatio-temporal vector similarity join event matching embeddings location time | 传统 trajectory join；无直接“语义+时空”向量 join | 组合可能开放，但本轮不做 |
| semantic trajectory join similar trajectories POI sequence embedding index | REST, Parallel Semantic Trajectory Similarity Join, TISIS | 语义轨迹 join 已有；与 episode 检索不同 |
| route-aware spatial keyword search along route retrieval | KORS, DESKS, IG-Tree, SemaSK | 路线/空间关键词搜索成熟；非 embedding episode |
| trajectory filter approximate nearest neighbor vector search corridor query | 轨迹查询/索引综述；无直接语义 corridor ANN | 可能开放，但证据不足 |
| moving region spatio-temporal query nearest neighbor trajectories index | 传统 MOD 查询；无 embedding | 邻近但不同 |
| sequence of embeddings nearest neighbor search subsequence matching vector database | 序列搜索/子序列匹配；无轨迹 episode 时间窗 | 通用序列检索有，但未结合时空约束 |
| DTW nearest neighbor search learned embeddings index sequences approximate | 学习轨迹相似度、DTW 综述 | 近似 DTW 索引有限；需查证 |
| semantic subtrajectory matching retrieval POI embeddings time-aware | TISIS, SimSub, subtrajectory surveys | 无直接命中；候选差异 |
| POI sequence retrieval temporal constraints embedding index check-in trajectories | TISIS, POI 推荐/表示学习 | 推荐为主，检索索引少 |
| time-aware semantic trajectory similarity retrieval timestamps alignment | TrajCL, ST2Vec, START, DASS | 表示学习为主；非索引 |
| temporal semantic retrieval query by change pattern entity embeddings index | 语义变化检测（词/实体）、LiveVectorLake | 变化检测成熟；检索索引待查 |
| diachronic semantic search retrieval entities by meaning change over time | 词义变化、TEMPO/TEMPS | 文档级时间检索；非轨迹 |
| place embedding evolution over time urban neighborhood retrieval | Urban2Vec, 区域 embedding, Yelp 评论变化 | 城市计算已有；检索问题待查 |
| semantic trajectory benchmark dataset retrieval evaluation | Massive-STEPS, TrajPrism, OmniTraj | 数据集可用；评测协议未覆盖 episode+时间窗 |
| subtrajectory query semantic embedding time window episode retrieval mobility | 几何子轨迹、视频子轨迹；无直接命中 | 本问题候选差异 |

## 最近邻工作清单

必须精读/补全的条目：

1. TANNS/TKDE 2026（DOI 10.1109/TKDE.2026.3722749）：全文、interval 查询、更新成本。
2. CubeGraph（arXiv:2604.06616）：全文，确认时间维度是否实际支持。
3. TANGO（arXiv:2609.00548）：全文，确认是否支持序列/多向量查询。
4. TISIS（arXiv:2409.11301）：实验部分、索引结构、复杂度。
5. TrajPrism（arXiv:2605.10782）：检索协议、是否含时间窗/episode。
6. OmniTraj（arXiv:2505.17437）：查询类型与索引。
7. STKST-I（ScienceDirect S0957417423005663）：正式书目、时间约束语义。
8. SimSub / 子轨迹搜索（PVLDB 16, arXiv:2003.02542）：几何 vs 语义边界。
9. REST / 语义轨迹 join：与 top-k episode 检索的边界。
10. 时空关键词搜索（DESKS/KORS/IG-Tree/SemaSK）：路线约束与查询语义。

## 覆盖限制

- 未做前后向引用闭包；可能漏掉 2026 年新预印本。
- 未系统检索 SIGMOD/VLDB/ICDE 2020–2026 的 proceedings 全文，只依赖搜索命中。
- 未检索工业界 vector DB 文档中的“trajectory/geo filter”实现。
- 未核实部分论文的正式 venue/DOI；表中已标注。
- 不能据此宣称“无人研究”；只能说明本轮检索未发现直接同问题工作。

## 待补查

- “semantic episode retrieval”“trajectory episode query”“time-windowed subtrajectory”同义词。
- “mobility pattern retrieval”“activity sequence retrieval”。
- “route corridor filter vector search”。
- “temporal contrast search”“change retrieval”。
- 2026 年 SIGSPATIAL/CIKM/WSDM 的最新语义轨迹检索工作。
