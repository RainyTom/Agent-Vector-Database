# 检索日志

日期：2026-10-23。检索目的：确认“时间窗约束的语义 episode 检索”是否已有直接工作，并定位最近邻基线。

## 检索范围

来源：Google Scholar 风格网页检索（经 `web_search`）、alphaXiv 检索/全文抽取脚本、arXiv API/PDF、Crossref、IEEE/ACM/Springer/ScienceDirect 页面。未做完整引用闭包，未检索中文数据库，未覆盖所有学位论文与工业系统文档。

## 检索词与命中

| 检索词 | 主要命中 | 判断 |
|---|---|---|
| spatio-temporal filtered approximate nearest neighbor search vector embeddings | Filtered ANN surveys, TANNS, CubeGraph | 单对象检索；排除出问题新颖性审计，仅保留执行机制参考 |
| semantic trajectory similarity search vector embedding retrieval | TISIS, DASS, OmniTraj, TrajPrism, semantic trajectory surveys | 语义轨迹检索活跃；多为整轨迹 |
| time-aware vector search temporal filtered ANN recency ranking | TANGO/TDVS, Re3, TEMPS | 单对象有效性/新鲜度检索；不是 episode 问题 |
| geo-aware approximate nearest neighbor search location filter embeddings | LIST, Location Matters, JAG | 单对象空间检索；不是轨迹问题 |
| spatio-temporal retrieval augmented generation geospatial vector database mobility embeddings | CubeGraph, DFR-Gemma, MobilityDB | 输入输出单位不同；不再称 CubeGraph“最接近” |
| trajectory vector database nearest neighbor search index learned trajectory embeddings | TISIS, trajectory similarity surveys, TrajCL | 学习轨迹表示成熟；索引多为整轨迹 |
| spatio-temporal vector similarity join event matching embeddings location time | 传统 trajectory join；无直接“语义+时空”向量 join | 组合可能开放，但本轮不做 |
| semantic trajectory join similar trajectories POI sequence embedding index | REST, Parallel Semantic Trajectory Similarity Join, TISIS | 语义轨迹 join 已有；与 episode 检索不同 |
| route-aware spatial keyword search along route retrieval | KORS, DESKS, IG-Tree, SemaSK | 路线/空间关键词搜索成熟；非 embedding episode |
| trajectory filter approximate nearest neighbor vector search corridor query | 轨迹查询/索引综述；无直接语义 corridor ANN | 可能开放，但证据不足 |
| moving region spatio-temporal query nearest neighbor trajectories index | 传统 MOD 查询；无 embedding | 邻近但不同 |
| sequence of embeddings nearest neighbor search subsequence matching vector database | 序列搜索/子序列匹配；无轨迹 episode 时间窗 | 通用序列检索有，但未结合时空约束 |
| DTW nearest neighbor search learned embeddings index sequences approximate | 学习轨迹相似度、DTW 综述 | 近似 DTW 索引有限；需查证 |
| semantic subtrajectory matching retrieval POI embeddings time-aware | TISIS*、GTRSS/TRSSQ、SimSub | GTRSS 已命中代表子轨迹 top-k 输出；语义与硬时间可行性组合仍待查 |
| POI sequence retrieval temporal constraints embedding index check-in trajectories | TISIS, POI 推荐/表示学习 | 推荐为主，检索索引少 |
| time-aware semantic trajectory similarity retrieval timestamps alignment | TrajCL, ST2Vec, START, DASS | 表示学习为主；非索引 |
| temporal semantic retrieval query by change pattern entity embeddings index | 语义变化检测（词/实体）、LiveVectorLake | 变化检测成熟；检索索引待查 |
| diachronic semantic search retrieval entities by meaning change over time | 词义变化、TEMPO/TEMPS | 文档级时间检索；非轨迹 |
| place embedding evolution over time urban neighborhood retrieval | Urban2Vec, 区域 embedding, Yelp 评论变化 | 城市计算已有；检索问题待查 |
| semantic trajectory benchmark dataset retrieval evaluation | Massive-STEPS, TrajPrism, OmniTraj | 数据集可用；评测协议未覆盖 episode+时间窗 |
| subtrajectory query semantic embedding time window episode retrieval mobility | GTRSS、ICDE 2024 TRSSQ、几何 SimSub | 输出语义已有直接前序；未核到同时具有 semantic embedding + 绝对时间/时长/空间硬约束 |

## 用户给定论文的逐篇核查

| 工作 | 已核证据 | 本轮分类 |
|---|---|---|
| Towards Quantum Range Query…，arXiv:2609.29612v1 | 全文4页、形式模型、Table 1、Conclusion | 轨迹时空混合 **range reporting**；不是 top-k。形式模型有 semantic，实验未实现 semantic |
| TISIS，arXiv:2409.11301v2 | 问题定义、TISIS* contextual POI、LCSS | POI embedding threshold trajectory search；timestamp 只保序 |
| REPOSE，arXiv:2101.08929 / DOI:10.1109/ICDE51399.2021.00067 | Definition 3、RP-Trie/Spark、正式书目 | 分布式完整轨迹 top-k |
| N-tree，arXiv:2408.07650 / DOI:10.1145/3716825 | exact kNN/range、DistanceAvg、正式书目 | 精确完整轨迹 metric search；DistanceAvg 含时空距离 |
| ST2Vec，arXiv:2112.09339 / DOI:10.1145/3534678.3539375 | 时空学习目标、KDD 正式题名/作者 | 完整路网轨迹的时空软相似度；top-k 为下游 case study |
| GTRSS，arXiv:2507.05542v1 | Definitions 1–4、DTW/EDR/ERP、GARI/CNDI | **当前输出语义最近**：每轨迹最佳连续子轨迹，再取全局 top-k |
| Trajectory Similarity Survey，arXiv:2303.05012 / DOI:10.1109/TKDE.2023.3323535 | taxonomy、Table 1、benchmark dimensions | 距离定义与评测背景；不是直接方法 |

## 直接问题家族的补查优先级

1. **最高优先级**：`Efficient Learning-based Top-k Representative Similar Subtrajectory Query`（ICDE 2024）。GTRSS 明确称其为 TRSSQ 直接前序；需获取全文、DOI、代码及引用闭包。
2. GTRSS/TRSSQ：核查完整实验、近似保证、窗口/约束扩展与所有后续引用。
3. ETQ（ICDE 2017）：前后向引用闭包，确认顺序扩展及语义子轨迹后续工作。
4. TISIS*：实验、索引复杂度及引用闭包；已确认 Word2Vec POI 匹配。
5. STKST-I：正式书目、时间约束究竟是软相关性还是硬范围。
6. TrajPrism/OmniTraj：是否已有 time-windowed episode 协议。
7. REST / semantic trajectory join：与单查询 representative-subtrajectory top-k 的边界。
8. 时空关键词路线搜索（DESKS/KORS/IG-Tree/SemaSK）：是否输出连续段。

Quantum Range Query 属于同一轨迹混合数据域，但算子是 range reporting；REPOSE、N-tree、ST2Vec 属于完整轨迹相似检索。ACORN、Filtered-DiskANN、TANNS、TANGO、CubeGraph、LIST 是单对象检索，只在采用其执行机制时补读实现细节。

## TRSSQ（ICDE 2024）获取记录

- 书目：[DOI:10.1109/ICDE60146.2024.00335](https://doi.org/10.1109/ICDE60146.2024.00335)，pp. 4396–4408，2024-05-13；作者 Kunming Wang, Shiyu Yang, Jiabao Jin, Peng Cheng, Jianye Yang, Xuemin Lin。Crossref、OpenAlex `W4400909989`、ICDE 2024 官方论文列表三处一致。
- 全文尝试：Unpaywall `oa_status=closed`；IEEE `stamp.jsp` 与 `iel8/.../10598106.pdf` 返回 HTTP 202（0 字节）；CSDL 页面无摘要；Crossref 的 xplorestaging 链接返回 HTML；Semantic Scholar 429；arXiv 无此文。
- 已得替代证据：OpenAlex 摘要（末句被截断）+ GTRSS §I–II 对 TRSSQ/RSSE 的明确转述（GRU 点级匹配、轨迹编码对齐、filter-validate）。
- 引用闭包：OpenAlex 记录被引 2 次（MATIBL, IJCNN 2025；SimCFT, TKDE 2026），均非受约束/语义 TRSSQ 后续；GTRSS 尚未进入 OpenAlex 引用。
- 结论：**未获取全文，不写入 `papers.jsonl`**；证据存于 `researches/semantic-spatiotemporal-hybrid-search/evidence/trssq-2026-10-23.md`。

## 覆盖限制

- 未完成 GTRSS/TRSSQ、ETQ、TISIS* 的前后向引用闭包；可能漏掉 constrained/semantic representative-subtrajectory 工作。
- 未系统检索 SIGMOD/VLDB/ICDE/SIGSPATIAL/CIKM/KDD 2017–2026 全部 proceedings。
- Quantum Range Query 当前是 arXiv v1，稿件页眉/DOI 占位；正式发表状态未核实。
- STKST-I、TrajPrism、OmniTraj 等仍只有摘要级证据。
- 本轮只证明各论文的已核问题定义；没有运行算法或复核其性能数字。
- 不能据此宣称“无人研究”或“组合必然构成新问题”。

## 待补检索词

- `constrained representative subtrajectory query`
- `semantic representative subtrajectory retrieval`
- `time-windowed subtrajectory top-k`
- `activity trajectory episode search`
- `POI sequence subtrajectory retrieval`
- `trajectory segment keyword time range query`
