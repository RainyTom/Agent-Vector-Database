# 层级属性约束向量搜索的创新性分析

> 历史模型说明：本文件基于早期多标签分类树/overlap建模。当前用户明确的单路径前缀问题，以[规范定义](../containment-filtered-vector-search/problem-definition.md)和[创新性复核](../containment-filtered-vector-search/novelty-analysis.md)为准；TrieHI与PathSeer已补读，不沿用下文早期证据空缺判断。

分析日期：2026-10-18（Asia/Shanghai）。对象为 [Problem Definition 与 Naive Solutions](index.md)。以 alphaXiv 两轮检索与相关 PDF 页为主要依据，辅以原始 arXiv 页面、作者代码仓库、Crossref 与作者机构发表记录核实身份和版本。

## 结论与立项判断

**当前定义具有应用意义，但不足以确认为全新的检索问题。** 它可以精确归约为已有 label-filtered ANN 的 overlap 或单标签 containment 查询；单标签版本还可以归约为一维 range-filtered ANN。允许内部节点标签、多标签和对象去重，使建模更完整，但这些条件本身不能证明问题新颖。

**方法层面仍有可检验的研究空间，但新颖性尚未充分排查。** 值得继续研究的是：显式利用外部分类树和跨层级工作负载，在不物化全部祖先标签、不独立维护全部子树索引的条件下，改善索引空间与最终对象 Recall@k 约束下的查询时间。必须与祖先展开后的现代 filtered ANN、DFS 区间转换后的 range ANN，以及共享结构索引比较，不能只超过 Naive Solutions 就认定创新成立。

最直接的重叠证据来自 [LSSG](https://www.alphaxiv.org/abs/2609.15058) 的三类集合谓词、[Curator](https://www.alphaxiv.org/abs/2601.01291) 的共享聚类树与 per-label 索引，以及 [iRangeGraph](https://www.alphaxiv.org/abs/2409.02571) 的元素图共享与在线区间图组织。单独提出“标签层级”“共享索引”“按召回目标执行”都存在明显的前序工作约束。[VADER](https://www.alphaxiv.org/abs/2609.38053) 已直接研究 filtered search 的 declarative recall。

本轮没有完成全领域引用闭包，也没有实现候选算法或运行性能实验；以下“候选创新”均为研究假设，不是已验证贡献。

## 当前问题的语义边界

输入为分类树 $T$、对象向量及直接标签集合 $L_i$；查询为 $(\mathbf q,u,k)$。可行条件为

$$
P_u(o_i)=1
\quad\Longleftrightarrow\quad
L_i\cap\operatorname{Desc}^{+}(u)\ne\varnothing.
$$

结果是在可行对象中按固定向量距离排列的 top-k，按对象 ID 去重；标签过滤精确，近邻搜索可以近似。一个节点过滤包含其自身和所有后代。本文研究单查询的 filtered top-k search，属于项目 B 类“可迁移借鉴”，不属于 threshold vector join、每查询批量 kNN join 或全局 top-k pairs。

需要区分三种“层级”：外部 taxonomy 的语义层级、Curator 的向量聚类层级，以及 HNSW 的导航层级。结构名称相似不表示问题相同；但问题不同也不自动表示算法不可迁移。

## 与已有问题的精确联系

以下归约为本项目基于定义的推导；相关论文提供目标过滤语义，不声称作者已提出本文的分类树归约。

### 直接标签与 overlap 过滤

保持对象标签 $L_i$ 不变，将查询标签设为 $F_u=\operatorname{Desc}^{+}(u)$，则

$$
P_u(o_i)=1
\quad\Longleftrightarrow\quad
L_i\cap F_u\ne\varnothing.
$$

因此，任何接受该规模标签集合的 overlap-filtered ANN，均可表达当前问题。LSSG §2.1、Definition 2–3（PDF p.3）明确定义 overlap；UNG 作者仓库 Overview 也列出 overlap 接口。代价是宽子树可能产生很大的查询标签集合，标签集合构造、索引查找和合并成本必须计入。这是潜在优化点，而非语义不可表达。

### 祖先展开与单标签过滤

将对象标签展开为

$$
A_i=\bigcup_{v\in L_i}\operatorname{Anc}^{+}(v).
$$

由祖先与后代关系的定义，有

$$
P_u(o_i)=1
\quad\Longleftrightarrow\quad
u\in A_i
\quad\Longleftrightarrow\quad
\{u\}\subseteq A_i.
$$

这把任意节点子树过滤转换为已有 label-filtered ANN 的单标签查询，且不改变对象向量或正确答案。该转换适用于内部节点直接标签和多标签。不能声称 generic filtered ANN 无法处理层级继承；合理的差异是避免展开后的额外标签成员、posting、索引元数据及构建成本。

展开后的逻辑标签成员数为 $\sum_i|A_i|$。这不等于实际内存字节数，也不意味着必须复制向量；Curator 已共享底层向量与分区结构，比较时必须分别统计向量存储、成员标识和搜索结构。

### DFS 区间与 range-filtered ANN

对分类树做 DFS preorder，令 $\mathrm{tin}(v)$ 为进入节点时的编号，$\mathrm{last}(u)$ 为 $u$ 子树中最大的进入编号。则

$$
v\in\operatorname{Desc}^{+}(u)
\quad\Longleftrightarrow\quad
\mathrm{tin}(u)\le\mathrm{tin}(v)\le\mathrm{last}(u).
$$

若每个对象只有一个直接标签 $v_i$，令其数值属性为 $a_i=\mathrm{tin}(v_i)$，当前查询即可精确转换为一维闭区间过滤。这包括内部节点标签，不要求标签仅在叶子。iRangeGraph §2–3（PDF pp.3–6）已为数值区间组织共享图，因此必须作为强归约基线；“将树转成区间再接现有索引”本身不足以构成方法创新。

多标签对象对应多个 DFS 编号，不能在不加处理的情况下压缩成一个标量。若按 $(\mathrm{id}_i,v)$ 展开为标签出现记录，过滤语义可恢复，但结果需做 distinct-object top-k。仅检索 k 条记录再去重可能不足 k 个对象：例如 k=2，最近对象在子树内有三个标签，最前两条记录可能都来自它。需要去重感知的继续搜索、候选预算或其他表示方式，并把展开与重复访问成本计入。该难点并不证明现有多标签方法不能处理，祖先展开方案仍是可行对照。

### 多标签使对象集合不再整体构成 laminar family

类别节点的子树是嵌套或不相交的，但对应对象集合不一定如此。设兄弟类别 a、b，有对象分别只属于 a、只属于 b，以及同时属于 a、b，则 $\mathcal D_a$ 与 $\mathcal D_b$ 相交且互不包含。因此，不能直接以“所有过滤集合构成层状集合族”为理论前提。祖先包含 $\mathcal D_v\subseteq\mathcal D_u$ 仍然成立。

若推导算法依赖严格 laminar 性，需要明确限定单标签版本，并将多标签作为另行证明与评测的版本。

## 核心相关论文及区别

以下七篇已取得原始 PDF、完整文本提取和 alphaXiv 返回的相关正文页；核查程度为相关章节阅读，不表示全部定理与实验已复现。论文中的比较结果不直接外推为当前 taxonomy workload 的收益。

| 工作与实际分析版本 | 与当前问题的联系 | 差异及创新约束 | 正文证据 |
|---|---|---|---|
| [ACORN](https://www.alphaxiv.org/abs/2403.04871)，2403.04871v1；PACMMOD 2024 | predicate-agnostic 单图支持不同过滤集合；当前谓词可通过精确 bitmap/callback 提供 | 未以外部分类树组织共享；但任意过滤与单图共享已有，不能以“支持层级过滤”替代性能贡献 | PDF pp.1–2、4、6–8；predicate subgraph traversal、扩展与连通分析 |
| [iRangeGraph](https://www.alphaxiv.org/abs/2409.02571)，2409.02571v1；PACMMOD 2024／SIGMOD 2025 | 单标签子树可精确转为数值区间；elemental graphs 与在线构造已有 | 输入为数值属性；原生多标签 distinct-object 适配需处理，但不能忽略区间基线 | §3，PDF pp.4–6；Theorem 3.1–3.2；§5 |
| [Curator](https://www.alphaxiv.org/abs/2601.01291)，2601.01291v3；PACMMOD 2026 | 在共享 hierarchical k-means tree 中嵌入 per-label 索引；祖先展开可转为单标签查询 | 聚类树由向量分布产生，外部 taxonomy 与其不同；但“共享树降低独立标签索引复制”已被研究 | §3.1–3.2，PDF pp.4–5；§4，pp.5–7；§6，pp.8–14 |
| [LSSG](https://www.alphaxiv.org/abs/2609.15058)，2609.15058v1；2026 预印本，arXiv 注释称 SIGMOD 2027 accepted | 原生 equality/containment/overlap；两种标签归约均可表达当前查询 | tiers 由标签集合相似度构造，非给定 taxonomy；多标签检索和标签图分层本身已有 | §2.1，PDF p.3；§3，pp.5–7；§4 条件概率分析；§6 与 Table 3–4，p.9 起 |
| [ANN with Graph Range Filters／DLH](https://www.alphaxiv.org/abs/2607.00727)，2607.00727v1；2026 预印本，正式 venue 未核实 | 图关系限制与向量检索联合，说明结构化关系过滤已是前沿方向 | §2.1 明确无向图、单节点关联、r-hop 范围；本项目为有根树有向后代、多标签存在语义。无向 hop 邻域不能直接等同子树 | §2.1，PDF p.2；§3–4；§5.1，p.9 |
| [Grant](https://www.alphaxiv.org/abs/2610.11563)，2610.11563v1；2026-10-08 预印本 | 属性划分树与叶子图索引结合；代表近期 tree-plus-graph 设计 | 多数值属性范围约束，原文 m≥2；不是语义 taxonomy，不能直接称为同一问题，也不能将其理论界移植到多标签去重任务 | §2，PDF pp.2–3；§4，pp.4–7；Theorem 1，p.6；§5 |
| [VADER](https://www.alphaxiv.org/abs/2609.38053)，2609.38053v2；2026 预印本，页面自述 VLDB 2027 | 已按用户目标 recall 预测 filtered search 搜索进度并早停 | 未针对 taxonomy；但“给定召回率自适应停止”已有直接前序。GBDT 预测不等于无条件逐查询召回保证 | §3.1–3.3，PDF pp.3–5；§4，p.6 起 |

正式出版与预印本需区分：LSSG 的 SIGMOD 2027 是 arXiv 注释中的接收声明，VADER 页面写有 VLDB 2027；本轮未将其视为已核实的正式出版年份/DOI。Grant 和 DLH 未核实正式 venue。Curator、ACORN、iRangeGraph 的正式 DOI 已核实，所读全文仍为上述预印本，正式版差异待核。

### 不能直接搬用的保证

LSSG §4 分析的是特定标签分布模型下的逐步概率；祖先闭包会引入强标签依赖，不能据此推得当前场景的端到端 Recall@k。iRangeGraph Theorem 3.2 给在线构造邻接的摊销代价，不是高维 ANN 任意数据的次线性召回保证。Grant 的 Theorem 1 使用 kd-tree、固定分区参数及底层 ANNS 复杂度假设；不能仅凭摘要声称严格过滤、多标签、distinct-object top-k 同时具有该界。DLH 使用 Bloom 压缩，当前任务若移植过滤机制，需要核查并显式保证最终输出的精确谓词校验。

Curator v3 明确支持一般复杂谓词的临时索引；LSSG 对 Curator 的简化基线描述不能覆盖其原文能力。VADER 的保守非对称损失旨在减少召回高估，其训练和预测结果也不构成任意分布下的概率证书。论文间的范围判断应回到被比较方法的原始版本。

## 背景与尚未精读的候选

| 工作 | 已核内容 | 与当前问题的联系及待核事项 |
|---|---|---|
| [UNG](https://doi.org/10.1145/3698822)，PACMMOD 2(6), 2024／SIGMOD 2025 | Crossref 书目、[作者页面](https://yz-cai.github.io/)、[官方实现 Overview](https://github.com/YZ-Cai/Unified-Navigating-Graph) 的四种谓词 | 代码明确提供 containment/overlap；标签包含导航是重要比较对象。alphaXiv 标题查找无匹配、DOI/PDF 访问失败，不能把二手描述充当正文精读；LNG 保证与具体共享边需补核 |
| [RaG-Tree](https://www.alphaxiv.org/abs/2608.01255)，2026-08-02 预印本 | 原始摘要：R-tree 与 partition-aware HNSW、代价驱动搜索和更新；正式 venue 未核实 | 数值范围与图共享前沿候选；未精读，暂不填理论和实验结论 |
| [E2E](https://www.alphaxiv.org/abs/2602.06721)，2026 检索候选 | alphaXiv 返回题名 Efficient Filtered AKNN Search via Adaptive Termination | 与自适应终止相关；作者、版本、方法保证与 VADER 差别待核 |
| [Attribute Filtering in ANNS: An In-depth Experimental Study](https://www.alphaxiv.org/abs/2508.16263) | alphaXiv 检索候选，未精读 | 用于基线覆盖与评测设计；不能用综述替代算法原始证据 |
| [SciHTC](https://arxiv.org/abs/2211.02810) | 既有摘要证据：论文层级多标签分类 | C 类应用/数据背景，原问题是分类，非 ANN；需核标签编码、taxonomy 版本与许可 |
| TrieHNSW | 搜索缓存显示 label-set Trie 与 HNSW，支持集合谓词；实际 GitHub/API 返回 404，alphaXiv 题名无匹配 | 仅为不稳定检索线索，不作为已确认论文或可复现 baseline，不据此填作者或 venue |

## 候选创新与可证伪实验

以下三项均为本项目综合推导的研究假设，未声称作者明确提出，也未确认首次研究。已有共享索引、去重和自适应搜索是约束，创新成立需落在具体机制与证据上。

### H1 利用分类树减少过滤表示与搜索结构的重复

问题：祖先展开使标签成员随树深增长，逐子树索引又重复维护结构。候选差异：使用显式 taxonomy 编码路由、成员关系或共享边，在不同查询深度下复用结构，而不是仅把祖先标签输入现成索引。

可检验假设：固定总内存及最终 Recall@k 时，原型在深树、多内部节点查询的工作负载上优于 ancestor-expanded Curator/LSSG 和 DFS-range iRangeGraph，并且计入构建、路由元数据后仍有优势。

最小实验：先用单标签构造可严格控制的 laminar 场景，比较 exact scan、ACORN+精确 bitmap、祖先展开的 LSSG/Curator、DFS+iRangeGraph、直接标签桶合并、逐子树索引及原型。扫描树深、分支数、标签倾斜和向量/类别相关性，分别报告成员数量、图边/树元数据字节、build time、距离计算、QPS、P95 与 Recall@k。

风险/反例：浅树、叶子查询占主导或祖先标签压缩有效时，新增层级机制可能只增加开销。单标签上胜出也不能直接外推至多标签。

### H2 多标签情况下按不同对象分配搜索预算

问题：DFS 出现记录与多桶搜索可能重复访问同一对象。候选差异：在类别集合存在包含和交叠的条件下，联合决定访问哪些索引、需要多少局部候选、何时继续搜索，并显式以不同对象数量与最终 recall 衡量收益。

可检验假设：保持直接标签数和可行集比例可比时，针对重复候选的预算策略可减少重复距离计算与缺额率；收益需超过原生多标签 LSSG 和祖先展开 Curator，而不是仅超过固定 k 的重复记录方案。

最小实验：固定向量和树，分别构造单标签、同分支多标签、跨分支多标签。记录 unique candidates、duplicate occurrences、重复距离计算、有效结果填满率及最终对象 Recall@k；用精确枚举验证所有输出满足过滤和去重。

风险/反例：缓存即可消除大部分重复计算；现有原生 label-set 索引可能已经避免记录展开，导致该优化没有独立优势。distinct top-k 与数据库多值索引去重的相邻先前工作尚需补查。

### H3 按层级与局部难度选择执行计划

问题：查询深度不等于查询难度，相同可行集比例也可能存在不同向量相关性。候选差异只能是利用明确的层级工作负载信息改善计划选择或质量校准，不能仅以“按召回率早停”命名创新。

可检验假设：在独立测试分支/查询分布中，加入层级结构特征，相对只使用可行集比例和局部搜索统计的模型，能降低总延迟或召回违约率，且预测开销计入。

最小实验：先比较固定规则、selectivity-only 路由、VADER 风格预测及加入层级特征的消融；训练/验证/测试分离，避免用测试真值挑参数。报告平均 recall 之外的逐查询 recall 分位数和目标违约率。

风险/反例：收益可能完全由 selectivity/correlation 解释，层级特征没有额外信息；预测早停在分布变化时也可能过早停止。因此 H3 更适合作为 H1/H2 的组件，暂不建议独立作为论文主创新。

## 建议的论文定位与最低证据标准

建议把论点收敛为：**给定外部分类树和多标签对象，研究跨层级查询中的过滤表示与索引共享，在最终对象召回率约束下减少冗余存储和搜索。** 当前应使用“研究”“设计并验证”等表述，不能写“首次提出层级向量搜索”或“现有 filtered ANN 无法表达该问题”。

至少需要提供：正确性层面的归约与去重分析、一个区别于现有共享结构的具体机制、相对强归约基线的内存—召回—延迟 Pareto 改善，以及机制失效的边界。只比较预过滤、后过滤和独立叶子图不足以支撑创新性。

统一比较原始向量/归一化、度量、k、对象 ID 规则、空与不足 k 的可行集处理、真实与合成标签、线程/硬件、总内存、build/update 与在线时间。Recall@k 按最终不同对象计算，不能平均各桶 recall 或用结果填满率代替；精确阈值 join 的 global pair recall 不适用于当前任务。

## 书目信息与本地证据

| 工作 | 完整作者顺序 | 出版与版本 | 本地证据 |
|---|---|---|---|
| ACORN | Liana Patel; Peter Kraft; Carlos Guestrin; Matei Zaharia | [10.1145/3654923](https://doi.org/10.1145/3654923)，PACMMOD 2(3), 2024；所读 arXiv v1 | [PDF](../../papers/filtered-ann/2403.04871-acorn/paper.pdf)、[文本](../../papers/filtered-ann/2403.04871-acorn/paper.md) |
| iRangeGraph | Yuexuan Xu; Jianyang Gao; Yutong Gou; Cheng Long; Christian S. Jensen | [10.1145/3698814](https://doi.org/10.1145/3698814)，PACMMOD 2(6), 2024；会议 SIGMOD 2025；所读 arXiv v1 | [PDF](../../papers/filtered-ann/2409.02571-irangegraph/paper.pdf)、[文本](../../papers/filtered-ann/2409.02571-irangegraph/paper.md) |
| Curator | Yicheng Jin; Yongji Wu; Wenjun Hu; Bruce M. Maggs; Jun Yang; Xiao Zhang; Danyang Zhuo | [10.1145/3786635](https://doi.org/10.1145/3786635)，PACMMOD 4(1), 2026；所读 arXiv v3 | [PDF](../../papers/filtered-ann/2601.01291-curator/paper.pdf)、[文本](../../papers/filtered-ann/2601.01291-curator/paper.md) |
| LSSG | Ziqi Wang; Jingzhe Zhang; Shuo Shen; Wei Hu | 2026-09-14，2609.15058v1；SIGMOD 2027 accepted 声明，正式 DOI 未核实 | [PDF](../../papers/filtered-ann/2609.15058-lssg/paper.pdf)、[文本](../../papers/filtered-ann/2609.15058-lssg/paper.md) |
| DLH | Qian Tao; Yuntao Jiang; Yongxin Tong; Yu Sun | 2026-07-01，2607.00727v1；正式 venue 未核实 | [PDF](../../papers/filtered-ann/2607.00727-graph-range-filters/paper.pdf)、[文本](../../papers/filtered-ann/2607.00727-graph-range-filters/paper.md) |
| Grant | Daichi Amagata | 2026-10-08，2610.11563v1；正式 venue 未核实 | [PDF](../../papers/filtered-ann/2610.11563-grant/paper.pdf)、[文本](../../papers/filtered-ann/2610.11563-grant/paper.md) |
| VADER | Manos Chatzakis; Duo Lu; Helena Caminal; Yannis Chronis; Fatma Özcan; Yannis Papakonstantinou; Timofey Asyrkin; Sebastian Infante Murguia; Itai Rosenblatt; Themis Palpanas | v1 2026-09-29，所读 v2 2026-09-30；页面自述 VLDB 2027，正式记录未核实 | [PDF](../../papers/filtered-ann/2609.38053-vader/paper.pdf)、[文本](../../papers/filtered-ann/2609.38053-vader/paper.md) |
| UNG，待补全文 | Yuzheng Cai; Jiayang Shi; Yizhuo Chen; Weiguo Zheng | [10.1145/3698822](https://doi.org/10.1145/3698822)，PACMMOD 2(6), 2024；会议 SIGMOD 2025 | [Crossref 快照](evidence/crossref-3698822-2026-10-18.json)；代码接口已核，全文未读 |
| RaG-Tree，待精读 | Jiawei Liu; Xiang Zhang; Chao Zhang; Ju Fan; Xiaoyong Du | 2026-08-02，2608.01255v1；正式 venue 未核实 | [原始摘要](https://arxiv.org/abs/2608.01255) |

每个已读目录保存 `alphaxiv-evidence-2026-10-18.xml`，保留平台返回的版本与 PDF 页号；`paper.md` 为完整 PDF 的 PyMuPDF 文本提取，非 MinerU，公式/表格需回看 PDF。七篇均为 B 类“可迁移借鉴”：向 vector join 迁移的是过滤、成员编码、共享索引与质量控制组件，普通 ANN 查询本身不称为 join。

## 检索记录与剩余缺口

先复核既有 `papers.jsonl`、`relations.jsonl`、search-log 和当前问题定义。本轮沿用此前已执行的 vector similarity join／vector join／similarity self-join／high-dimensional similarity join／inner-product similarity join 核心检索结果，针对用户明确指定的新方向开展 alphaXiv 检索，没有重复运行整个 join 检索。

alphaXiv 共执行两次 discovery：第一轮侧重新近 taxonomy/hierarchical-label、multi-label、共享索引与范围过滤；第二轮侧重最近邻先前工作和标签包含关系。原始返回保存为 [第一轮](evidence/alphaxiv-discovery-1-2026-10-18.json)、[第二轮](evidence/alphaxiv-discovery-2-2026-10-18.json)。后续对已定位论文批量查询定义、方法、保证、实验和限制；标题无法精确匹配时，改用原始 DOI/作者来源核查，没有将相似标题强行合并。

补充网页检索包含 `"Navigating Labels and Vectors" arxiv`、`"TrieHNSW"`、`"hierarchical" "filtered" "nearest neighbor" taxonomy`、`"iRangeGraph" arxiv` 及 DOI 核实。alphaXiv 相关性排序和有限检索不能证明不存在同题研究。

关键缺口是 UNG 正文、TrieHNSW 技术报告身份、ELI/UniFilter 等标签索引、Hi-PNG/SeRF/其他范围索引、subset-search 与多值索引 distinct top-k 的引用链，以及面向 taxonomy 的更早 IR 工作。尚未系统覆盖 SIGMOD/PVLDB/ICDE/SIGIR/KDD/TOIS/TODS/TKDE，也未做完整前后向引用追踪。

本轮新增创新性分析、七篇原始全文与结构化记录及方法关系；未改写主稿的问题定义。更新主题为层级过滤的归约边界、共享索引与跨层级执行。下一步优先补核最近邻工作，再做 H1/H2 的最小实验决定是否立项。

## 2026-10-18 Overlap 细化补充

ELI 已取得正式稿与 v2 原始全文，Curator 已补核 §4.2 与 Fig.9；UniFilter 已核书目并取得作者综述说明，原文仍受限。此前“ELI/UniFilter 待核”的缺口现应按此状态理解。新增 [结构化 overlap 细化](overlap-refinement.md) 与主稿定义 7–10，讨论紧凑子树并集、祖先展开强归约、对象交叠、总/峰值内存与端到端成本；H4/H5 为本项目假设，非已验证结果。下一步为 UniFilter 原文、强基线实现及最小实验。
