# 文献检索与覆盖日志

## 2026-10-11 初始结构化索引审计

范围：核对项目已有 8 篇论文的书目与相关全文章节，补充核心术语检索和 ANN 迁移候选。本轮不是全领域系统综述，不把搜索结果排名视作完整覆盖。

### 检索顺序与查询

1. 核心查询：`"vector similarity join" high dimensional paper`、`"vector join" SimJoin DiskJoin`、`"similarity self-join" "inner product" join`。
2. 元数据/版本查询：Xling 正式 publication/ICDE、DiskJoin DOI、GPU self-join DaMoN/JPDC、IPS PODS、SJPC TKDE、LSH-SS PVLDB。
3. 相邻向量数据库查询：`"vector database" "filtered" ANN SIGMOD ACORN VBASE`、ACORN arXiv ID、VBASE Relaxed Monotonicity。
4. 新颖性粗筛：`"vector join" "cardinality" optimizer NN-Map`、`"high-dimensional similarity join" "cost" optimizer`、`"inner-product similarity join" "estimation"`。

来源：arXiv abstract/history、原始全文/作者稿、PVLDB、作者发表列表、大学发表记录、USENIX、ACM/Crossref 注册元数据。搜索引擎用于发现候选；结论以原始来源与本地全文核对。

### 已核实入口与限制

| 工作 | 原始入口 | 核实事项/限制 |
|---|---|---|
| SimJoin | [作者稿](https://xiejiadong.github.io/files/paper/%5Bsigmod25%5Dvector_join.pdf)、[DOI](https://doi.org/10.1145/3725403) | 标题、作者、PACMMOD 3(3) 与正文参考格式；ACM 页面 403，出版注册元数据可访问 |
| Fast AVJ | [arXiv](https://arxiv.org/abs/2603.16360) | v2 标题、作者顺序与版本；本地 §6 NN-Map 已核；正式 venue 未核实 |
| DiskJoin | [arXiv](https://arxiv.org/abs/2508.18494)、[DOI](https://doi.org/10.1145/3769780)、[注册元数据](https://api.crossref.org/works/10.1145/3769780) | 正式刊年 2025，会议 SIGMOD 2026；本地与正式页数不同，不自动等同实验 |
| Xling | [arXiv](https://arxiv.org/abs/2402.13397) | 本轮核实预印本，正式 venue 未找到已核实证据；§VI-A 抽样/归一化/划分已核 |
| GPU-Join | [作者列表](https://jan.ucc.nau.edu/mg2745/publications/)、[DaMoN 原文](https://jan.ucc.nau.edu/mg2745/publications/GowanlockKarsin_DaMoN2019.pdf) | 同题 DaMoN 2019 与本地 2018 长稿一条工作记录，差异待核；同作者 JPDC 2019 低维工作另列候选，不强行合并 |
| IPS | [arXiv](https://arxiv.org/abs/1510.02824)、[机构记录](https://researcher.itu.dk/en/publications/on-the-complexity-of-inner-product-similarity-join/) | PODS 2016 DOI/作者/页码；原文 witness 与 all-pair enumeration 区别 |
| LSH-SS | [PVLDB 全文](https://www.vldb.org/pvldb/vol4/p338-lee.pdf)、[arXiv](https://arxiv.org/abs/1104.3212) | Definition 1、cosine/TF-IDF、§5.2/§6；Crossref 请求遇到 429，改用正式全文，不循环重试 |
| SJPC | [arXiv](https://arxiv.org/abs/1806.03313) | journal reference=TKDE 32(4):768–781 (2020)；字段 equality/Hamming 模型 |

### 候选队列（尚未纳入 papers.jsonl）

以下仅为待补查入口，候选属性不等于全文核实后的最终分类；标题/venue 不确定处保留状态。没有给这些候选填造实验或复杂度。

| 候选/入口 | 预期分类与迁移点 | 待办 |
|---|---|---|
| [Implementing Distributed Approximate Similarity Joins using Locality Sensitive Hashing](https://openproceedings.org/2022/conf/edbt/danny.pdf) | A 候选；distributed LSH join、倾斜与重复候选 | 核全部作者/正式 ID；精读输出语义、负载与召回保证；追相关理论前序 |
| [High-Dimensional Similarity Joins](https://research.google/pubs/high-dimensional-similarity-joins-2/) | A 候选；epsilon-tree 与 cost model；页面列 TKDE 2002 | 核原文/早期版本关系与 DOI；精读成本假设、距离与现代 dense 适用性 |
| [Similarity Join over Array Data](https://faculty.ucmerced.edu/frusu/Papers/Conference/2016-sigmod-array-sim-join.pdf) | A/C 待定义核对；partitioning 与 cost model | 核作者与正式信息、原始数据模型/输出定义；不能只凭标题归 A |
| [The Space Complexity of Inner Product Filters](https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ICDT.2020.22) | A 理论组件候选；确定性压缩过滤/gap guarantee | 核作者、读定理条件；区分 filter 与 join 枚举 |
| [VBASE](https://www.usenix.org/conference/osdi23/presentation/zhang-qianxi) | B 候选；直接支持复杂 vector-relational 执行/可迭代 ANN，SimJoin 也用作 baseline | 精读 threshold/analytic query 的真实语义、完整作者、接口与停止准则；不把全部 ANN 功能称为 join |
| [ACORN](https://arxiv.org/abs/2403.04871) | B 候选；可迁移 filtered graph connectivity | 核正式 venue/DOI、精读过滤定义/构图与内存；明确由 filtered top-k 到批量 threshold join 的改造 |
| GPU 作者列表中的 HEGJoin、Hybrid KNN-Join、load imbalance 工作 | A 候选；heterogeneous execution 与 per-query kNN join | 逐项建记录，不同 title/work 不并入 GPU-Join；核正文/版本 |

### 未覆盖与下一轮顺序

- 先补早期 exact/LSH/out-of-core/distributed 的引用链，避免综述仅围绕四篇现代图/SSD/learned 工作。
- 尚未系统检索所有 SIGMOD/PVLDB/ICDE/SIGIR/KDD/WWW/TODS/TKDE/TOIS 卷期，也未做 citation forward/backward 完整闭包。
- B 类尚未正式入索引；量化、更新/并发、SSD search、多向量检索与 filtered threshold join 需补查。
- semantic/LLM、集合/字符串 join 尚未形成单独背景索引；仅在明确算法/系统/评价迁移点后收录。
- 官方实现/下载链接、formal/preprint 差异、recall 分母/zero-result queries、pair conventions 与 materialization 开销仍需逐篇补证。
- 粗筛已发现更早 cost models 与 Fast AVJ §6，不能宣称向量 join 基数/优化器无人研究；研究假设新颖性仍“尚未充分排查”。

## 2026-10-18 层级属性向量搜索定义与候选发现

用户确认目标为 `researches/nested-attr-vector-search/index.md`。该方向是 filtered top-k search，归入 B 类可迁移借鉴方向，不作为 vector join 执行工作。场景、形式化与实验草案保存在该文件。

- 先检索：`"vector similarity join" "vector join" threshold`、`"similarity self-join" "high-dimensional similarity join"`、`"inner-product similarity join"`；随后检索 `site.acm.org CCS concepts multiple concepts hierarchical classification papers` 与 `"filtered" "approximate nearest neighbor" "hierarchical" labels taxonomy`。
- 核对已有 papers.jsonl、relations.jsonl、研究问题及 SimJoin §2 / Fast AVJ §3.1 后，区分 join 与单查询 top-k。
- 候选：[Hierarchical Multi-Label Classification of Scientific Documents](https://arxiv.org/abs/2211.02810)，Mobashir Sadat、Cornelia Caragea。仅核摘要，支持论文层级多标签应用；原问题为分类，暂作 C 类应用/数据候选，不填 ANN 算法与实验结论，不加入精读索引。
- ACM CCS 官方指南全文访问返回 403；真实 taxonomy 的多父结构、内部标签和直接/继承标注需核数据版本。搜索返回 TrieHNSW 仓库入口，但打开为 404，未作可用实现或方法证据。
- 未完成：hierarchy-aware filtered ANN 与 label-set containment 全文排查、SciHTC 数据/许可核查。本轮不是相关工作覆盖综述，新颖性尚未充分排查。

### 2026-10-18 层级属性搜索 Introduction 撰写

在 `researches/nested-attr-vector-search/index.md` 的问题定义前加入应用与动机。复核 SciHTC（arXiv:2211.02810）与既有候选 ACORN（arXiv:2403.04871）的原始摘要，仅用于层级多标签场景及混合检索背景；未据此填造算法、性能或层级检索新颖性结论。本文对逐类别索引、全局检索后过滤及祖先重复索引的讨论为方案层面的推导，尚无实测收益。待补查层级共享索引先前工作及真实数据标签编码。

## 2026-10-18 alphaXiv 层级属性检索创新性排查

- 目标：明确层级属性约束向量 top-k 与 generic filtered ANN、label-set containment/overlap、range-filtered ANN、共享索引和召回控制的联系。
- 已查本地 8 条原始记录、6 条关系、现有搜索日志与 nested-attr-vector-search 定义；本轮复用此前核心 join 查询结果，没有重复进行全量 join 文献搜索。
- alphaXiv 两轮 discovery，关键词覆盖 hierarchy-constrained vector top-k search、hierarchical label filtering、filtered approximate nearest neighbor search、label-set containment、共享索引与 ACORN。随后对定位论文批量读取 PDF 的问题定义、相关方法、保证与实验页。原始检索返回保存在 `researches/nested-attr-vector-search/evidence/alphaxiv-discovery-{1,2}-2026-10-18.json`。
- 新增七篇 B 类记录及原始全文：ACORN、iRangeGraph、Curator、LSSG、DLH、Grant、VADER；保存相关页证据和 PyMuPDF 完整文本（非 MinerU），新增七条有证据关系。正式 DOI 使用 Crossref 和原始出版/作者机构记录核实；预印本、接收声明和未来会议年份分开。
- 结论：当前过滤语义可精确归约为 overlap 或祖先展开后的单标签过滤；单标签可归约为 DFS 数值区间。问题定义本身不足以确认为全新问题；具体 taxonomy 索引共享/多标签去重机制的创新仍“尚未充分排查”。
- 完整区别矩阵、七篇作者顺序/版本/章节证据、H1–H3 与最小实验见 `researches/nested-attr-vector-search/novelty-analysis.md`。
- 仍为候选：UNG（DOI 10.1145/3698822 及官方实现已核，alphaXiv 题名无匹配、正式 PDF 403，未以二手描述冒充全文）；RaG-Tree（2608.01255，仅核摘要）；E2E（2602.06721）、实验综述（2508.16263）；TrieHNSW 仅搜索缓存线索，实际 GitHub/API 404，身份与全文未确认。
- 未完成：taxonomy 直接同题工作前后向引用、ELI/UniFilter、Hi-PNG/SeRF、subset search 与数据库 distinct top-k、真实分类编码与许可，以及任何算法复现。未声称排除全部同题工作。

## 2026-10-18 UniFilter Curator ELI 原文核查与 overlap 细化

- 起点：已有 15 条论文记录、13 条关系、相关 PDF 和 novelty-analysis。先复核三篇的现有记录，新增 ELI 而不重复 Curator。先查 `vector similarity join` / `similarity self-join` / `high-dimensional similarity join` / `inner-product similarity join` / `vector join` 核心词，再针对题名、DOI、overlap、taxonomy、multi-label distinct top-k 查原始论文与引用线索。
- ELI：取得 arXiv:2505.03212v2（18 页）与 PVLDB 正式稿（14 页），分别保存。核对 §3.1 Lemma 3.2、§3.2 成本/预算、A.2/A.3 p.16 overlap 与 Fig.14。正式引用刊年 2025、online 2026-03-18 与 VLDB 2026 分开；alphaXiv 原始相关页及 Crossref 日期快照已保存。
- Curator：复核已有 v3 §4.2/Algorithm 2 pp.6–7 及 §6.2 Fig.9 pp.10–11。复杂谓词以外部有序合格 ID 为输入，支持临时构建及所述延迟子树优化；Idx 为已预建 virtual-label 版本，非同一路径简单扣时；实验 OR/AND 各两标签，不代表表达能力只限两标签。
- UniFilter：Crossref/作者机构/作者主页核标题、完整作者、PACMMOD 2025 与 SIGMOD 2026；ACM PDF/HTML 403，alphaXiv 无原文匹配，作者站点文件列表未提供全文。作者综述相关页（p.20）已保存，说明 AND/OR 谓词图与 proximity graph 联合；LSSG 的 label-partition 分类不替代原文。候选信息单独保存，未加入要求 paper.md 全文的 papers.jsonl。
- 新增 1 条 B 类论文、修订 Curator、追加 2 条关系（1 原文比较、1 研究者方法对照）。文献比较、结构化 overlap 定义、预算/端到端约束、H4/H5/强基线/最小实验见 `researches/nested-attr-vector-search/overlap-refinement.md`；主稿加入定义 7–10。
- 主要边界：一般 overlap 已有直接方法，原问题是其结构化子类。祖先展开使大子树查询变成短 overlap，必须纳入强对照。精确 distinct 局部 top-k 合并足够，不能把标签交叠本身写成结果缺额原因。
- 未完成：UniFilter 原文/官方实现、subset/structured OR/interval-union/distinct top-k 系统引用闭包、真实 taxonomy 版本与许可、任何性能复现。没有从三篇差异推导“无人研究”，新颖性仍尚未充分排查。

## 2026-10-19 包含过滤搜索的八项工作

- 用户明确当前谓词为查询标签集合包含于每个结果对象的标签集合，即 Lq⊆Li；序列按集合解释，不把 AND 与此前 overlap OR 混同。目标文档为 `researches/containment-filtered-vector-search/index.md`，前期稿增加语义变更入口。
- 先检查已有16篇/15关系、全文、novelty-analysis/overlap-refinement，再查重；先搜 `vector similarity join` / `vector join` containment 与 `similarity self-join` / `high-dimensional similarity join` / `inner-product similarity join`，随后检索 TFANNS/RWalks/Filtered-DiskANN/UNG/UniFilter 原题名、DOI和作者源。
- 新取得 Filtered-DiskANN 作者11页稿、TFANNS作者13页稿、RWalks作者仓库26页稿及补充4页，保存 PyMuPDF 全文、日期化书目与报告。TFANNS 在 alphaXiv 查询原始 URL 获取相关页；题名直接查询无匹配时不以相似论文代替。
- 一条网页线索将 arXiv:2211.12850 关联 Filtered-DiskANN，实际工具返回 OOD-DiskANN，已按标题/作者不一致排除，未登记为其版本。Filtered-DiskANN 以原作者网站全文和 DOI 10.1145/3543507.3583552 为准，不猜测预印本 ID。
- 重要纠偏：Filtered-DiskANN §2/§3.1 的原生评测主问题单标签，多标签入口为 OR；明确 AND 为 future work。Packing 是 TFANNS 的方法而非论文名，Global 空间 Lemma 不冒充 Packing 端到端界；RWalks 的属性公式与 Equality 名称按公式区分；LSSG 表2/§6 对比边界与先前论文原文分别记录。
- UNG 原论文 ACM PDF 403，作者站只提供 poster/code；新增作者海报和官方 README/核心代码快照，核containment传递与极小超集入口。UniFilter原文仍403/未匹配，沿用作者综述证据。两篇保留原文待补候选，不造 paper.md 入全文索引。
- Crossref核新三篇DOI和作者；TFANNS二次保存遇429，不循环重试，首次响应已核且有IEEE/作者稿佐证。Filtered-DiskANN 作者稿 Premukumar 与正式 Crossref Premkumar、RWalks 作者稿 Arnobaldi 与正式 Arnaboldi 的拼写差异已记，不猜改作者顺序。
- 追加3篇B类、5条原文明示边；修订LSSG复杂度、测试仅保留≥k答案和Table2能力粒度；ELI旧题名补alias。索引现19篇（A7/B11/C1）、20关系。文档含定义、8项对照、逐篇动机/机制/保证/实验边界、方法关系、C1/C2假设和统一评测。
- 未完成：UniFilter/UNG原文、原文/当前代码版本核查、真实层级/查询数据与许可、相关领域完整前后向引用、独立复现。未声称领域全覆盖，未执行算法性能实验。

### 2026-10-19 前缀包含语义修正

- 用户澄清当前是 prefix containment，上一轮将属性序列视为无序集合丢失顺序，已修正当前文档开头与问题定义；原八项集合包含文献保留为背景和经编码后的强基线。
- 先检查现有文档/索引约定，检索 `"vector similarity join" "vector join" prefix`、`"similarity self-join" "high-dimensional similarity join" "inner-product similarity join"`，再查 `"prefix" "filtered" "nearest neighbor" hierarchy` 与 `"prefix" "vector search" "trie"`。
- 当前形式化为 p 是对象序列 s 的起始连续前缀，含相等与空前缀。单序列前缀集合层状性、位置编码到containment、完整前缀token到singleton、DFS子树范围归约均为本项目从定义推导；多路径不能混合位置标签或沿用对象集合层状性。
- 新候选：Directory-Aware Query and Maintenance in Vector Databases，arXiv:2606.16903v1，2026-06-15；Mengzhao Wang、Zheng Gong、Jingpei Hu、Jiajie Fu、Maojia Sheng、Junwen Chen、Yifan Zhu。已核原始arXiv摘要/作者/版本；摘要含DSQ/DSM、PE-Online/PE-Offline/TrieHI。正式venue未核，正文与当前prefix top-k的精确差别尚未读，未伪造实验/理论入全文索引。
- 这是前缀相关工作的有限初筛，不是完整引用闭包。待办优先精读2606.16903，核单/多路径对象语义与检索算子，随后比较位置编码/前缀展开/原生Trie/range基线；新颖性尚未充分排查。本轮未新增已精读论文或关系。

### 2026-10-19 逐篇讨论第一篇 Filtered-DiskANN

按用户“一篇一篇、循序渐进”要求，本轮仅讨论 FilteredVamana/StitchedVamana，复读已有原文 §3–4 Algorithms 1–5，并打开原作者PDF核对，不扩展新一轮全领域检索。新增 `researches/containment-filtered-vector-search/walkthroughs/01-filtered-diskann.md`，区分原方案、完整前缀singleton基线、本项目LCP等价剪枝/Trie查询判定推导，以及尚未成型的边预算动机。F1给假设、基线、数据/指标、最小实验和反例；单路径/静态Trie前提明确，多路径不沿用单LCP。用长度0–3、两符号序列枚举3375组三元组核对剪枝标签条件与LCP深度条件等价，非性能实验；尚未实现ANN改造或验证加速。主索引补项目启发，未把推导写成作者结果，论文/关系数量不变。下一篇先读TFANNS/Packing的频率与边预算。

### 2026-10-19 逐篇讨论第二篇 TFANNS/Packing

用户“继续”承接上一轮一篇一篇讨论，本轮仅复读已存TFANNS原文§I/§III–V Algorithms 2/3/6/7，未重新开展全文献检索。原作者PDF网页解析因15MB尺寸限制失败，使用此前已取得的同一作者原始PDF文本核查。新增walkthroughs/02-packing.md，区分Local/Global/Packing、对数频率分配、最稀有覆盖图查询与前缀累计有效度假设F2；将新意边界、物理边/槽位区别、宽前缀几何多样性、单路径前提及最小实验写明。更新主文入口/项目启发，不改原论文保证、不新增论文或关系；未运行算法实验。下一篇衔接UNG标签包含导航。

### 2026-10-19 逐篇讨论第三篇 UNG

继续上一轮逐篇任务，仅复核已有UNG作者海报、官方README和核心代码get_min_super_sets/build_label_nav_graph/build_cross_group_edges/get_entry_points/iterate_to_fixed_point，并重新打开官方仓库；ACM原文PDF仍不可读，未将代码冒充论文全文，不补造原文定理/实验。新增walkthroughs/03-ung.md，说明极小超集入口、组内Vamana、单调跨组边，以及原实现已有标签集合Trie。

本项目推导：完整前缀编码使占用终止组包含图成为森林；内部空节点和大入口前沿仍是宽前缀问题。将Trie路由等价简化与新增LCP条件跨分支桥接分别定义为F3研究组件。用2符号长度0–2的7条序列的127种非空终止分布核森林与前沿覆盖关系，非ANN/性能实验。补充候选分析快照，保留原候选元数据；没有新论文/关系入库，19篇/20关系不变。待补UNG原文、原型基线及TrieHI/范围导航前序；下一篇ELI。

### 2026-10-19 逐篇讨论第四篇 ELI

继续逐篇任务，复读ELI已有正式稿/v2 §3–4并打开PVLDB原始正式PDF，不重复做全领域检索。新增walkthroughs/04-eli.md，解释覆盖索引方向、elastic factor、两种优化目标及其假设。前缀适配需保留完整链查询才能让原字面subset路由识别祖先；singleton正确过滤不自动具有相同可用索引集。

本项目推导限定候选前缀、单祖先服务、独立可加成本与固定factor门槛的树DP，分开代理最优与ANN实效。用7节点固定树、3种候选集、4个门槛和128种查询节点集共1536组与穷举一致；不是性能实验或新颖性证明。补项目启发/F4与主文入口，19篇/20关系不变。待补树覆盖/物化视图/结构化索引选择前序、真实成本与召回验证；下一篇Curator。

### 2026-10-19 逐篇讨论第五篇 Curator

继续用户逐篇任务，本轮复读已有Curator v3 §3.1–4.2/Algorithm2及§6.2，重新打开原始arXiv版本；未重复开展全领域检索。新增walkthroughs/05-curator.md，分开共享几何树、标签自适应buffer、临时virtual-label视图、已有延迟/缓存能力与前缀候选改进。

本项目F5限定跨前缀共享成员表示及局部几何视图；推导属性DFS/几何ID双顺序交集和单路径计数加和，枚举24种四对象顺序核对交叉分区无共同连续序，不是ANN实验。补主文入口、项目启发、研究问题及变更日志，19篇/20关系不变。待补原实现延迟构建与完整计时、Directory-Aware/TrieHI和范围索引前序；下一篇RWalks。

### 2026-10-19 逐篇讨论第六篇 RWalks

继续逐篇任务，先检查既有论文/关系/作者全文，再复读RWalks §2.1/§3/§4/§5相关部分。本轮未重复开展全领域检索；原DOI网页不可读，作者PDF网页解析被content-type拒绝，采用此前保存的原始作者PDF文本，保留正式稿差异待核。新增walkthroughs/06-rwalks.md，区分扩散、混合导航、双队列和启发式剪枝，以及原始分数/标准化分数/精确过滤。

本项目F6推导终止路径计数表示：680个三采样多重集、10,200次逐前缀核对中，显式展开与DFS二分计数一致，并核原始祖先单调性；不作为ANN或性能证据。更新主入口、项目启发、研究问题与变更日志，19篇/20关系不变。待补原代码标准化/统计成本与层级扩散前序，新颖性尚未充分排查；下一篇LSSG，UniFilter原全文仍待获取。

### 2026-10-19 逐篇讨论第七篇 LSSG

继续逐篇任务，检查既有论文/关系/作者全文，复读LSSG v1 §3–5/§6.1并打开原始arXiv页面；未重复全领域检索。新增walkthroughs/07-lssg.md，区分标签相似范围与实际分层边、原生in-filtering与候选截断、局部概率与ANN召回。

本项目F7给完整前缀Jaccard的LCP恒等式、深度分桶候选条件、固定深度的门槛等价、变长路径相似/合法错位反例及合法深度分层假设。30条非空路径、900对、6门槛共5400次核对一致，非性能实验。更新入口/项目启发/F7/变更日志，19篇/20关系不变。UniFilter原文仍待补，不能用LSSG二手分类代替其机制；下一轮优先补原始证据，新颖性未充分排查。

### 2026-10-19 逐篇讨论第八篇 UniFilter（原文未取得）

继续八项逐篇任务，先查已有候选/作者综述/论文关系，再搜完整标题PDF、UniFilter+DOI/github/cross-edges。机构原始摘要和作者主页可读；ACM PDF/ePDF在web与requests仍403，alphaXiv原PDF读取失败。检查作者主页公开仓库main递归文件树，未发现filter/predicate/sigmod26相关命名文件；不把有限路径名搜索当全文不存在证明。作者综述raw PDF下载200、873943字节，重核p20的AND/OR谓词图、coverage和cross-edges。排除同名多模态UniFilter与异作者Beyond Vector Search项目，未用Paperity开头预览重建算法。

新增walkthroughs/08-unifilter.md及日期化补查快照，保留10-18候选。原机制证据受限，F8仅为本项目前缀谓词路由/入口预算问题；单语义节点覆盖不等于单向量入口保证。补入口/F8/变更日志，未将作者综述伪造为原文、未入全文索引或加方法关系，仍19篇/20关系。无ANN实验。下一步精读直接相关2606.16903 Directory-Aware/TrieHI，再收敛创新点。

### 2026-10-19 第九篇 Directory-Aware／TrieHI 原文核查

按上一轮承诺继续精读已登记的直接相邻候选，先查本地索引/候选，再打开原始arXiv摘要/HTML，取得v1原始15页PDF（3082250bytes）、PyMuPDF全文、日期化元数据与阅读报告。核读§II–V/§VII：recursive DSQ单路径scope与当前定义一致，内部目录本地对象已有；TrieHI为scope层，仍存逐节点inclusive-ID/Roaring聚合，不改变ANN布局。

核TableII节点工作与bitmap总时间边界、TableIV非普遍更快、TableV空间最大及共同catalog排除；WIKI-Dir单规范路径、ARXIV-Dir两独立命名空间不冒称任意多路径。OpenViking入口可读但原代码/commit未核；数据仓库网页超时，下载与许可待补。新增walkthroughs/09-directory-aware.md、全文论文记录与读取快照，保留摘要候选；主文修正过时的仅摘要状态，F9收缩创新定位。现20篇(A7/B12/C1)/20关系，无新作者继承边、无ANN性能实验。待核scope接口/质量边界和更直接的层级图导航前后向文献后，选择一个机制开展最小实验。

### 2026-10-19 论文目录整理（非文献检索轮次）

按用户要求将主研究主题与项目相关性分离：7篇vector-join、11篇filtered-ann、1篇prefix-vector-search、1篇一般similarity-join。统一书目/关系/变更记录上移papers/，新增topic检索，更新引用与维护约定；原研究日志保留为历史，不据此判断全文分类。未新增论文、未改作者关系或论文保证；20篇/20关系及迁移完整性核对通过。详见papers/changelog.md与迁移清单。

### 2026-10-19 Weaviate官方过滤文档（系统资料）

用户指定官方Filtering链接，本轮先读此页并核Vector index参数参考，不重复全领域检索。补systems/weaviate-filtering.md与日期化证据：allow-list+过滤HNSW、定制ACORN与论文实现区别、sweeping/pathseer及flat回退。默认/版本为本轮文档观察，不代表本地运行；图例15%不当默认，参考配置整数40000/0强制向量索引另记。PathSeer DOI3802098正文403，未核全作者/算法/出版信息，列待读候选。补ACORN项目启发/系统文档证据，未新增全文论文或作者关系；20篇/20关系不变。待补PathSeer原文、版本固定代码/路径边界和实际系统实验，未运行Weaviate。

### 2026-10-19 PathSeer原文与作者代码学习

按用户指定论文任务，先查现有Weaviate候选与总索引，再搜精确标题/作者来源。ACM原PDF仍403；作者主页/清华组页面与SIGMOD官方核会议作者，Crossref核卷期/日期。由DBScholar链接取得原论文PDF镜像27页/2485048bytes，核标题/作者/DOI/刊头，未用其AI摘要作机制证据；未与ACM文件逐字节比对。保存PDF、PyMuPDF全文、日期化元数据/Crossref。

核读§2–5/§6，重点双区融合索引、DFN/DOS、VOB近似/离线profile、六工作负载/同Faiss余条件与空间代价。原文不依赖DOS二跳且需额外存储，与Weaviate文档同名实现分开。保存作者仓库commit6e0cd1df08927651013823f1f5458e6fb4a2336f的README/IndexPathSeer/impl核心文件，核VO函数和profile说明，未编译运行。新增filtered-ann记录/阅读报告/第十篇/F10与2条原文明示关系（比较ACORN、讨论Filtered-DiskANN），现21篇(A7/B13/C1)/22关系。旧Weaviate候选快照保留，当前笔记状态更新。待核完整profile时间/groundtruth边界/同内存/产品代码对应，未运行实验。

### 2026-10-19 TrieHI后端选择补核

用户追问为何不用先进filtered ANN；复核原文§II-A、§V-A、§VI与参考文献：实验固定Viking PG/IVF以隔离目录元数据开销，且确实引用ACORN/Filtered-DiskANN/UNG。原文未逐一解释排除各先进后端，不能臆测作者意图或将未跑解释成不能组合；结论限已测scope/后端。第九篇与阅读报告补充交叉实验建议及版本时序边界；未新增记录/实验。

### 2026-10-19 前缀问题重定义与创新性复核

先检查总书目/关系、当前前缀定义、十篇报告与历史H/C/F，再按vector join六类词及prefix/hierarchy变体检索，随后prefix-filtered/hierarchical/laminar/subtree ANN与directory/iRangeGraph。原始arXiv与作者iRangeGraph仓库重核静态范围图前序；其余命中含无关字符串prefix/AI综述，不作为直接证据。未得新的足以确立创新的原始论文，不将有限检索未命中写成无人研究。

新增problem-definition.md和novelty-analysis.md，主index改为引用统一规范，旧nested-attr创新页标历史模型，入口/研究问题同步。六定义包含精确合法、距离与ID排序、零/不足k、欠填/recall、全预算；创新性撤回新算子/Trie本身，归并为RQ-P待验证共享导航。证据快照记录覆盖与缺口；补3篇项目启发，无新论文/关系，21篇/22关系不变。未运行算法/性能实验，待补层状/层级过滤图与UniFilter/UNG原文及同后端强基线。

### 2026-10-19 原生目录必要性论证纠偏

用户提供MOVE/非递归/存储三点解释，复核Directory-Aware原文§III–V：PE-Online目录键级改写不必逐向量更新；TrieHI MOVE仍需children/parent、祖先bitmap及catalog处理；Local仅不变量记号，non-recursive仍做子集合并差；TableV显示TrieHI存储最大。补第九篇常见论证纠偏，区分原始片段无序contains与正确前缀编码，并把稳定目录ID+外部目录服务标为本项目备选推导。未进行新文献发现或算法实验，未新增论文/关系。

### 2026-10-19 Context Database扩展路线初筛

用户提出DAG/联合索引/摘要导航/MVCC/ACL，先查已有TrieHI全文/创新页，再搜vector similarity join/vector join与DAG/MVCC/permission、self-join/high-dimensional/inner-product variants，随后查hierarchical RAG及官方系统文档。核RAPTOR的ICLR2024摘要、Zanzibar的Google/ATC2019摘要、Milvus2.6 timestamp、Qdrant租户过滤、Git对象及Linux symlink手册；TigerVector作者PDF检索命中图模式vector-join候选，完整书目/输出语义未核，登记待办。新候选仅摘要/片段，不纳入全文索引。

重核TrieHI §V-E确实说明深目录PG遍历成本增大；不扩展为HNSW必然掉召回。新增extensions.md与日期化初筛，修正bitmap去重/撤销、symlink可成环、RAG非安全几何剪枝、版本/权限已有前序等论证。核心单路径静态定义保持，21篇/22关系不变，无实验。待补动态可达性增量维护、版本ANN/权限继承及RAPTOR/Zanzibar/TigerVector原文，不能声称五方向均新颖。

## 2026-10-19 六个子方向方案设计与初筛

先查papers/papers.jsonl、relations.jsonl、Directory-Aware及现有十篇笔记，复核VADER、Graph Range Filters、Grant的本地报告。先检索vector similarity join的hierarchical变体，随后检索DAG/过滤ANN；第二轮补齐vector join、similarity self-join、high-dimensional similarity join、inner-product similarity join等核心同义词，再查递归维护、版本、权限及多分辨率导航。核心扩展搜索噪声较多，不作为方法证据。完整查询及来源状态见[日期化证据](../nested-attr-vector-search/evidence/subdirection-design-screen-2026-10-19.json)。

新增待精读线索：AAAI2018 Optimised Maintenance of Datalog Materialisations（官方作者/题名/DOI/日期已核，摘要阅读）；作者PDFMaterialisation Maintenance Revisited（书目版本未核实）；arXiv2606.18520可达嵌入（摘要）；SPI arXiv2511.16681v3（官方版本题名与摘要，搜索旧摘要混杂，本文不引用旧性能）；ChronoVec官方README已声明MVCC/分支delta，未固定commit或验证实现。Zanzibar本轮读§2.2/§2.4/§3.2.4，明确token和Leopard集合快照/增量索引为前序；非全篇精读。CRoaring仅核官方README，具体lazy/fused API待实现时冻结版本。

输出[六方向总览](../nested-attr-vector-search/directions.md)和六个独立子目录。未精读候选不建完整论文记录；仅补Directory-Aware项目启发，论文21/关系22不变。新颖性尚未充分排查，尤其需每项引用闭包与逐步骤机制对照。本轮无算法性能实验。

## 2026-10-19 alphaXiv六方向新颖性淘汰审计

按用户“有人做的就不要做了”要求，在alphaXiv执行2次跨方向discovery、18个PDF/URL阅读目标。14个arXiv论文返回原文相关页；另有Zanzibar官方PDF和ChronoVec官方网页。Packing DOI返回反机器人页，经典多步近邻题名入口未命中，这2项不计入读到原文。原始检索结果与相关页保存在[证据目录](../nested-attr-vector-search/evidence/alphaxiv-screen-2026-10-19/)，逐项来源、版本、章节、缺口见[审计](../nested-attr-vector-search/alphaxiv-novelty-audit.md)和[decisions.json](../nested-attr-vector-search/evidence/alphaxiv-screen-2026-10-19/decisions.json)。

重要直接前序：Curator §4.2已有按预计访问子树过滤/构建与缓存；Counting/DRed与Enzyme §4.5已有支持撤销及增量/重算选择；iRangeGraph/JAG已有共享范围图/多尺度边；CertEx已有候选上界+安全投影下界排除，SPI v3已有自适应多分辨率；ChronoVec软件声明分支/MVCC，湖仓ANN §4已有索引与表快照共同发布；Zanzibar已有Expand、Leopard、快照缓存和新鲜度。未将相邻机制的存在写成完整组合均已实现。

决定：D0/D1/D3/D5停止独立立项；D2/D4暂停、冻结原MVP。没有方案通过新颖性门槛，不开始实现或实验。排除伪重合：2502.13245是几何半径range retrieval，2607.22922是PQ codebook，不作为同名范围/instance编译判重。alphaXiv历史授权/IVM命中有限，未形成引用闭包，完整组合均未被认证为某一论文完全等价。未知不转成新意。

新增候选仅登记在审计与日期化证据，书目/全文未完成者不伪装成完整论文记录；已有Directory-Aware、Curator、iRangeGraph补项目判断，主索引仍21篇/22关系。没有性能实验。

## 2026-10-20 LES合理性与SOTA声明核对

先检查总论文/关系索引及LES正文、归档，再检索 `"vector similarity join" "high-dimensional"` 与 `"filtered approximate nearest neighbor" ACORN PathSeer`。此次仅为任务分类与评测门槛的定向核对，未完成vector join各同义词、引用链及全部最新filtered ANN的系统覆盖，不声称找到当前最强方法。

复核已有ACORN记录的原始摘要：https://arxiv.org/abs/2403.04871。新候选 https://arxiv.org/abs/2509.07789 （Filtered Approximate Nearest Neighbor Search: A Unified Benchmark and Systematic Experimental Study [Experiment, Analysis & Benchmark]）仅阅读原始摘要，正式venue及正文实验未核实，暂不建完整论文记录；迁移价值为统一参数调优和过滤语义的评测协议，归类B、可迁移借鉴，不能称其直接支持vector join。搜索另命中 https://arxiv.org/abs/2507.21989 ，未精读，留待办。直接join检索命中Ma等PAA/SAX论文及DiskJoin，未据搜索摘要更新其算法记录。

本轮判定写入 `researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps/report.md` §15：LES表示机制合理，但当前实验和基线覆盖不能支持SOTA。无新增性能实验，不重启已收束路线。

## 2026-10-20 属性集等价与包含的双语义核查

先查总论文/关系索引、containment逐篇笔记和本地全文；用户确认Lx=Q、Q⊆Lx。先检索vector similarity join + set containment，再检索filtered nearest neighbor label-set equality、Unified Navigating Graph equality containment、原题名和ELI equality appendix。重点阅读LSSG v1 HTML §2.1/3/6.3、UNG作者README与已有源码、ACORN v1 §3.1/5、ELI v2 §2.1 Remark，以及本地PathSeer p.2和RWalks §2.1原文。

结论与原始链接见[报告](../containment-filtered-vector-search/2026-10-20-exps/report.md)。UNG原文全文仍缺，ELI所需equality附录/代码未核；UniFilter/Curator完整集合相等能力本轮未核，不凭第三方比较表收录为已证支持。PathSeer/RWalks原始PDF在线重取失败，使用已存原始全文和可读官方仓库。未声称穷尽最新文献，无新实验。

## 2026-10-25—28 Directory-Aware 研究记录索引

本方向的问题背景、前序核查与检索边界已统一归档到 [Directory-Aware 检索日志](../nested-attr-vector-search/01-directory-aware-acceleration/search-log.md)。单查询 scope top-k 为 B 类可迁移检索，不冒称几何 vector join；详细记录不在本索引重复维护。


## 2026-10-29 动态层级filtered ANN独立复核

本轮先核直接vector similarity join/vector join/inner-product similarity join检索，再核混合ANN机制；结论属于B类可迁移检索，不是join算法结果。详见[08方向检索日志](../nested-attr-vector-search/08-dynamic-hybrid-vector-search/search-log.md)及[独立审计](../nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/independent-review.md)。修订8条已有论文的项目证据，不新增未经完整归档的论文条目；无新ANN实验。


## 2026-10-29 稀疏表示与条件化过滤剪枝

直接join检索回溯到L2AP的部分倒排覆盖与prefix L2界，它是避免重复提出“残差范数剪枝”的重要A类前序；本轮未完整归档其书目和全文，作为待办保留。新增BioVSS属于B类vector-set检索表示借鉴，非join；正式ICDE2025、扩展稿分析与总库记录分开。见[机制审查](../nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/pruning-index-directions.md)。未把本轮合成证书smoke当作向量连接性能结果。


## 2026-10-29 七篇向量检索机制与记忆系统

先做直接join术语范围核对，再研读用户指定七篇。新增6工作、复核Directory-Aware；均不直接提供vector join。MSG为固定多字段向量，Blend为相关性多路融合，VikingMem为记忆系统；不因迁移到本项目就改论文原问题分类。详见[检索与版本限制](../nested-attr-vector-search/08-dynamic-hybrid-vector-search/search-log.md)和[机制综合](../nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/seven-paper-inspiration-review.md)。条件证据/导航适用域仍是未充分查重的研究假设。

## 2026-10-30 条件几何诊断的归因边界

先检索pivot/norm的直接vector similarity join与inner-product similarity join范围，再核经典metric indexing。Pivot-based Metric Indexing原文§2.3、§5.1–5.2用于避免将参考点距离区间当新过滤机制；未新增完整论文记录。研究仍为单查询层级多属性filtered ANN，不是join。真实CSV再分析与同阈值oracle分解见[上游报告](../nested-attr-vector-search/2026-10-30-exps/report.md)，不能把metric filtering等同属性过滤或将norm控制当新索引。

## 2026-10-30 层级多属性索引机制范围拓宽

直接join术语检索后转查filtered vector search/bitmap/Trie，复核已有ACORN与Curator原文，并阅读SIEVE v2相关引言/框架章节。问题仍是单查询filtered ANN，不称vector join。允许复用经典组件、以具体机制差异评估新颖性，研究范围拓宽至六族；见[协议](../nested-attr-vector-search/2026-10-30-exps/index-mechanism-portfolio.md)。SIEVE只新增章节证据候选，未做完整入库，无新ANN实验。
