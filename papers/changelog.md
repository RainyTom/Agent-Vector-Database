# 索引变更记录

## 2026-10-26（Asia/Shanghai）

- TrieHI（arXiv:2606.16903）技术解读（问题定义 DSQ/DSM、三方案结构与复杂度画像、§V 全部实验数字与口径、局限）落位于 `researches/nested-attr-vector-search/01-directory-aware-acceleration/paper-notes-2606.16903.md`（研究文档归 researches，papers 目录只留索引摘要与全文）；复现图在 `researches/nested-attr-vector-search/01-directory-aware-acceleration/` 下 `2026-10-26-selectivity-sweep/figures/` 与 `2026-10-26-triehi-reproduction/figures/`。

## 2026-10-11（Asia/Shanghai）

- 首次将已有 8 篇论文迁移到 `papers.jsonl`：A=7（执行/理论/基数估计），C=1（多字段 streaming estimator）；本轮没有新增论文全文。
- 建立 6 条关系：4 条原文明确关系、2 条研究者综合关系，标注证据与方向。
- 修正 LSH-SS 的问题分类：原文 Definition 1 直接定义实向量 VSJ size，cosine/TF-IDF 为主要实例；不是仅集合/Jaccard estimator。保留现代 dense embedding 迁移待验证的边界。
- 补入 DiskJoin 正式 DOI、PACMMOD 2025 卷期与 SIGMOD 2026 的区别；记录本地 15 页稿与注册 27 页正式稿待比较。
- 补入 IPS 的 PODS 2016、SJPC 的 TKDE 2020、GPU 的 DaMoN 2019，保留各自预印本年份。GPU 同题长短稿尚未逐项比较，JPDC 低维工作不并入。
- 补记 Fast AVJ v2 §6 NN-Map、维护、map selection/composition、join statistics；撤回“基数/优化器/动态维护完全缺失”式无范围判断。
- 记录不同 recall、输出 pair 语义与 build/online 计时，防止跨论文加速比直接横比。
- 增加项目 `AGENTS.md`、本地检索/结构验证工具、检索覆盖日志与一个明确标注待验证的研究假设。
- 待办：正式稿差异、官方代码/数据链接、LSH/分布式/早期精确连接、B 类 ANN/filtered ANN 与研究新颖性引用链扩展。

## 2026-10-18（Asia/Shanghai）

- 扩充 `researches/nested-attr-vector-search/index.md`：定义层级过滤、对象多标签、精确/近似 top-k、边界条件、叶子索引覆盖条件和实验目标。
- 明确该问题为 B 类 filtered search 可迁移方向，不称为 vector join；SciHTC 仅登记为应用/数据待精读候选。
- 更新 search-log 的检索范围、访问失败及补查项；未改动已有 8 条论文记录和 6 条关系，未新增已精读文献。

### alphaXiv 层级属性搜索创新性分析

- 新增 `researches/nested-attr-vector-search/novelty-analysis.md`，给出三种问题归约、层级/多标签边界、相关论文矩阵、可证伪假设和最小实验。
- 新增七篇 B 类可迁移工作：ACORN、iRangeGraph、Curator、LSSG、DLH、Grant、VADER；原始 PDF、PyMuPDF 全文文本、alphaXiv 页证据、日期化元数据及逐篇报告已保存。论文总数 15（A=7、B=7、C=1）。
- 新增六条作者原文关系和一条明确标注研究者综合的共享结构比较关系，总计 13 条。未将 label-set hierarchy、向量聚类层级和 HNSW 导航层级混同。
- UNG、RaG-Tree 等保留待补查状态；无法取得全文的工作不填造算法/实验。未改写主稿定义或宣称新颖性已被绝对确认。

### 同日 overlap 文献补核与问题细化

- 新增 ELI（doi:10.14778/3785297.3785304，B 类），分别保存正式稿/v2，核实 overlap 附录、期望代价假设与作者 future work。刊年 2025 与 online 2026 分开。
- 修订 Curator：virtual-label 临时构造的有序 ID 输入、Fig.9 两标签 OR/AND、预建索引与在线临时索引区分，以及端到端计费边界。
- UniFilter 仅登记全文待补候选；书目与作者综述已核，不把 LSSG 分类描述或作者综述冒充原文精读。
- 追加 LSSG→ELI 原文比较、ELI→Curator 研究者执行方式类比，共 16 篇/15 关系；主稿新增结构化 overlap 定义 7–10，并补充 H4/H5、预算/并发峰值内存及端到端代价约束。未运行实验，未证明新颖性。

## 2026-10-19（Asia/Shanghai）

- 新增 Filtered-DiskANN（WWW 2023）、Tag-Filtered Approximate Nearest Neighbor Search／Packing（ICDE 2025）、RWalks（PACMMOD 2025）三条B类记录、作者原始全文、日期化元数据与阅读报告；保留作者稿与正式元数据的姓名拼写差异。
- 追加5条原文比较/方法改进关系，现19篇/20关系；修订LSSG §5复杂度、Table2能力粒度与实验覆盖边界；ELI旧题名加入aliases，不重复计数。
- 新建包含过滤向量搜索文档：Lq⊆Li 的问题定义、八项文献关联和动机、强基线及C1/C2实验假设。原overlap稿加当前语义入口，保留历史方案。
- UNG新增作者海报与官方代码证据，UniFilter沿用作者综述；两篇原文全文仍待补，未虚构全文或保证入库。修正不能以Filtered-DiskANN原文声称任意AND原生支持；未运行性能实验。

### 同日前缀语义修正

- 当前问题由一般无序集合包含更正为属性序列的前缀包含；相关文献分析保留为背景和编码归约强基线。
- 补充单路径层状性、位置标签与完整前缀标识的归约、多路径边界和新候选2606.16903（仅核摘要，未入全文索引）；现有19篇/20关系数量不变。

### 同日逐篇讨论第一篇

- 新增Filtered-DiskANN教学与设计笔记，按原问题→构图/剪枝→前缀编码→LCP/Trie等价改造→最小实验递进。
- 结构化记录仅补项目推导，不改变作者保证；未新增论文或关系。LCP等价性小规模枚举通过，尚无ANN性能结果。

### 同日逐篇讨论第二篇

- 新增TFANNS/Packing教学与设计笔记，推导前缀边有效深度和跨层次累计预算需求；明确原Packing已有块内共享/mask，不预设其物理重复存边。
- 补项目假设F2、强基线/消融/反例及文档入口；19篇/20关系不变，未实施或验证性能改进。

### 同日逐篇讨论第三篇

- 新增UNG教学与前缀设计笔记，明确证据来自作者海报/官方代码，原文全文仍待补。
- 记录终止路径森林/入口前沿推导及F3候选路由/条件桥接，保留原候选快照并新增分析快照；未新增论文/关系，未运行ANN性能实验。

### 同日逐篇讨论第四篇

- 新增ELI教学与前缀物化选择笔记，指出完整链与singleton编码的路由资格差异。
- 记录受限树DP与F4a/F4b，1536组小树代理问题与穷举核对；不作新颖性或ANN速度声明。索引仅补项目启发，19篇/20关系不变。

### 同日逐篇讨论第五篇

- 新增Curator教学与前缀分析，明确共享几何树与语义Trie的区别，原方案已有临时/延迟/缓存视图。
- 补F5局部成员表示假设、双顺序交集、计数加和与完整端到端强基线；24顺序反例核对通过，未运行ANN性能实验。仅补项目启发，不新增论文/关系，19篇/20关系不变。

### 同日逐篇讨论第六篇

- 新增RWalks教学与前缀扩散分析；指出完整前缀单标签与冗余链AND的导航信号差异，原始分数单调不推广到标准分数。
- 补F6终止路径计数、表示等价/行为改动两阶段验证及完整空间口径；10,200次小规模等式核对通过，未运行ANN性能实验。仅补项目启发，19篇/20关系不变。

### 同日逐篇讨论第七篇

- 新增LSSG教学与前缀分析，给出完整前缀Jaccard/LCP等价与固定深度下已有门槛等价，分开等价核验、候选路由和变长合法深度分层。
- 补F7、强基线/消融/反例及入口，5400次小规模门槛核对通过；未运行ANN性能实验，仅补项目启发，19篇/20关系不变。

### 同日逐篇讨论第八篇（候选证据）

- 新增UniFilter证据受限讨论与日期化补查快照；原论文PDF仍403，作者综述重核成功，保留未知原算法字段。
- 补前缀范围单节点表达与真实向量入口质量的区别、F8预算假设/组件实验/反例。未新增全文记录或方法关系，19篇/20关系不变，未运行性能实验。

### 同日新增直接前序 Directory-Aware／TrieHI

- 新增arXiv2606.16903v1原始15页PDF、全文、报告与结构化B类记录；原摘要候选保留，追加读取状态。索引20篇(A7/B12/C1)，关系仍20条。
- 确认recursive DSQ与单路径前缀scope核心一致，修正当前文档定位；Trie/祖先物化/动态目录维护已有。记录真实bitmap/复杂度/资源边界，新增F9导航机制假设与对照，未运行ANN实验。

## 2026-10-19 论文主主题目录整理

- 修正将“可向vector join迁移”当物理分类的问题：vector-join保留7篇，filtered-ann迁入11篇，prefix-vector-search迁入Directory-Aware/TrieHI 1篇，similarity-join迁入多字段streaming estimator 1篇。保留论文目录短名、版本、全文、图片和相关性A/B/C。
- 总papers.jsonl、relations.jsonl、changelog.md移到papers/；新增topic字段，20篇记录去重不变，跨主题20条关系保持。迁移映射记录在reorganization-2026-10-19.json。
- 更新根/各主题README、AGENTS路径约定、阅读笔记/证据引用与检索工具；支持--topic并检查topic与local_dir一致、版本PDF和关系证据路径。
- 迁移13个论文目录及3个总索引文件，147个PDF/图片哈希逐项一致；20篇/20关系校验、四主题CLI与物理数量核对通过，219个本地文档链接检查无新增断链（已有4个UNG官方README快照图片链接未下载，不属于本次迁移）。

### 同日系统资料补充：Weaviate filtering

- 新增官方系统机制笔记与日期化来源，放在researches的systems目录，未归类为论文或vector join。补ACORN系统变体证据。
- PathSeer作为原文待补候选；产品ACORN与论文区分，记录allow-list、多跳/入口/扫描回退对前缀基线的约束。论文20篇/关系20条不变，未运行系统实验。

### 同日新增PathSeer原论文

- 新增filtered-ann/10.1145-3802098-pathseer原始27页论文镜像、全文、Crossref/日期化版本、详细报告及作者代码快照，非vector join。
- 新增双区索引/局部混合/VOB学习与前缀判定边界，修正将Weaviate同名实现等同论文机制的风险；旧候选证据保留。新增1篇与2条显式关系，21篇(A7/B13/C1)/22关系；未运行性能实验。

### 同日前缀属性问题的规范与创新性重审

- 新增统一问题定义与创新性复核，修正多个历史模型/候选并列造成的主张漂移；原始研究笔记保留并链接到当前规范。
- 明确TrieHI/单标签/DFS-range直接前序；主候选收敛为同字节/同召回共享导航，表示优化与profile控制单独消融。只补项目启发/证据记录，21篇/22关系不变，无性能或新颖性结果。

### 同日Context Database扩展初筛

- 新增五方向研究路线与来源/候选快照，分别列前序、语义、假设、最小实验与反例；不改核心前缀定义。
- 补TrieHI深目录PG遍历观察，保持其经验边界；RAPTOR/Zanzibar/TigerVector未精读候选不入全文索引，21篇/22关系不变，无算法实验。

### 前缀检索六个子方向方案

- 在researches/nested-attr-vector-search建立D0–D5六个子目录及directions.md；分别提出Directory-Aware执行加速、DAG增量scope、联合修补图、摘要/几何证书导航、版本快照覆盖清单、继承权限前沿方案。
- 每方案给定义、机制、前序差异、正确性边界、成本、MVP、基线/消融和停止条件；均是待验证研究者设计，尚未实现，不能写成作者future work或已成立创新。
- 更新研究入口、RQ-P候选构图规则、问题/检索日志；论文索引只补Directory-Aware项目启发。未精读新候选保留在日期化检索证据，仍为21篇/22关系。
- 重点新增前序约束：Counting/DRed、SPI v3、ChronoVec分支向量索引、Zanzibar Leopard；公式统一美元分隔符，检查本地链接及索引结构。
- 最终校验：21篇/22关系结构通过；7份新文档本地链接/分隔符通过；KaTeX 0.16.22实际生成65条公式的HTML无语法错误，未检查宿主Markdown渲染器的视觉预览。临时验证依赖已删除，未改项目依赖。

### alphaXiv六方向新颖性审计及选题收缩

- 保存2次alphaXiv检索与18个阅读目标的原始返回，区分14个arXiv原文、2个外部原始来源与2个失败/未命中；不以AI报告或标题相似判重。
- 新建researches/nested-attr-vector-search/alphaxiv-novelty-audit.md和机器可读decisions.json。D0/D1/D3/D5停止独立立项，D2/D4暂停，全部原MVP不实施；没有将未找到等价论文写成创新性确认。
- 六个README增加当前决定，directions及所有主要入口撤销原实施优先级；保留历史方案与证据。
- 补Directory-Aware、Curator、iRangeGraph的项目判断，不改作者方法/理论；其他新候选书目与全文未完核，未新增完整论文条目，仍21篇/22关系。
- 无算法实现或性能实验；完整组合是否等价与核心是否已有前序分开记录，历史授权/IVM及图增广引用闭包仍待补。

## 2026-10-23（Asia/Shanghai）轨迹检索问题边界修正

- 新增 `trajectory-search` 主主题及 Quantum Range Query、TISIS、REPOSE、N-tree、ST2Vec、GTRSS、TKDE trajectory similarity survey 共7条记录；保存 arXiv 原始 PDF、日期化元数据与逐篇证据。
- 核实 GTRSS/TRSSQ 已定义“每条轨迹的最佳连续子轨迹，再做全局 top-k”，修正将 episode 定位、轨迹级去重和区间输出视为候选新意的旧判断。
- 将 Quantum Range Query 从单对象过滤边界中纠正为“同一轨迹混合数据域、不同 range-reporting 算子”；其形式模型含 semantic，但 Table 1 未实测 semantic，结论仍将其列为 future work。
- 核实 TISIS* 使用 Word2Vec POI embedding + LCSS threshold；REPOSE/N-tree/ST2Vec 分别提供分布式完整轨迹 top-k、exact metric kNN/range、时空软相似度前序。
- 新增 survey→REPOSE、survey→ST2Vec、GTRSS→REPOSE 三条有正文证据的关系。总索引现为 **28篇/25关系**；本轮未运行性能实验，不作新颖性确认。
- 补核 GTRSS 的直接前序 TRSSQ（ICDE 2024, DOI:10.1109/ICDE60146.2024.00335）：Crossref/OpenAlex 书目一致，OpenAlex 摘要被截断，方法细节仅来自 GTRSS §I–II 转述（RSSE = GRU 点级匹配 + 轨迹编码对齐 + filter-validate）；IEEE/CSDL 付费墙与 202 挑战页导致全文未获取。按全文缺失处理，**不计入论文索引**，证据存 `researches/semantic-spatiotemporal-hybrid-search/evidence/trssq-2026-10-23.md`；28篇/25关系不变。
- 用户提供 TRSSQ 13 页 IEEE 正式版 PDF 后**转正**：新增 `trajectory-search/10.1109-icde60146.2024.00335-trssq/`（PDF + 日期化元数据 + 全文证据），全文核对度量 DTW/EDR/ERP/LCSS、RSSE 结构与 top-M 候选+ExactS 验证、无端到端精确保证、无时间/空间/语义约束。新增 GTRSS→TRSSQ、TRSSQ→REPOSE 两条原文关系。索引现为 **29篇/27关系**；上述“不计入”条目仅保留历史，不再适用。

## 2026-10-20 完整属性集 equality / containment 能力核查

- 用户明确equality为Lx=Q、containment为Q⊆Lx；新增containment研究目录当日报告与六方法证据。
- 修订LSSG、ACORN、PathSeer、ELI、RWalks五条既有索引的支持层次；LSSG补已核官方代码入口。UNG保留作者产物证据候选，不补造未取得的全文记录；论文总数/关系数不变。
- 明确UNG general/equality分构建模式；RWalks名为Equality的公式只要求包含；ACORN/PathSeer的双集合能力是通用predicate实例化，ELI具体equality扩展实现待核。
- 旧Exact/Subtree与完整集合谓词不同，帕累托计划追加语义校正及核心基线优先级。无新性能实验。
## 2026-10-25 Directory-Aware 子目录的新问题定义

- 在 `researches/nested-attr-vector-search/01-directory-aware-acceleration/` 新增跨集合目录兼容阈值向量连接的 Problem Formulation 与研究记录；原 D0 停止独立立项结论保留。
- 更新 nested-attr 入口、vector join 问题入口及检索日志；本轮未新增论文或关系记录，未运行算法实验，候选新颖性尚未充分排查。
- 同日按问题范围修订：将单父文件目录树背景改为共享虚拟目录 DAG，补四种精确基线与逻辑对象去重语义；旧树定义不再适用，原 D1 动态维护方案仍为独立历史方向。
- 同日再次修订：文件夹快捷方式仍未摆脱文件目录背景，当前 Problem Formulation 改为多维商品目录的跨平台商品连接；基线更新为五种，前两版文件目录定义只作历史记录。
- 同日层级语义修正：普通 facet 的约束细化不足以构成品类层级；当前定义明确为“品类树 × 属性 facet”，并将五种基线统一到品类祖先可比、属性兼容和 L2 阈值三重条件。

## 2026-10-29 动态层级检索交接的独立机制复核

- 完整读交接与P0/P1/P2清单，新增[独立判断](../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/independent-review.md)、[原文机制审计](../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/literature-mechanisms-review.md)及可复现的162次summary离线审计；没有新ANN性能实验。
- 修订ACORN、iRangeGraph、Curator、VADER、ELI、Filtered-DiskANN、TrieHI、PathSeer共8条既有记录的项目启发与日期化证据；保留旧快照及正式稿/预印本差异待办。总论文29条，关系27条不变。
- 撤回“层级MOVE无人研究/首次可回收摘要/动态预算基本空白”等泛化结论；区分新颖机制、正确实现、强对照与维护策略实测。Filtered-DiskANN原始剪枝为共同标签交集，不沿用EMA相关工作中的并集误述。
- Quake正式OSDI稿与EMA预印本相关章节进入方向机制证据和待入库日志，未伪造完整归档/全文通读状态；本轮未新增引用或继承关系，既有关系索引保持不变。


## 2026-10-29 表示/过滤剪枝机制与BioVSS

- 新增BioVSS/BioVSS++一条正式ICDE2025记录，aliases保留v1/v2改题及算法名；分析v2扩展稿，不将占位期刊当发表。建立multi-vector-search主题，完整按页文本、提取SHA与原文机制审读已保存。总库30篇，关系27条。
- 纠正“FlyHash multi-vector”泛称：主要使用BioHash，核心目标是Hausdorff而非MaxSim；稀疏计数倒排/OR sketch是候选机制，不是硬属性逻辑。条件理论和作者表格矛盾保留。
- [新机制审查](../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/pruning-index-directions.md)核对L2AP、Seismic、MUVERA、Curator、FINGER、FCVI和iFVS。未为未完整归档工作伪造总库条目或作者继承关系。
- 用户停止默认维护主线；服务器改为先设计表示/条件几何证书，再做小型判别。已运行合成代数smoke，未运行真实ANN性能实验。


## 2026-10-29 七篇机制学习与层级多属性边界

- 新增MSG、CDMG、Hubness、Flash、VikingMem、Blend六条，复核已有Directory-Aware；总库36工作、29关系。新增Hubness→Flash、Directory-Aware→Blend两条原文明确引用，不按同作者建立继承边。
- VikingMem/Blend改用2026正式PVLDB身份；CDMG只取得2025历史五作者16页报告，正式六作者29页书目另列，正文差异待补；Flash实际分析arXiv v1。保存6篇分页原文提取、文本SHA与完整机制审读，不伪造PDF字节校验。
- 研究继续限定外部层级与多属性硬资格＋原始向量top-k；提出条件几何证书/条件遮挡证据两类并列待查候选，不默认重启D2/LES。5项合成语义反例运行通过，不是新ANN性能结果。
- 修正README中当前checkout不存在的research_index.py命令；本轮以独立结构/路径核验替代，不新造工具或声称原验证器成功。

## 2026-10-30 上游诊断复核的章节证据与版本边界

- 新增Pivot-based Metric Indexing（PVLDB2017）§2.3、§5.1–5.2的[原文节选及来源](../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/evidence/pivot-filtering-excerpts-20261030.md)，仅作为经典pivot/区间控制的证据候选；未完整核实验，不新增伪精读记录或引用关系。
- 本分支仍36工作、29关系；服务器7756064的36工作、31关系来自不同分支，未自动合并，不将数量相加或覆盖旧记录。
- [上游报告](../researches/nested-attr-vector-search/2026-10-30-exps/report.md)区分几何oracle能力与过滤特有增量，保存64条原CSV控制与可复现分析。条件遮挡路线未实施，不继承稀疏残差B的暂停为其实验失败。

## 2026-10-30 复用组件与跨机制族研究

- 复核ACORN §5.2的过滤遮挡反例、Curator §4.2的倒排/bitmap与虚拟谓词视图；不得将已有动机认作新发现，也不得仅因复用图/Trie/bitmap就否定候选。
- 新增[SIEVE v2 §1原文节选](../researches/nested-attr-vector-search/2026-10-30-exps/evidence/sieve-component-reuse-20261030.md)，阅读§1–3及§4.1开头；未完整核实验和正式版本，作为候选证据不新增完整论文记录。总库与关系计数不变。
- [机制研究方案](../researches/nested-attr-vector-search/2026-10-30-exps/index-mechanism-portfolio.md)明确允许已有原语上的实质索引/算法贡献，撤销证书诊断的全局门禁；六族及新工作负载均为待执行计划，不覆盖历史实测。

## 2026-10-30 跨方向索引与共享视图

- 新增Memento、Grafite、FlexFlood、Qd-tree、F-IVM、DBSP六条C类可迁移工作，按自身问题归入data-indexing(4)/incremental-views(2)，不是vector join或ANN论文。总库42工作、30关系；Memento→Grafite为原文明示比较和benchmark复用，未按研究者相似推断继承。
- 每篇保存原文reader提取、日期化文本SHA、分析章节/版本、完整作者与未核边界。Memento采用v3不混v1定理；FlexFlood为workshop；F-IVM区分2018前身及2023在线/2024卷期；DBSP正式五作者不能被arXiv四作者覆盖。
- 新增[具体迁移研究](../researches/nested-attr-vector-search/2026-10-30-exps/cross-domain-index-transfer.md)及7组已执行有限语义构造，未运行作者实现或新增真实ANN实验。组件替换不是已确认新颖性，未实现不称实验失败；服务器7756064分支未合并。
