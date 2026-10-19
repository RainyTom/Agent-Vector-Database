# 前缀包含过滤向量搜索与相关工作

更新日期：2026-10-19（Asia/Shanghai）。

当前规范入口：[问题定义（Introduction / Definitions / Naive Solutions）](problem-definition.md)、[创新性复核与立项条件](novelty-analysis.md)。以下逐篇笔记和早期集合模型作为证据与背景；研究主张以本轮复核为准。

扩展研究另见[Context Database五方向复核](extensions.md)：DAG维护、联合索引、摘要导航、多版本与权限；不将其自动叠入当前单路径定义。

## 逐篇讨论

1. [Filtered-DiskANN：标签感知构图与前缀等价改造](walkthroughs/01-filtered-diskann.md)。
2. [TFANNS／Packing：频率敏感边预算与前缀嵌套复用](walkthroughs/02-packing.md)。
3. [UNG：包含导航、终止路径森林与前缀条件桥接](walkthroughs/03-ung.md)。
4. [ELI：祖先索引复用与树上的物化选择](walkthroughs/04-eli.md)。
5. [Curator：共享几何分区与前缀范围的衔接](walkthroughs/05-curator.md)。
6. [RWalks：属性扩散与前缀采样质量的紧凑表示](walkthroughs/06-rwalks.md)。
7. [LSSG：标签相似分层与前缀合法深度](walkthroughs/07-lssg.md)。
8. [UniFilter：谓词路由与前缀入口，证据受限讨论](walkthroughs/08-unifilter.md)。原论文全文仍待补，不与前述原文分析视为同等证据。
9. [Directory-Aware／TrieHI：直接重合的目录前缀检索与后续定位](walkthroughs/09-directory-aware.md)。已取得 v1 原始全文。
10. [PathSeer：融合索引、混合邻居处理与前缀判定成本](walkthroughs/10-pathseer.md)。已取得27页原文，与Weaviate同名实现分开。

## 直接前序核查后的研究定位

2026-10-19 核读 Directory-Aware §II–V 后确认：在固定命名空间、对象绑定单一路径的条件下，其 recursive DSQ 的合格范围与当前前缀过滤一致，内部目录直接绑定的对象也包含在内。原生 Trie、祖先成员聚合、MOVE/MERGE 已有明确前序，不能作为本项目独立新意。当前候选应聚焦前缀结构参与 ANN 候选生成/导航或成员表示的可检验增量，需对照 TrieHI + 相同 ANN；新颖性尚未充分排查。详见第九篇。

## 当前问题的语义修正

当前研究的是**前缀包含约束下的向量 top-k 搜索**（prefix-constrained vector top-k search），查询属性序列必须是返回对象属性序列的前缀。先前把本问题直接写成无序集合包含不够准确；下文保留一般 containment 文献作为可归约的背景与强基线，而不把它们直接当作原生前缀索引。顺序、位置和路径边界是当前谓词的一部分。

## Problem Definition

完整学术定义统一维护于[问题定义](problem-definition.md)：以唯一对象ID、向量和单一路径为输入，查询由向量、前缀与k组成；精确前缀合法性不因ANN近似而放宽。定义同时明确精确答案、欠填、零/不足k、并列、平均召回与总预算，避免主文和笔记出现多个版本。

前缀可行集合为 $\mathcal D_p=\{o_i:p\preceq s_i\}$。单路径可行集合层状；完整前缀单标签与静态DFS区间归约均精确成立。这些是已有执行能力的基础，不单独构成新意。

## 与一般包含过滤的关系与研究动机

以下归约是本项目基于定义的推导，不是八篇论文已提出这些前缀编码的事实。**语义有区别不代表无法归约，也不单独证明问题新颖。**

### 保留位置的集合包含归约

对于单条属性序列，定义位置标签集合

$$
E(s)=\{(j,a_j):1\le j\le |s|\}.
$$

则有精确等价

$$
p\preceq_{\mathrm{pref}}s
\quad\Longleftrightarrow\quad
E(p)\subseteq E(s).
$$

因此 ELI、UNG、Packing、LSSG 等 containment 方法可以在位置编码后作为强基线；直接丢掉位置，把序列变成 $\{a_1,\ldots,a_m\}$ 则不正确。位置编码改变标签全集和分布，比较时须计标签表示、构建和查询成本。多路径对象不能直接将各路径的位置标签取并集，否则 $(A,X)$ 与 $(Y,B)$ 的并集会错误接受查询 $(A,B)$。

### 完整前缀标识与单标签归约

令 $\operatorname{Pref}(s)$ 为序列 s 的所有前缀的规范标识集合，包含空前缀，且不同完整前缀使用不同标识，则

$$
p\preceq_{\mathrm{pref}}s
\quad\Longleftrightarrow\quad
\operatorname{id}(p)\in\operatorname{Pref}(s).
$$

这可将前缀查询转为 singleton 过滤，作为 Curator、Filtered-DiskANN 及一般 filtered ANN 的基线。它需要维护完整前缀成员关系，但不必复制原向量。对多路径对象，按完整前缀标识取并集仍保持“存在一条匹配路径”的语义，且需按对象 ID 去重。

### 原生前缀索引的候选动机

通用集合方法能够通过编码表达前缀，而共享 Trie 可以显式利用前缀集合的嵌套/互斥关系。候选研究问题是：能否在相同总内存、构建计费和最终对象 Recall@k 下，利用前缀结构减少重复表示、路由或搜索开销？该收益必须超过位置编码 containment、完整前缀 singleton，以及单路径 DFS 子树区间加 range-filtered ANN 等强归约，而不是仅超过朴素叶子索引合并。

单路径版本可把所有实际路径组织为 Trie，并将对象所在终止节点做 DFS 编号；一个前缀对应子树区间，包括恰好终止于该前缀的对象。路径长度、查询深度、分支数与各前缀命中比例是额外结构信息，不是 ANN 召回保证。多路径转成多条出现记录时须额外处理 distinct-object top-k，不可沿用单路径层状性结论。

### 直接相邻工作与待补证据

已精读直接前序 [Directory-Aware Query and Maintenance in Vector Databases](https://arxiv.org/abs/2606.16903)，Mengzhao Wang、Zheng Gong、Jingpei Hu、Jiajie Fu、Maojia Sheng、Junwen Chen、Yifan Zhu，2026-06-15 预印本 v1，正式 venue 未核实。其 recursive DSQ 的单路径合格范围与本问题一致；TrieHI 使用逐节点 inclusive-ID 集合，作为独立于 ANN 布局的 scope 层。当前差异需落实到 ANN 导航或成员表示机制，不能以“原生前缀”本身宣称创新。阅读证据、代价边界和新定位见[第九篇](walkthroughs/09-directory-aware.md)及[原始全文](../../papers/prefix-vector-search/2606.16903-directory-aware/paper.md)。

## Background Definition of Set Containment

以下为八项已有工作的通用集合模型，用于相关工作比较，不作为本项目当前前缀谓词的替代定义。

**背景定义 1（属性集合与数据对象）。** 令 $\Sigma$ 为属性标签全集，数据集为

$$
\mathcal D=\{o_i=(\mathrm{id}_i,\mathbf x_i,L_i)\}_{i=1}^{n},
\qquad \mathbf x_i\in\mathbb R^d,\quad L_i\subseteq\Sigma.
$$

其中，$\mathrm{id}_i$ 唯一标识对象，$L_i$ 为对象的属性集合。输入即使以数组或序列存储，一般集合包含只考察集合成员关系，不考察属性的先后顺序和重复次数。不同字段应编码为不同标签，如 `type=paper` 与 `venue=SIGMOD`。有序子序列、连续前缀和路径匹配属于其他谓词，不能直接用集合包含替代。

**背景定义 2（包含过滤查询）。** 查询为 $Q=(\mathbf q,L_q,k)$，其中 $\mathbf q\in\mathbb R^d$、$L_q\subseteq\Sigma$、$k>0$。可行集合为

$$
\mathcal D_{L_q}
=\{o_i\in\mathcal D:L_q\subseteq L_i\}
=\bigcap_{\ell\in L_q}\mathcal D_{\ell},
\qquad
\mathcal D_{\ell}=\{o_i:\ell\in L_i\}.
$$

空查询标签 $L_q=\varnothing$ 对应全体对象，即无过滤向量检索。查询标签包含不存在的标签时，可行集合为空。例如，$L_q=\{A,B\}$ 时，$\{A,B,C\}$ 合格，$\{A\}$ 与 $\{B\}$ 均不合格。

**背景定义 3（精确与近似结果）。** 固定距离函数 $\delta$，以 $(\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i)$ 为统一全序。令 $k_Q=\min(k,|\mathcal D_{L_q}|)$，精确结果为可行集合内排序最前的 $k_Q$ 个不同对象：

$$
R^*(Q)=\operatorname{TopK}_{o_i\in\mathcal D_{L_q}}
\bigl((\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i);k_Q\bigr).
$$

近似结果 $\widehat R(Q)$ 允许遗漏最近邻，但必须严格满足包含谓词、对象去重及 $|\widehat R(Q)|\le k_Q$。对于 $k_Q>0$，定义

$$
\operatorname{Recall@k}(Q)
=\frac{|\widehat R(Q)\cap R^*(Q)|}{k_Q}.
$$

不足 k 个可行对象时以实际真值数量为分母；空可行集单列正确性与延迟，不计入平均召回率分母。最终排序依据原向量距离；融合属性的距离可以用于搜索导航，但不能默默改变目标排序。

### 与之前层级和 overlap 定义的关系

此前的 $L_i\cap F\ne\varnothing$ 是 OR/overlap，集合包含 $L_q\subseteq L_i$ 是 AND/containment。二者不能因都使用多标签而视为同一查询；分别合并单标签 top-k 只能直接求并集内 top-k，不能直接求交集内 top-k。

若保留分类树 $T$，必须先确定对象属性是直接标签还是包含继承的有效标签。采用祖先闭包时，令

$$
A_i=\bigcup_{v\in L_i^{\mathrm{direct}}}\operatorname{Anc}^{+}(v),
\qquad
\mathcal D_{L_q}^{T}=\{o_i:L_q\subseteq A_i\}.
$$

同一根到叶路径上，查询祖先标签可被其最深后代标签蕴含；此时一条路径的多个要求可化为一个最深节点要求。若查询节点跨分支，则需要对象同时属于各分支，不再是此前的子树并集。任意层级属性是否能这样闭包，要由业务定义决定；没有继承语义时直接使用定义 2 的 $L_i$。

## 文献范围与书目

下列八项对应 LSSG v1 的 Table 2 中标记支持 containment 的项目，是该表的比较范围，不是全部包含过滤研究。其中 Vamana 和 Packing 是方法/变体名称，不能分别当成名为 Vamana/Packing 的新论文。ELI 的旧题名与新题名属同一工作，不重复计数。

| 项目 | 原始论文与完整作者顺序 | 出版与分析版本 |
|---|---|---|
| Filtered／Stitched Vamana | [Filtered-DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with Filters](https://doi.org/10.1145/3543507.3583552)；Siddharth Gollapudi; Neel Karia; Varun Sivashankar; Ravishankar Krishnaswamy; Nikit Begwani; Swapnil Raz; Yiyong Lin; Yin Zhang; Neelam Mahapatro; Premkumar Srinivasan; Amit Singh; Harsha Vardhan Simhadri | WWW 2023，3406–3416；所读作者公开 11 页稿。作者稿将 Premkumar 拼为 Premukumar，索引以正式 Crossref 姓名为准并保留差异 |
| UniFilter | [Beyond Vector Search: Querying With and Without Predicates](https://doi.org/10.1145/3769765)；Jiadong Xie; Jeffrey Xu Yu; Siyi Teng; Yingfan Liu | PACMMOD 3(6), Article 300，2025；SIGMOD 2026。书目/摘要和作者综述已核，原文全文仍待补 |
| Curator | [Curator: Efficient Vector Search with Low-Selectivity Filters](https://doi.org/10.1145/3786635)；Yicheng Jin; Yongji Wu; Wenjun Hu; Bruce M. Maggs; Jun Yang; Xiao Zhang; Danyang Zhuo | PACMMOD 4(1)，2026／SIGMOD 2026；所读 2601.01291v3 |
| Packing | [Tag-Filtered Approximate Nearest Neighbor Search](https://doi.org/10.1109/ICDE65448.2025.00272)；Jiarui Luo; Miao Qiao; Chaoji Zuo; Dong Deng | ICDE 2025，3642–3654；所读作者公开 13 页稿，Packing 为其第三种方案 |
| RWalks | [RWalks: Random Walks as Attribute Diffusers for Filtered Vector Search](https://doi.org/10.1145/3725349)；Anas Ait Aomar; Karima Echihabi; Marco Arnaboldi; Ioannis Alagiannis; Damien Hilloulin; Manal Cherkaoui | PACMMOD 3(3), Article 212，2025／SIGMOD 2025；所读作者公开仓库 26 页稿。作者稿第三作者拼作 Arnobaldi，与正式元数据 Arnaboldi 有差异 |
| UNG | [Navigating Labels and Vectors: A Unified Approach to Filtered Approximate Nearest Neighbor Search](https://doi.org/10.1145/3698822)；Yuzheng Cai; Jiayang Shi; Yizhuo Chen; Weiguo Zheng | PACMMOD 2(6), Article 246，2024／SIGMOD 2025；作者海报与官方代码已核，论文全文待补 |
| ELI | [Elastic Index Selection for Label-Hybrid AKNN Search](https://doi.org/10.14778/3785297.3785304)；Mingyu Yang; Wenxuan Xia; Wentao Li; Raymond Chi-Wing Wong; Wei Wang | PVLDB 19(4):617–630，2025／VLDB 2026；online 2026-03-18 单列；所读正式稿与 2505.03212v2。旧题名 Elastic Index Select for Label-Hybrid Search in Vector Database |
| LSSG | [Fast Label-Filtering Approximate Nearest Neighbor Search via Progressive Label Set Stratification](https://arxiv.org/abs/2609.15058)；Ziqi Wang; Jingzhe Zhang; Shuo Shen; Wei Hu | 2026-09-14 预印本 v1；注释称 SIGMOD 2027 accepted，正式出版/DOI 未核实。它是所引表格所属论文的方法，不是本项目已提出的方法 |

## 核心机制与动机对照

| 工作 | 直接针对的瓶颈 | 机制与包含查询路径 | 主要收益与代价 |
|---|---|---|---|
| Filtered／Stitched Vamana | 普通向量图过滤后难到达合格点；独立标签图重复建索引 | 标签感知剪枝；增量构图或逐标签图叠加后压缩；原文主要单标签，OR 可扩展，任意 AND 非原生已解决问题 | 在共享有界出度图中保留标签导航；稀有标签仍可能竞争不到边，多标签交集连通不能由单标签图保证 |
| UniFilter | 无谓词、类别谓词、数值谓词难由一个索引兼顾 | 作者综述描述谓词 AND/OR 图与原向量图通过 cross-edges 联合 | 统一谓词导航与原向量图；具体多标签 AND 路径、空间/理论成本需原文，不能仅凭“分区”分类推断组合爆炸 |
| Curator | 合格点稀疏时图索引断连；逐标签独立索引昂贵 | 共享向量聚类树嵌入标签索引；复杂 AND 谓词以合格 ID 构造 virtual-label 临时索引 | 共享几何分区，避免每个组合独立聚类；需要产生合格 ID 并构造/遍历临时结构，代价随合格集和分布变化 |
| Packing | 中低频标签的导航边不足，固定逐点预算浪费 | 按标签频率分配边，块内共享出度；选最稀有查询标签的覆盖图导航，精确验证其余要求 | 缓解不同节点标签数量与频率不均；单标签覆盖图不等于交集图，仍会访问未满足全部属性的对象 |
| RWalks | 不改原图时属性稀疏，搜索不知道哪里有合格对象 | 对已有图做随机游走属性扩散；混合导航距离、双结果队列和启发式剪枝；最终精确 AND 校验 | 保留基础向量图，复用邻域属性信息；新增属性表示和参数，高剪枝阈值可能使召回饱和 |
| UNG | 单标签过滤结构不能直接组织任意集合包含关系 | 相同标签集分组，Label Navigating Graph 定位极小合格超集，组内近邻图与向上 cross-group edges 联合 | 利用包含传递性，在合格组之间导航；有组数量、标签路由和跨组边成本，原文定理/成本待补核 |
| ELI | 为全部查询标签组合建索引会大量复制成员 | 从候选包含集合中选择部分索引；用较宽覆盖索引回答较严查询并校验 | 将空间—搜索折中变成物化选择；elastic factor 依赖全局基数，局部导航效率仍受相关性影响 |
| LSSG | 不同集合谓词和标签分布下单一标签图导航不稳定 | 以 Jaccard 标签相似度分 tier，双空间剪枝；查询构造 containment label-set scope 并过滤搜索 | 用多个相似度尺度复用结构；有 tier、标签候选/scope 开销，其概率与期望界均有条件 |

## 逐篇分析与正文证据

### Filtered DiskANN 从过滤搜索改到过滤构图

动机是仅修改查询阶段无法保留稀疏过滤子集的导航能力，而逐标签独立索引成本高。FilteredVamana 逐点插入、通过标签入口搜索候选并以 FilteredRobustPrune 更新边；StitchedVamana 先为每标签构建小 Vamana 图，再叠加边集并剪枝到给定出度。剪掉边 $(a,c)$ 时，替代邻居 b 除满足距离条件，还需覆盖 a 与 c 的共同标签。原文证据：[§3–4、Algorithms 3–5，PDF pp.3–5](https://harsha-simhadri.org/pubs/Filtered-DiskANN23.pdf#page=3)。

**包含能力边界非常重要。** §1.1/§2 主要定义单标签过滤；§2 明确将 AND 留作 future work，§3.1 的多标签 FilteredGreedySearch 则是“至少一个命中”的 OR。可将已知且数量有限的复合谓词物化为新标签，但这是额外表示/预处理代价。LSSG 表 2 的 containment 标记不能覆盖这一原始范围差异。

实验包含 Turing、Prep、DANN 等生产数据和若干半合成数据，§5 Table 1/Fig.1–3 按标签命中比例评估 QPS–Recall@10，并测试 SSD 检索。所读实验吞吐采用 48 线程，不能与后文单线程结果直接横比。构图与启发式标签保边不是任意 AND 的高召回证书；原文没有提供本项目需要的任意多标签交集端到端保证。

### Packing 把图边资源分配给中低频标签

TFANNS 论文直接定义 $t(q)\subseteq t(p)$。作者观察到 StitchedVamana 的全局出度限制仍可能让中低频标签失去导航边，而为这些标签全部补建独立图成本很高。论文提出 Local、Global、Packing 三种方案；Local 按节点标签频率分配出度，Global 按标签组织图，Packing 在块内共享边预算，并把高频标签聚为 mask tag，改善边预算利用率。该设计不是量化向量或简单打包内存页。证据：[§I–II、§III–V，PDF pp.1–7](https://miaoqiao.github.io/paper/ICDE25.pdf)。

查询 Algorithm 3 从 $L_q$ 选择最稀有标签 t，满足全部标签的对象必在 $\mathcal D_t$ 内。根据交集基数估计选择扫描或图搜索；导航可在覆盖集合 $\mathcal D_t$ 内进行，结果以完整 $L_q$ 检查。**这是一条先覆盖、再精确校验的 containment 路径，不是 OR 的逐标签结果合并。** 单标签 posting 的交集不能由各标签 top-k 的交集替代。

§IV Lemma 1 给 Global 方案图边空间 $O(nM_0)$；不能当作完整 Packing 系统的全部存储界，也不是任意数据召回保证。§VI 比较 Local/Global/Packing 与 Filtered／Stitched Vamana、ACORN、NHQ、post-filter HNSW、Faiss-IVF；数据覆盖 SIFT/GIST 合成标签、YFCC、YouTube-Audio/Video 的单/多标签查询。原文摘要报告在 recall≥0.9 时最高约 13 倍 QPS，仅属于其基准，不能外推为当前属性/层级数据结果。

正文中“交集基数不应小于最稀有标签基数”的文字方向与集合关系矛盾；本项目采用严格集合关系 $|\mathcal D_{L_q}|\le|\mathcal D_t|$，不照抄这处表述。实现的估计截断方向与边界行为仍需核代码。

### RWalks 保留向量图 用邻域属性引导搜索

RWalks 的动机是兼顾 filtered/unfiltered 搜索，避免把不同更新节奏的属性永久绑入基础图边。它先对已有图做固定次数和深度的随机游走，聚合邻域属性；搜索时以原向量距离与属性导航信号构造 hybrid distance，并保留被主队列挤出的合格对象到 twin queue。属性扩散还用于判断邻居是否可能通向合格区域，减少无效距离计算。证据：[作者公开稿 §3、Algorithms 1–2，PDF pp.7–12](https://raw.githubusercontent.com/AnasAito/rwalks-reproduce-v2/master/RWalks_paper_public.pdf)。

§2.1 AND 谓词是所有激活查询属性在对象中均出现，等价于集合包含；文中称其二进制向量为 one-hot，但实际应按多热标签表示理解。该文的 Equality 命名也不能不加区分地等同于 LSSG 的完整标签集合相等，应以公式为准。属性扩散与融合距离是导航工具，最终合格性由原二进制属性检查。

§4 的概率命题分析局部有效访问比例，使用单二进制属性、距离分布等模型条件；不等于 top-k 召回下界。所报平均建图 $O(n\log n)$、查询 $O(\log n)$ 沿用基础 HNSW 的启发式平均复杂度。扩散的 $O(n)$ 描述固定游走和属性处理参数；原始 Algorithm 1 的密集属性循环仍包含属性维数成本。§4–5 也讨论稀疏 map，不能将标签规模或扩散后活跃属性数当作免费常数。

§5 在 SIFT、Deep、YFCC、arXiv 上评测单标签及复合 AND 等过滤、建图时间/空间和 Recall@10–QPS。高剪枝阈值可能提前停止，§5.4 建议高 recall/困难数据关闭剪枝以避免饱和；这是一项须纳入调参的性能折中。作者明确 future work 为 range queries 与其他距离，见 §6；本项目不能把这些展望写成已实现能力。

### UNG 用包含传递性组织合法搜索范围

UNG 的关键观察是：若 $L_q\subseteq L_a$ 且 $L_a\subseteq L_b$，则 b 对查询也合格。它把相同标签集合的对象分组，构造 Label Navigating Graph 编码集合包含关系，为每组建向量近邻图，并加入向更大标签集合组的有向跨组边。查询先定位包含 $L_q$ 的极小入口组，再在相应合格组范围搜索。动机包括支持任意查询标签数、避免无效对象距离计算，以及减少结果不足。

此机制已由[作者海报](https://drive.google.com/file/d/1jGAgrHEz2n0qqQC4lRK8qLL_r0oe_gI3/view)与[官方代码](https://github.com/YZ-Cai/Unified-Navigating-Graph)相互核对：`build_label_nav_graph` 使用极小超集，`get_entry_points` 的 containment 分支路由到极小超集组；组内实现使用 Vamana。当前代码另支持 overlap/equality/no-filter，但不能据此改写 2024 原文最初评测范围。

其包含结构不同于外部分类树，也不同于 HNSW 的导航层级。作者宣称的 completeness 应区分“搜索拓扑覆盖合格组／能返回足量答案”和“精确全局 top-k”：ANN 即使返回 k 个也不等于 recall=1。本轮取得海报、正式书目和代码，未取得原文全文；定理假设、全套原论文数据/实验和复杂度未核实，不能用后文对它的评测替代其自身实验。

### ELI 把包含过滤转化为可控的索引物化选择

ELI 的动机是完整查询标签组合物化会复制大量索引成员。令 $S(F)=\{o:F\subseteq L_o\}$。若 $F\subseteq L_q$，则 $S(L_q)\subseteq S(F)$，因此较宽索引可覆盖更严格的查询。其 elastic factor 为

$$
e(Q,I_F)=\frac{|S(L_q)|}{|S(F)|},\qquad F\subseteq L_q.
$$

框架在候选标签组合索引中选择部分物化，查询选择可覆盖且 elastic factor 较大的索引。它与 UNG 均使用包含关系，但 UNG 用它连接搜索组，ELI 用它决定物化与选哪个覆盖索引。[§3–4、正式稿](https://www.vldb.org/pvldb/vol19/p617-yang.pdf)。

EIS 的 NP-complete 结论针对其定义的通用选择问题；不能自动转用于分类树子类。$O(C+k/c)$ 为带假设的期望检索成本，需要标签—向量位置/访问顺序方面的条件及后续邻居摊销枚举；全局基数比例不证明当前查询附近有同样的合格比例。预算主要以索引成员数近似且已有全局索引记零增量成本，实际比较应另列并计入总字节。

§5 在 SIFT、MSMARCO、LAION、OpenAI-1536、PAPER、TripClick、Arxiv、DEEP 上比较 UNG/ACORN 等，指标含 QPS–recall、建图和空间。旧题名和当前正式题名已作为同一条记录保存。附录的 OR 拆分不是本轮 containment 的主执行方式；当前语义应直接使用其核心包含查询。

### Curator 用共享分区组织合格集合

Curator 的聚类树来源于向量几何，而不是外部属性 taxonomy。它在共享 hierarchical k-means tree 中嵌入 per-label 索引，以避免完整独立索引的重复结构，并应对合格点比例很低时图遍历困难。复杂 AND 条件的交集可视为 virtual label，利用按树子区间排序的合格向量 ID 构造临时分层索引，沿用原搜索算法。[v3 §3–4、Algorithm 2，PDF pp.4–7](https://arxiv.org/pdf/2601.01291v3)。

其设计同时涉及结构共享与按需谓词组合，因此不能把 Curator 简化成“只支持提前已知的最多两个标签”。实际 §6.2 Fig.9 测试两标签 OR/AND，表达能力与实验覆盖需分开；Curator (Idx) 是提前建好 virtual-label 的版本，Curator 是在线临时构造版本。外部产生有序合格 ID 的代价也应计入本项目端到端时间；§4.2 还提出只对预计搜索子树延迟构造，不能只给它实现一个较弱的全量 eager 版本。

原文 low selectivity 指合格对象比例低。本文统一用 pass ratio=$|\mathcal D_{L_q}|/n$，避免与其他论文“高选择性”的反向用法混淆。实验覆盖 YFCC-10M/arXiv、图索引/IVF/pgvector 等，体现低 pass ratio 场景的稳健性；原文没有给本项目要求的无条件端到端 top-k recall 保证。

### UniFilter 通过谓词图连接属性空间与向量空间

其原始摘要提出兼顾无谓词、类别谓词与数值谓词，动机是一个索引难以同时达到普通 ANN 与专门过滤索引的效率。[作者综述 p.20](https://xiejiadong.github.io/files/paper/vector_survey.pdf)描述辅助谓词 AND/OR 图与原 proximity graph 通过 cross-edges 连接：类别构造 AND graph，数值构造 OR graph，查询定位覆盖所需对象的谓词节点后导航。

这些来源支持“统一属性／向量导航”的动机与框架认识，但原文全文仍未取得。LSSG 将其标为 label-partition、containment/overlap，是后续论文的归类，不足以证明它只构建独立分区索引，也不足以重建其完整查询步骤、复杂度与保证。数值 OR graph 的命名也不能直接当作离散标签 overlap 执行细节。该工作保留为原文待补候选，所有未知字段明确标注，未虚构算法或实验。

### LSSG 以标签相似度分层而非只按精确包含连接

LSSG 试图在 equality/containment/overlap 三种语义与不同标签分布下维持导航效果。底层允许较宽的标签差异，更高 tier 逐渐要求更高 Jaccard 相似度；同时使用向量 RNG 风格剪枝和标签差异剪枝。构建时以 IVF 或 MinHash 选择相似标签集合，避免预存全部标签集合两两距离。查询先由倒排表形成合格标签集合 scope，再按 scope 遍历多 tier 图。[v1 §3–5，PDF pp.4–8](https://arxiv.org/pdf/2609.15058v1)。

它与 UNG 的集合包含图不同：Jaccard 近不等于包含，所以最终仍须精确检查 containment。与 RWalks 也不同：它修改多层图边结构，而非只扩散属性；与 ELI 不同：它不以选择若干包含物化索引作为核心任务。

§4 的有效扩展概率依赖明确标签模型；不能把它升级为任意真实相关标签的端到端 recall。§5 给图空间 $O(mnT)$，还需计 IVF/MinHash 标签结构；查询确定性代价由 scope 构造与实际扩展数相加，期望对数式还依赖单调搜索和概率代理条件。因而不能只报遍历的 $O(\log n)$ 而隐藏标签 scope 成本。

§6 在 SIFT、GIST、TripClick、LAION、YouTube、Wikipedia、YFCC 等上比较 Packing、RWalks、UNG、ELI、ACORN 等。UniFilter 因代码不可用未被实测；Curator 也未进入其主评测。因此，“表中包含八项”不等于“八项在统一实验里全部运行”。该论文测试查询仅保留至少 k 个合格答案，未覆盖本项目所有零/不足 k 边界。后文关于 Curator 的限制描述与 Curator v3 本身的能力不完全相同，比较应采用原文能力较完整的版本。

## 方法之间的关联

| 关联 | 类型与证据 | 可得到的结论 |
|---|---|---|
| Packing → Filtered／Stitched Vamana | 原文明确问题分析与比较；TFANNS §I、§VI | 从标签感知剪枝进一步转向频率敏感边资源分配；不是完全替代全部构图框架 |
| UNG 与 ELI | 本项目方法对照；官方海报/实现与 ELI §3–4 | 同用集合包含传递性，分别用于合法组导航和索引物化覆盖，不应混为同一机制 |
| RWalks 与标签感知构图 | 原文 §3 的设计对照 | 保留基础向量图，用属性扩散与搜索偏置替代把过滤完全嵌入图边；不表示任意基础图具有相同效果 |
| Curator 与独立标签索引 | 原文 §1、§3–4、§6 | 从独立几何结构转为共享聚类结构，复杂谓词按需产生专用视图 |
| LSSG → Packing／RWalks／UNG／ELI | 原文 §6 的显式 baseline 关系 | 可核查为比较对象，但后续论文报告的加速不代表统一当前工作负载的排序 |
| UniFilter 与 UNG／LSSG | 当前仅方法类比及后续归类 | 都关联属性和向量导航；没有原文证据时，不建立具体方法继承边 |

这些工作可按“在哪个阶段为过滤付费”理解：FilteredVamana/Packing/LSSG 主要在构图与边布局阶段付费；RWalks 在构图后的属性表示及查询导航付费；UNG 在标签组与跨组连接付费；Curator 在共享分区和谓词视图准备付费；ELI 在选择性物化付费。各阶段可以组合，但是否兼容、是否有净收益需要单独验证，不能从组件名称相近推断。

## 集合包含文献的启发与候选问题

一般集合包含语义已有明确前序；以下候选 C1/C2 来源于前一轮集合建模，当前前缀问题应优先考虑上文的位置编码、完整前缀标识及原生 Trie 的差异。最直接的基线是 ELI、UNG、Packing 和 LSSG 的原生 containment，以及 Curator 的 AND virtual-label 查询；ACORN+精确 bitmap、最稀有标签覆盖后搜索和精确交集扫描构成必要背景基线。这里比较单查询对象 Recall@k，不使用 vector join 的 global pair recall。

### C1 层级蕴含下的查询规范化与共享

来源为本项目综合推导，新颖性尚未充分排查。问题是祖先闭包使标签成员重复，查询也可能包含可由其他要求蕴含的祖先条件。差异必须落在节省成员/路由/搜索结构的具体机制，而不是仅宣称支持层级属性。

可检验假设：先按层级蕴含消除冗余查询标签，并使用共享表示，在同一总字节预算及最终 Recall@k 下，相对祖先展开后的 ELI/UNG/LSSG/Curator 降低端到端时间或空间。最小实验以可核实 taxonomy 数据或明确标为合成的树，分别比较单路径、跨分支 AND 与无冗余平坦查询；所有基线都加入相同的查询规范化，原型再与其比较，避免只测预处理常识收益。指标为标签/路由字节、scope 时间、距离数、平均/P95 延迟、Recall@k。反例是单路径只需最深节点，或 bitmap 压缩已经消除大部分冗余；此时无需设计新索引。

### C2 相关属性下的覆盖索引选择与导航

来源为本项目综合推导，新颖性尚未充分排查；ELI 已有索引选择，Packing 已有基数驱动的扫描/图选择，VADER 等已有召回控制。候选问题只能是：全局集合覆盖比例相同但 query-local 命中分布不同，是否有可利用的局部信号改善选择，而非再次提出一个泛化 optimizer。

可检验假设：控制 pass ratio、k 与内存预算后，加入独立验证集学习的标签—向量相关特征或付费 pilot，能降低 ELI elastic-factor 规则和最稀有标签规则的选择损失；将预测/pilot 开销计入。最小实验固定向量与查询，构造 independent、正相关、负相关和聚簇标签，同时比较 ELI、Packing、Curator、LSSG、ACORN 及原型，报告实际最优可行基线 oracle 的 regret、逐查询 recall 分位数/违约率、重复访问和端到端延迟。风险是现有相关性统计已足够、pilot 增加净成本或收益只来自替换底层库；需做组件消融后再决定是否立项。

暂不把属性序列长度、动态更新、SSD、形式化逐查询概率保证同时叠入问题。先确定外部层级是否确有业务语义，以及真实查询是单路径还是跨分支 AND，再选择一个可验证机制。

## 统一评测条件

比较前统一距离/归一化与浮点精度、k、标签集合方向、是否允许空标签、对象 ID/并列规则、真值和不足 k 处理。分别记录标签全集规模、不同标签集合数、对象/查询标签长度分布、单标签频率、实际 AND pass ratio、标签相关性与向量相关性；边缘标签频率不能替代交集大小。

统一硬件、线程、并行查询数与总内存；同时报告向量、图边、属性、posting、路由、临时结构的字节，而非比较一方总内存与另一方增量开销。端到端计时包括标签条件解析、posting 交集/合格 ID 生成、临时索引、ANN、精确属性/距离验证和最终排序；持久构建和更新成本单列，冷/热缓存分别报告。

至少报告 Recall@k–QPS/延迟曲线、P95、逐查询 recall 分位数、结果填满率、过滤违规数、零/不足 k 查询、距离计算、visited 与有效对象数、构建时间和峰值内存。标签含义、候选顺序和近邻保证不能被融合导航距离偷偷改变。

## 背景与证据入口

系统实现补充：[Weaviate：allow-list、过滤 HNSW 与扫描回退](systems/weaviate-filtering.md)。Weaviate定制ACORN与论文ACORN分开标记；PathSeer原论文已取得，完整三组件与产品同名实现分开记录。系统文档不计入全文论文索引。

| 背景工作 | 与当前任务的关系 |
|---|---|
| [ACORN](https://doi.org/10.1145/3654923) | predicate-agnostic 图过滤强对照，不属于用户指定的 Table 2 八项 label-specific 清单，但不能从实验中省略 |
| [iRangeGraph](https://doi.org/10.1145/3698814) | 单标签层级可 DFS 归约范围，任意多标签 containment 不能直接压成一个数值区间 |
| [VADER](https://www.alphaxiv.org/abs/2609.38053) | recall-aware 搜索控制已有直接前序，不能将一般按质量早停当作本项目新意 |

已读完整来源：[Filtered-DiskANN](../../papers/filtered-ann/10.1145-3543507.3583552-filtered-diskann/paper.md)、[TFANNS/Packing](../../papers/filtered-ann/10.1109-ICDE65448.2025.00272-tag-filtered-ann/paper.md)、[RWalks](../../papers/filtered-ann/10.1145-3725349-rwalks/paper.md)、[ELI](../../papers/filtered-ann/2505.03212-eli/paper.md)、[Curator](../../papers/filtered-ann/2601.01291-curator/paper.md)、[LSSG](../../papers/filtered-ann/2609.15058-lssg/paper.md)。核查为相关章节阅读，不表示全文逐段复现或认可所有原论文保证。

UNG 已保存[作者海报](evidence/ung-author-poster-2026-10-19.pdf)、[官方 README](evidence/ung-readme-2026-10-19.md)、[代码快照](evidence/ung-uni-nav-graph-2026-10-19.cpp)与 Crossref；UniFilter 使用此前[作者综述相关页](../nested-attr-vector-search/evidence/unifilter-author-survey-2026-10-18.md)及书目快照。二者原论文全文未取得，保留证据等级和待核字段，不创建伪全文入索引。

待补查：UniFilter/UNG 原文、原文与官方当前代码的版本差异、各论文官方实现和复现实验、真实 taxonomy/查询数据与许可、subset/materialized-view selection/多值索引先前工作。此八项范围不代表领域文献全覆盖，未完成系统前后向引用闭包，也未运行算法性能实验。
