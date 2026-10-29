## Summary

1. **VikingMem 是 LLM 长期记忆管理系统，不是 TrieHI 目录过滤索引论文。** 它的实质是 schema 驱动的 Event–Entity 模型、单遍抽取、事件驱动实体更新、时间压缩及多路语义召回。正文里的结构化属性、时间窗口、按用户/主题分组，可以支撑我们研究动态外部属性的应用动机；**不能据此声称它已定义“祖先/后代/subtree AND 多属性谓词，再按原始 FP32 squared-L2 排序”的问题。**
2. **Blend 的 hybrid 明确是 lexical–semantic 多路融合，不是 scalar-filtered ANN。** 其 TRF 使用 token-level MaxSim，不能移植为本项目最终排名函数。最有价值的是拆分候选覆盖、排名误差、路径成本的实验方法，而不是直接照搬“弱路径污染”结论。
3. 两篇**均已有 2026 年正式 PVLDB 版本**。尤其 Blend 不能只标“2025 arXiv 预印本”；VikingMem arXiv HTML 中的 `10.14778/XXX` 是占位符，正式 DOI 已存在。
4. 全程只读；未运行系统、build、lint、tests 或 formatter，未修改仓库。

---

# Report

## 1. 书目信息、版本和可持久化原文

| 项目 | VikingMem | Balancing the Blend |
|---|---|---|
| 完整题名 | **VikingMem: A Memory Base Management System for Stateful LLM-based Applications** | **Balancing the Blend: An Experimental Analysis of Trade-offs in Hybrid Search** |
| 完整作者顺序 | **Jiajie Fu, Junwen Chen, Mengzhao Wang, Aoxiang He, Maojia Sheng, Xiangyu Ke, Yifan Zhu, Yunjun Gao** | **Mengzhao Wang, Boyu Tan, Yunjun Gao, Hai Jin, Yingfeng Zhang, Xiangyu Ke, Xiaoliang Xu, Yifan Zhu** |
| arXiv 版本历史 | v1：2026-05-28；v2：2026-05-29；v3：2026-06-12 | v1：2025-08-02；v2：2025-11-03 |
| 本轮核读 HTML | [2605.29640v3](https://arxiv.org/html/2605.29640v3) | [2508.01405v2](https://arxiv.org/html/2508.01405v2) |
| 固定版本 PDF | https://arxiv.org/pdf/2605.29640v3 | https://arxiv.org/pdf/2508.01405v2 |
| arXiv DOI | https://doi.org/10.48550/arXiv.2605.29640 | https://doi.org/10.48550/arXiv.2508.01405 |
| **正式状态** | **PVLDB 19(12): 4344–4357, 2026** | **PVLDB 19(8): 1715–1730, 2026** |
| **正式 DOI** | **https://doi.org/10.14778/3827998.3828037** | **https://doi.org/10.14778/3811243.3811246** |
| **正式原始 PDF，推荐父任务保存** | **https://www.vldb.org/pvldb/vol19/p4344-ke.pdf** | **https://www.vldb.org/pvldb/vol19/p1715-gao.pdf** |
| 正式版 artifact 链接 | https://github.com/BytedanceFu/VikingMem | https://github.com/infiniflow/infinity |

版本日期据 arXiv abstract 页的原始 HTML submission history；作者顺序同时核对正式 PDF 的 **PVLDB Reference Format**，避免双栏 PDF 文本抽取打乱姓名。

**实质版本差异：**
- VikingMem arXiv HTML 保留卷期、页码、DOI 占位符；正式 PDF 已补全。正式版 Table 4 将“移除组件”的延迟变化写为负值，arXiv HTML 显示正值；解释时应写“该组件引入约 6.8/25.8 ms 开销”，避免符号混淆。
- Blend 正式版 §4.1.2 / Table 4 **新增 Qwen3-Embedding-8B 与 KaLM-Embedding-Gemma3-12B-2511**；arXiv v2 的该表只有 NV-Embed-v2。正式版也把 TRF 相对轻量融合的表述由 “consistently” 收敛为 “generally”，与表格反例更一致。
- 未逐字对比所有早期版本；这里区分的是已核读的最新 arXiv 与正式出版版本，不虚构 v1→v2 的全部变化。

---

## 2. VikingMem：机制、证据与任务边界

### 2.1 真正研究了什么

**问题目标不是返回几何最近邻，而是提供能提高 LLM 回答效果的长期记忆上下文。** §2.3 的目标是提升 \(\mathcal L(Q,M)\) 的响应准确性、连续性、相关性，并降低抽取和检索成本。

主要架构与机制：

| 原文位置 | 机制实质 | 对新颖性的判断 |
|---|---|---|
| [§2.2.1–2.2.2](https://arxiv.org/html/2605.29640v3#S2.SS2)，正式 PDF pp.3–4 | Event 是带时间戳、受 schema 约束的离散记录；Entity 是事件增量维护的持久状态。`entity := SELECT OP(event.content) FROM Events WHERE filters(event) GROUP BY keys(event)`；keys 可为用户、用户–助手对、主题，filters 可为时间窗口 | 研究价值在于把记忆演化抽象成可配置的数据管理接口，而非每种应用独立 prompt。它本质上借用了事件日志/物化视图思想，不是新的几何索引 |
| [§3.1](https://arxiv.org/html/2605.29640v3#S3.SS1)，Algorithm 1，正式 pp.5–6 | schema 合成单个 prompt，单次读取原始流抽取多种记忆；EUA 让抽取器输出 SEARCH/REPLACE patch，用最小编辑距离寻找旧字段中的替换 span | EUA 的具体价值是省去实体字符串更新的额外 LLM 调用；**仍由 LLM 产生 patch**，并非无 LLM 更新全过程。先 ANN 找相关实体 top-5，不保证穷尽正确实体 |
| 同节 IMSM | 先剔除低价值片段，再输出语义片段坐标，合并主题相同但不连续的对话段 | 属于记忆抽取/表示机制；这里的“过滤、剪枝”是语义抽取，不是可证明安全的向量候选剪枝 |
| [§3.2](https://arxiv.org/html/2605.29640v3#S3.SS2)，正式 pp.6–7 | SUM/COUNT/AVG/MAX、LLM_MERGE、TIME_COMPRESS；按主题时间线 lazy merge，旧事件形成周/月摘要，细粒度记录设置 TTL，召回可强化保留 | 生命周期机制是系统贡献；统计聚合、TTL、缓存本身是已有工程工具。摘要保真是应用语义假设，不是保持原始向量 top-k 的证明 |
| [§3.3](https://arxiv.org/html/2605.29640v3#S3.SS3)，正式 p.7 | 主路径 dense+sparse，辅路径 keyword graph；分路径重排、分别配额后合并；加入 recency/business 权重；ColBERT 式 multi-vector rerank | 多路召回、已有压缩/量化/token merge 的系统集成。没有给出新的 hierarchy-aware L2 剪枝定理 |

其排名明确是：
\[
S_{\rm final}
=(1-w_t-w_b)S_{\rm origin}+w_tS_{\rm time}+w_bS_{\rm business}.
\]
这与本项目“过滤决定资格，原始 FP32 squared-L2 唯一决定排名”不是一个目标函数。

### 2.2 目录、结构化属性、vector scope、生命周期与 TrieHI 的关系

**必须分开三件事：**

1. **VikingMem 的结构化属性与事件/实体分组。**  
   schema 定义 EventType/EntityType、PropertyName/Type/Description、AggregateExpression；用户/主题/时间窗口用于状态生成。这说明真实记忆对象有类型、时间及持续变化的状态；**不等于原文定义了目录祖先/后代谓词或多属性过滤执行算法。**

2. **VikingMem 的检索与生命周期。**  
   正文 §3 把 VikingDB 定位为存储/检索 substrate，把抽取、去重、实体更新和记忆生命周期放在其上。它没有把 Event–Entity 聚合层级等同于外部路径树；TIME_COMPRESS 的“高层摘要”也不是 subtree membership 索引。

3. **TrieHI 属于另一篇原始论文。**  
   [Directory-Aware Query and Maintenance in Vector Databases, 2606.16903v1](https://arxiv.org/html/2606.16903v1) 的 §IV-C 明确说明：**TrieHI 集成于 OpenViking**，用 `viking://` 层级命名空间组织 memories/resources/skills，支持 L0 abstract、L1 overview、L2 full content 和目录递归检索。其 §IV-A 明确 TrieHI 是 ANN 上方的 metadata scope 层，不改变向量排名或 ANN 布局，节点持有 inclusive IDs。  
   VikingMem §1 明确说 OpenViking 开源了它的部分能力；**这构成系统生态联系，不构成“VikingMem 本文提出/使用/评测 TrieHI”的证据。**

**明确引用关系核查：**
- VikingMem §3.2–3.3 的 dense–sparse 引文是 **Haoyu Zhang 等，Efficient and Effective Retrieval of Dense-Sparse Hybrid Vectors Using Graph-Based Approximate Nearest Neighbor Search，arXiv:2410.20381**，不是 Blend。
- 本轮核读 VikingMem 最新 arXiv 正文及正式版参考文献，未见其明确引用 TrieHI/Directory-Aware 或 Blend；不能把共享作者、相似应用或聚合网站的叙述当作引用关系。
- Directory-Aware **§VI “Hybrid Retrieval” 明确引用 Blend [96]**，紧接着说明：DSQ 解决候选资格，dense/sparse/hybrid 解决资格集合内的相关性排名，二者互补。可追溯原文：https://arxiv.org/html/2606.16903v1#S6 。
- 检索中出现第三方页面把 VikingMem 与 DSQ/DSM/TrieHI 混写；这里以两篇原始正文为准。

### 2.3 实验与数字口径

依据 VikingMem §5，正式 PDF pp.8–12，Tables 1–6：

| 证据 | 结果 | 正确口径 |
|---|---|---|
| LOCOMO，GPT-4o-mini | VikingMem **88.83**；Mirix 78.66；Zep 75.14；Mem0 66.88；RAG 60.26 | LLM-Judge 百分制 QA 分数，不是向量 Recall@k |
| LOCOMO，GPT-4.1-mini | VikingMem **90.12**；Full-Context 86.36；Mirix 85.38；Zep 79.09 | 不同生成/评审模型单列，不能混算 |
| LongMemEval，GPT-4o | VikingMem **75.80**；Zep 70.40；Mem0-graph 54.80 | 相对 Zep 为 +5.40 个百分点，约 +7.67% 相对提升 |
| 检索 latency | LOCOMO p50/p95 **0.20/0.39 s**；LongMemEval **0.25/0.89 s** | 包含 VikingDB 候选生成及 VikingMem 分数融合、rerank；不是单纯 ANN 内核，不是答案生成总耗时 |
| One-pass/EUA，LOCOMO 三类记忆 | Multiple Prompts：**$0.35 / 11.02s / 88.86**；one-pass 无 EUA：**$0.11 / 12.32s / 88.83**；有 EUA：**$0.07 / 7.42s / 88.77** | 单遍无 EUA 并不更快；multi-pass 使用并行调用。价格/时间是抽取实验报告，不是查询成本 |
| Retention，LongMemEval | 保存 token 为原始量 **16.82%**，下降 **83.18%**；Judge 75.80 vs Naive RAG 63.81 | **文本 token 存储量**，不是向量、图、bitmap、tensor、总内存节省 83.18% |
| Ablation，LOCOMO GPT-4o-mini | 去 rerank：88.83→85.19；去 Entity：→86.93；去 IMSM：→83.51；去 keyword graph：→86.92 | 分别下降 3.64、1.90、5.32、1.91 **分**；rerank/keyword graph 引入约 6.8/25.8 ms p95 |
| token-level F1 | VikingMem **54.98%** vs Full-Context 53.03% | 对最终回答的 F1，不是候选集合过滤正确性 |

**重要限制/不宜照抄的宣传句：**
- “up to 30%”没有统一说明比较对象及相对/绝对口径；应保存具体表格对比，不改写成“ANN recall 提升 30%”。
- 引言“最低 p95”与 Table 1 不完全一致：LOCOMO Mem0 p95 **0.20s**，小于 VikingMem **0.39s**。
- 引言“p95 降低 900ms”没有在上述主要对照表中给出可唯一匹配的比较口径，不能当成本项目可预期收益。
- **§5.1.5 默认关闭 time-weighting**，因数据主要为事实型问题。因此主实验不能证明时间加权有效，更不能证明动态生命周期索引性能。
- 数据一次导入，答案生成/评审重复三次；不是三次独立重新抽取建库。若干带 † 的质量分数取自已发表结果；系统还采用外部 API，存在环境/版本不可完全控制的问题。
- LOCOMO 10 组长对话；LongMemEval_s 500 组、平均约 115K tokens。没有层级深度、subtree 大小、属性选择率、标签–向量相关性、MOVE 与更新频率的过滤索引实验。
- 没有针对本项目精确资格集合或 squared-L2 top-k 的理论保证。

### 2.4 对本项目可迁移与不可迁移

**真正约束我们的问题的需求：**
- [INFERENCE] 若用长期记忆作为应用背景，必须能表达用户/会话/主题/时间/类型等**外部属性资格**，且属性变化、删除/过期后不能返回陈旧资格结果。
- [INFERENCE] 目录作用域需求应引用 Directory-Aware/OpenViking 的明确原文，不能用 VikingMem 的 Event–Entity 层级代替证明。
- 这些需求只确定 workload 与正确性约束；**不要求我们实现 LLM 抽取、实体合并、时间偏好排名或多向量 rerank。**

**可证伪假设 V：更新边界分离的层级–多属性表示。**  
[INFERENCE] 固定对象向量和终止 scope ID，将层级拓扑/祖先成员与可变类型、时间范围、多值标签成员分离存储；新表示在查询访问的层级/几何块内做精确交集，而非每次预先物化整个大 subtree 的完整 AND bitmap。假设它在**宽 subtree、额外属性选择性强且属性频繁变化**时，能够降低交集准备与更新写放大，并保持同一原始 FP32 L2 真值下的召回。

- 对照：NodeANN 主性能基线，以及 TrieHI scope + 相同过滤执行器；同总字节、同查询/更新轨迹。
- 控制：subtree 大小/深度、属性更新率、标签/范围选择率、属性与向量相关性。
- 反证：精确资格有误；或局部表示维护成本抵消收益；或普通 bitmap 交集更快；或收益只来自向量/答案摘要造成问题改变。
- **不宣称新颖性已确立**：这是由生命周期工作负载激发、仍需对照已有局部/延迟过滤表示的候选，不是 VikingMem 已证明的算法。

**不能迁移：** 语义抽取丢弃“低价值”对象、摘要替代原始记录、时间/business 加权、LLM 判断过滤资格、ColBERT/MaxSim 改变最终排序。

---

## 3. Blend：hybrid 定义、机制与实验有效范围

### 3.1 hybrid 不是 scalar-filtered ANN

[§2–3](https://arxiv.org/html/2508.01405v2#S2) 的四条 retrieval paths：

- **FTS**：BM25 + Block-Max WAND。
- **SVS**：learned sparse inner product + Block-Max Pruning。
- **DVS**：single dense vector inner product + HNSW/LVQ。
- **TenS**：token tensors，ColBERT 式
  \[
  \sum_i\max_j q_i^\top d_j
  \]
  + EMVB/PQ。

**RRF** 按多条路径的 rank 融合；**WS** 按归一化分数加权；**TRF** 对多路径候选集合重新计算 token-level MaxSim。它不是把类别/价格/标签当 AND 谓词，也没有研究目录祖先/后代资格。

### 3.2 贡献实质与工程成分

- **主要研究价值：** 同一框架、四种检索范式及其组合、多个 rerank 策略上的效果–延迟–空间–建库成本实验；揭示多路相关性模型并非总能互补。
- **TRF 的具体机制价值：** 把昂贵 tensor 交互限制到少量候选，而不是为所有查询运行全库 tensor search。
- **并非新提出：** BM25、BMW、BMP、HNSW、LVQ、EMVB、PQ、RRF、MaxSim 的基础算法；列式存储、独立索引、push-based DAG、线程池是执行框架集成与工程实现。
- 没有提供“最弱路径必然降低效果”的定理，也没有本项目层级过滤安全剪枝定理。§6.2 的 adaptive optimizer、动态维护评测等是未来方向，不是已实现机制。

### 3.3 数据、变量和分布

依据 §4 / Table 1，正式 PDF pp.6–7。下表依次是 corpus / queries / 平均文档长度：

| 数据 | 规模与文档特征 |
|---|---|
| MSMA(en) | 8,841,823 / **43** / 56 |
| DBPE(en) | 4,635,922 / 400 / 50 |
| MCCN(zh) | 935,162 / 339 / 1,263 |
| TOUC(en) | 382,545 / **49** / 292 |
| MLDR(zh) | 200,000 / 800 / 4,249 |
| MLDR(en) | 200,000 / 800 / 3,308 |
| TREC(en) | 171,332 / **50** / 161 |
| FIQA(en) | 57,638 / 648 / 132 |
| CQAD(en) | 40,221 / 1,570 / 129 |
| SCID(en) | 25,657 / 1,000 / 176 |
| SCIF(en) | 5,183 / 809 / 214 |

- 来源：BEIR 为主，补 MTEB 与 AIR-Bench；作者称覆盖 9 类任务、7 个领域，除 MSMA 外均 out-of-domain。
- dense/sparse 默认 **BGE-M3**，输入上限 8192 tokens；英文 tensor 用 **answerai-colbert-small-v1，96 维**，中文用 **Jina-ColBERT-v2，64 维**。因此不同 path 的差异同时含**模型差异**，不是仅索引结构差异。
- 主要变量：四路径的 4 个单路+11 个混合组合；fusion 方法；candidate list size；brute-force/EMVB；不同 embedding；不同语料/文档长度与查询特征。
- 默认每路候选 \(k_0=10\)，最终 nDCG@10；RRF \(\kappa=60\)；WS 权重取**相应数据集单路 nDCG@10**，不是无需 relevance 标签的通用在线策略。
- HNSW \(M=16,\ efConstruction=200\)；BMP block size 8；EMVB 8192 centroids、32 PQ subspaces。
- 主表 Table 2 **不含 TenS 检索路径**，因资源高；不能把主表误述为所有四路径组合都在 11 个数据集完整列出。
- 没有控制层级深度、scope 选择率、属性交集分布、vector–predicate correlation。文中“查询长度影响路径强弱”主要是跨数据集观察，**不是只改变查询长度的因果实验**。

### 3.4 关键结果和失败区间

| 原文定位 | 数字与结论 |
|---|---|
| §5.1 / Table 2，正式 pp.7–8 | DBPE：FTS **.565**、SVS **.479**，RRF **.608**，有互补；TOUC：FTS **.650**、DVS **.390**，FTS+DVS RRF **.604**，弱路径融合降低效果 |
| 同表 | MCCN：DVS **.543**、FTS **.222**，RRF **.440**；但 TRF **.624**。说明 fusion/rerank 可显著改变结果，不能把“损害不可逆”绝对化 |
| 同表 | FIQA：DVS **.532**，FTS+DVS RRF **.471**、TRF **.508**，两种融合均未超过强单路 |
| 同表，反例 | CQAD 三路 RRF **.486** > TRF **.465**；TOUC FTS+SVS RRF **.690** > TRF **.677**。TRF 不是全面优于 RRF |
| §5.3.1，正式 p.10 | DBPE FTS+DVS：RRF **.668**→TRF **.722**；差 **.054 nDCG**，相对提升 **8.1%**，不是 +8.1 个百分点 |
| 正式 Table 4，p.9 | CQAD Qwen3-8B：DVS **.573**，三路 RRF **.539**、TRF **.486**。强 dense 模型下加路径/用 tensor rerank 仍可能更差 |
| Table 3 | MLDR(en) 三路 RRF/TRF：Recall@10 **.798/.814**；MRR@10 **.637/.653**。这是 IR qrels 的 Recall，不是 FP32 L2 邻居 recall |
| Table 6，正式 p.10 | MLDR(en) mean/P99，ms：FTS **.37/.53**；DVS **.26/.63**；三路 RRF **.65/.91**；三路 TRF **.87/1.01** |
| §5.2.1 | CQAD 四路 nDCG@10 约 **.50**；延迟为 FTS-only 的 **3.7×**，是特定语料/配置，不是所有查询的常数 |
| §5.3.3 | MLDR(en)，FTS+DVS + TRF 的**峰值内存**比 FTS+DVS+TenS 低 **86%**。不是“比 RRF 省 86%”，也不是总持久化 tensor 数据省 86% |
| §5.3.2 | 作者报告 cross-encoder 排名耗时 >TRF **100×**，并以输入截断解释长文下降；依赖所测模型、输入配置及硬件，不能推广为所有 cross-encoder 的固有限制 |
| §5.3.4，Figs.8/15/16 | 增大候选数准确率收益递减、TRF 时间/内存近线性增长；正文没有给出可通用采用的选择率/长度阈值 |
| §5.4 / Table 7 | DBPE 单查询 `france second world war normandy`：FTS **.624**、DVS **.784**、RRF **.604**、TRF **.880**；TRF 仍把不相关 cemetery 排第三，说明 hard negative 仍在 |

**资源口径：**
- Table 1 最后四列是各数据表示的 corpus size，不宜直接称“最终索引空间”。例如 MLDR(en)：dense **791MB**，tensor **94GB**。
- Query memory 采用 mmap，驻留/峰值内存可能远低于磁盘索引大小。
- 机器为双 Xeon E5-2650 v4、48 logical processors、125GB RAM；embedding 在两张 RTX 5000 Ada 上生成。QPS 是多线程 batch workload。亚毫秒表格不能直接用作本项目单线程、single-query、FP32 L2 的吞吐预期。
- §6.2.2 明确动态 ingestion/update/delete 的维护成本仍需未来研究；offline index construction 结果不能代替动态属性维护结论。
- 无本文支持的“层级谓词选择率低于 X 就会失败”的失败区间；现有失败证据是**特定语料、模型、候选深度与重排组合**。

### 3.5 最重要的不能跨任务迁移之处

**“弱路径污染”在我们的评分语义下不自动成立。**

[INFERENCE，直接由集合 top-k 性质推导] 固定查询、原始向量与资格谓词；若新候选集合是旧集合的超集，对所有合格候选用**同一个精确 FP32 squared-L2** 重新排序、没有额外截断及共享预算挤占，则增加候选不会降低真实 L2 top-k 的覆盖。Blend 的下降来自融合排名、不同 relevance scorer、hard negatives，或候选预算/执行限制，不是“候选越多必然越差”。

因此：
- 不引入 RRF/TRF 作为本项目最后排序。
- 不把关键词“语义相近但不相关”解释为几何 ground truth 的“误召回”。
- 不用这篇论文证明层级过滤一定需要多路检索、某一层级索引优于 NodeANN，或 dense+sparse 是本任务新机制。
- 可以借它要求实验区分：**候选遗漏、资格判定、近似距离误差、最终精确重排、预算挤占、资源开销**。

### 3.6 可证伪假设 B：受限资格下的几何覆盖表示，而非相关性融合

[INFERENCE] 对同一层级多属性查询，比较单一路 NodeANN 候选与加入少量“subtree–属性交集对应的几何入口/块”所得的补充候选；所有候选严格验证同一谓词，最终用原始 FP32 squared-L2 排序。假设在**属性命中高度不均、合格点形成多个几何簇**时，交集局部的多入口表示能在相同距离计算预算下减少候选遗漏，比固定全局入口更有效。

- 固定：总索引字节、距离预算、k、线程、向量、最终排序。
- 分桶：subtree cardinality、交集选择率、合格点几何簇数及属性–几何相关性。
- 观察：每路独有真邻居覆盖、重复候选、资格检查成本、FP32 Recall@k、P95。
- **反证条件：** 新入口只增加重复访问/元数据；收益只来自增加搜索预算；或面对独立/单簇分布无改善且没有清楚适用边界。
- 新增路径在**固定总预算**时可能挤占强路径，导致覆盖下降；这才是本任务需要验证的风险，不能直接归因于 Blend 的 RRF“污染”。
- 这个机制仍需与已有层级入口、ELI/Curator/TrieHI+相同执行器等查重；不是凭 Blend 推出研究空白。

---

## Files / 可直接复用的证据位置

### 本轮原文抽取
- `artifact://54`：VikingMem arXiv v3 HTML 全文；§2–3、Tables 1–6。
- `artifact://62`：VikingMem 正式 PDF 全文；p.1 出版信息，pp.3–7 模型/算法，pp.8–12 实验。
- `artifact://55`：Blend arXiv v2 HTML 全文。
- `artifact://63`：Blend 正式 PDF 全文；p.1 DOI，pp.6–10 设置与数字，p.9 新增模型表。
- `artifact://61`：Directory-Aware 原始 HTML；§IV-C OpenViking 集成，§VI 的 Blend [96] 引用。
- `artifact://58`、`artifact://57`：两篇 arXiv abstract 页原始 HTML，含完整 submission history。

### 仓库上下文
- `researches/containment-filtered-vector-search/walkthroughs/09-directory-aware.md:29-60`：TrieHI inclusive-ID 不变量、scope 与 ANN 层分工。
- 同文件 `:106-116`：已有误读纠正：不是所有路径移动都逐对象重写、TrieHI 不消除祖先成员、MOVE 仍有集合维护成本。
- `researches/containment-filtered-vector-search/novelty-analysis.md:15-21,73-79`：现有直接前序与公平组合要求。本轮未修改。

## Architecture

两篇在本研究中的合理位置是：

**VikingMem 提供动态状态与生命周期的应用动机 → Directory-Aware/TrieHI 提供明确层级资格语义及 scope 层前序 → 本项目研究层级 AND 多属性的表示/过滤剪枝 → NodeANN 主基线执行候选生成 → 同一原始 FP32 squared-L2 最终 top-k。**

**Blend 只提供候选生成/融合/资源代价的实验反思，不改变上述任务定义或最终评分函数。**