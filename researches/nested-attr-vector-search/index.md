> **2026-10-25 范围更新：** 共享虚拟目录 DAG 中的跨集合向量连接与精确基线见 [Problem Formulation](01-directory-aware-acceleration/problem-formulation.md)。此前层级 Exact/Subtree 与 LES 实验已完成一轮，结论与限制见[LES 当日报告](07-laminar-edge-sharing/2026-10-20-exps/report.md)；原 D0–D5 决定不变。语义时空混合搜索的独立进展见[对应入口](../semantic-spatiotemporal-hybrid-search/index.md)。

> 当前语义（2026-10-19）：查询属性序列必须是返回对象属性序列的前缀；见[前缀包含过滤文档](../containment-filtered-vector-search/index.md)。下文保留此前层级节点/overlap方案，不能将一般无序集合包含或子树并集直接当作当前前缀定义。

## 基准与实现结果

当前测量入口见[基准协议](07-laminar-edge-sharing/benchmark-protocol.md)；研究草案、问题定义与提出的共享索引见[Laminar Edge Sharing](07-laminar-edge-sharing/README.md)；开源过滤 ANN 基线的改造评估见[基线改造](07-laminar-edge-sharing/baseline-adaptation.md)。该结果不改变下方六个方向的停止/暂停决定。

## 子方向方案入口

最新查重决定见[alphaXiv新颖性审计](alphaxiv-novelty-audit.md)：D0/D1/D3/D5停止独立立项，D2/D4暂停，当前不实施原六个方案。以下链接保留设计与证据，不代表六项创新已获确认。

当前六个方案见[子方向总览](directions.md)：[Directory-Aware加速](01-directory-aware-acceleration/README.md)、[DAG维护](02-dag-incremental-scope/README.md)、[联合索引](03-prefix-coupled-index/README.md)、[渐进导航](04-certified-progressive-routing/README.md)、[版本检索](05-versioned-directory-search/README.md)、[继承权限](06-inherited-acl-search/README.md)。每项已有具体机制、前序差异、最小实验与停止条件；均为待验证提案。以下Introduction和定义保留历史模型，当前单路径定义仍以前述规范入口为准。

# Introduction

向量检索通过表示空间中的距离刻画对象与查询之间的相关性，为文本等非结构化数据提供了检索方式。在实际应用中，用户通常还需要通过结构化属性限定检索范围。因此，查询处理需要同时满足属性条件与向量排序要求：先确定哪些对象有资格进入结果，再从这些对象中返回与查询向量最近的 k 个对象。已有混合检索研究，如 [ACORN](https://arxiv.org/abs/2403.04871)，已将向量相似度与结构化谓词的联合处理作为研究对象。

当属性具有层级关系时，检索范围可以随用户需求在不同粒度之间变化。以学术论文检索为例，论文可以按照主题组织为由粗到细的分类结构，并同时归属于多个主题。[SciHTC](https://arxiv.org/abs/2211.02810) 基于 ACM CCS 构建层级多标签论文数据，为这一应用场景提供了实例。考虑用户希望检索与“高效的语义检索索引”相关的论文：用户既可以在较宽的学科范围内搜索，也可以进一步限定到某个细分类别。查询指定一个分类节点后，该节点及其后代类别共同确定允许返回的论文集合，语义向量距离则决定集合内部的结果顺序。

这种查询具有明确的层级约束语义。标注为细分类别的对象同时满足其祖先类别的过滤条件，而仅标注为较粗类别的对象不能被推断为属于某个特定后代类别。此外，同一对象可以通过多个标签满足一次查询，但只能作为一个结果返回。因此，层级属性约束下的向量 top-k 搜索，需要在分类继承与对象去重的基础上，从指定子树对应的对象集合中寻找最近邻。

支持任意分类节点查询，会使候选集合的规模和组织方式随查询而变化。沿同一分支向下细化查询节点，可行集合逐步缩小；面向较粗类别的查询则可能覆盖多个分支。这使得索引设计需要同时考虑不同粒度的查询。一个直接方案是在各细分类别上分别建立向量索引，查询时搜索目标子树内的相关索引，再合并局部结果。该方案能够利用类别划分缩小单次搜索范围，但较粗粒度的查询可能需要访问较多索引，并承担重复初始化、候选合并与去重的开销。若对象具有内部节点直接标签，仅搜索叶子索引还需要额外的数据覆盖机制。

另一种方案是在全局向量索引中检索候选，再执行层级属性过滤。然而，固定数量的全局近邻中可能只有少量对象满足目标类别，因而不能保证得到过滤集合内部的 top-k；增加候选数量又会引入额外搜索成本。反过来，为每个分类节点分别建立覆盖整棵子树的独立索引，可以直接服务对应节点查询，却可能让同一对象被存储和索引于多个祖先节点。由此形成一个需要研究的折中：如何在支持不同粒度查询的同时，控制重复索引成本与查询执行开销。

层级结构也为缓解这一折中提供了依据。祖先节点与后代节点的可行集合具有包含关系，不同查询范围之间因而存在可利用的结构联系。但这种包含关系并不保证局部近邻结果可以直接替代其他范围的答案；多标签还可能使不同分支的对象集合重叠。将层级关系用于索引组织或搜索复用时，需要保持过滤语义和最终结果排序的一致性，并以去重后的查询结果衡量召回率。

基于上述动机，本文研究层级属性约束下的向量 top-k 搜索：给定带有层级标签的向量数据集，支持用户在任意分类节点所确定的对象范围内执行最近邻检索。本文关注的核心问题是，如何利用类别之间的包含关系组织索引与执行搜索，在严格满足层级过滤条件的前提下，降低给定内存预算和召回目标下的查询时间。下文首先形式化描述属性层级、可行对象集合以及精确与近似查询语义，为索引设计和查询处理提供统一的问题基础。

# Problem Definition

本文研究层级属性约束下的向量 top-k 搜索（hierarchy-constrained vector top-k search）。首先定义属性层级及其对应的数据模型，随后定义层级过滤条件，并在此基础上给出精确查询与近似查询的形式化描述。

**定义 1（属性层级，Attribute Hierarchy）。** 属性层级表示为一棵有限有根树 $T=(V,E,r)$，其中 $V$ 为属性节点集合，$E$ 为从父节点指向子节点的有向边集合，$r$ 为根节点。每个节点代表一个类别，子节点代表其父节点类别的进一步细分。对于任意 $u\in V$，定义包含节点自身的后代集合为

$$
\operatorname{Desc}^{+}(u)
=
\{v\in V\mid v=u\ \lor\ u\leadsto v\},
$$

其中，$u\leadsto v$ 表示存在从 $u$ 到 $v$ 的有向路径。若 $v\in\operatorname{Desc}^{+}(u)$，则类别 $v$ 的成员同时属于类别 $u$。

**定义 2（带层级属性的向量数据集，Vector Dataset with Hierarchical Attributes）。** 给定属性层级 $T$，数据集定义为

$$
\mathcal D
=
\{o_i=(\mathrm{id}_i,\mathbf x_i,L_i)\}_{i=1}^{n},
$$

其中，$\mathrm{id}_i$ 为对象的唯一标识，$\mathbf x_i\in\mathbb R^d$ 为对象的 $d$ 维向量，$\varnothing\ne L_i\subseteq V$ 为直接赋予对象的属性标签集合。标签可以位于层级的任意节点，每个对象可以具有多个标签。类别成员关系沿祖先方向继承；对象具有某个内部节点标签，并不意味着该对象属于该节点的任意特定后代类别。不同标识对应不同对象，即使其向量或标签集合相同。

**定义 3（层级过滤与可行对象集合，Hierarchical Filter and Feasible Set）。** 给定查询节点 $u\in V$，对象 $o_i$ 满足层级过滤条件，当且仅当其至少一个直接标签属于以 $u$ 为根的子树。相应的过滤谓词定义为

$$
P_u(o_i)
=
\begin{cases}
1, & L_i\cap\operatorname{Desc}^{+}(u)\ne\varnothing,\\
0, & \text{otherwise}.
\end{cases}
$$

查询节点 $u$ 对应的可行对象集合为

$$
\mathcal D_u
=
\{o_i\in\mathcal D\mid P_u(o_i)=1\}.
$$

由上述定义可得，$\mathcal D_r=\mathcal D$；若 $v\in\operatorname{Desc}^{+}(u)$，则 $\mathcal D_v\subseteq\mathcal D_u$。由于对象可以具有多个标签，不同分支对应的可行对象集合可能重叠；同一对象在集合中仅出现一次。

**定义 4（层级属性约束下的向量 top-k 查询，Hierarchy-Constrained Vector Top-k Query）。** 给定数据集 $\mathcal D$、属性层级 $T$ 以及固定的向量距离函数 $\delta:\mathbb R^d\times\mathbb R^d\rightarrow\mathbb R_{\ge0}$，查询定义为

$$
Q=(\mathbf q,u,k),
$$

其中，$\mathbf q\in\mathbb R^d$ 为与数据向量处于同一表示空间的查询向量，$u\in V$ 为查询节点，$k\in\mathbb N_{>0}$ 为期望返回的对象数量。查询要求按照 $\delta(\mathbf q,\mathbf x_i)$ 从小到大对可行集合 $\mathcal D_u$ 中的对象排序。为消除距离并列带来的歧义，固定对象标识上的全序，并以标识作为第二排序键。记对象排序键为

$$
s_Q(o_i)
=
\bigl(\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i\bigr),
$$

排序键之间采用字典序比较。属性仅用于确定对象是否可行，不参与距离计算或排序打分。

**定义 5（精确查询结果，Exact Query Result）。** 对于查询 $Q=(\mathbf q,u,k)$，令 $k_Q=\min\{k,|\mathcal D_u|\}$。精确查询结果 $R^*(Q)$ 为 $\mathcal D_u$ 中按照 $s_Q$ 排序的前 $k_Q$ 个不同对象，即

$$
R^*(Q)
=
\operatorname{TopK}_{o_i\in\mathcal D_u}
\bigl(s_Q(o_i);k_Q\bigr).
$$

其中，$\operatorname{TopK}$ 返回按排序键升序排列的有序对象序列。其对象集合满足

$$
\begin{aligned}
& R^*(Q)\subseteq\mathcal D_u,\\
& |R^*(Q)|=k_Q,\\
& s_Q(o_i)<_{\mathrm{lex}}s_Q(o_j),
\quad
\forall o_i\in R^*(Q),\ 
\forall o_j\in\mathcal D_u\setminus R^*(Q).
\end{aligned}
$$

在集合运算中，$R^*(Q)$ 表示该序列包含的对象集合。当 $\mathcal D_u=\varnothing$ 时，结果为空；当 $|\mathcal D_u|<k$ 时，返回全部可行对象。对象具有多个满足过滤条件的标签时，仍只返回一次。

**定义 6（近似查询结果与召回率，Approximate Query Result and Recall）。** 对于查询 $Q$，近似查询结果 $\widehat R(Q)$ 为按 $s_Q$ 升序排列的不同对象序列，其对象集合必须满足

$$
\widehat R(Q)\subseteq\mathcal D_u,
\qquad
|\widehat R(Q)|\le k_Q.
$$

当 $k_Q>0$ 时，查询召回率定义为

$$
\operatorname{Recall@k}(Q)
=
\frac{|\widehat R(Q)\cap R^*(Q)|}{k_Q}.
$$

近似性仅体现在最近邻结果的遗漏，层级过滤条件仍须精确满足。若可行对象少于 $k$ 个，召回率以实际精确结果数量 $k_Q$ 为分母；近似算法返回不足 $k_Q$ 个对象时，分母保持不变。当 $k_Q=0$ 时，查询结果必须为空，该查询不计入平均召回率。

给定有限查询集 $\mathcal W$，令 $\mathcal W_+=\{Q\in\mathcal W\mid k_Q>0\}$。当 $\mathcal W_+\ne\varnothing$ 时，其平均召回率为

$$
\overline{\operatorname{Recall@k}}(\mathcal W)
=
\frac{1}{|\mathcal W_+|}
\sum_{Q\in\mathcal W_+}
\operatorname{Recall@k}(Q).
$$

因此，本文的查询处理问题可以表述为：给定静态数据集 $\mathcal D$、属性层级 $T$、距离函数 $\delta$、索引内存预算 $B$ 和目标平均召回率 $\rho\in(0,1]$，设计索引与查询算法，在索引内存不超过 $B$、所有返回对象均满足层级过滤条件且平均召回率不低于 $\rho$ 的约束下，最小化给定查询工作负载上的平均查询时间。精确查询要求每个查询均返回 $R^*(Q)$；平均召回率约束不等同于逐查询召回率保证。


# Refined Problem Definition

一般的 overlap-filtered top-k 查询要求对象标签与查询标签至少有一个共同元素。为保留外部分类结构，本文进一步考虑以少量分类节点紧凑表示的子树并集查询。该查询是一般 overlap 语义的结构化子类；以下约束用于明确研究范围与资源目标。

**定义 7（紧凑层级查询，Compact Hierarchical Query）。** 给定定义 1 中的分类树 $T$，查询节点集合 $U\subseteq V$ 为非空 antichain，即其中任意两个不同节点互非祖先。其覆盖的标签集合定义为

$$
F_T(U)=\bigcup_{u\in U}\operatorname{Desc}^{+}(u).
$$

任意非空节点集合均可通过删除已有祖先节点覆盖的后代节点，转化为覆盖范围不变的 antichain。查询输入为节点集合 U 及对静态分类树的引用，无须预先展开全部后代。$|U|=1$ 时恢复定义 4 的单节点查询；可用 $|U|\le b$ 限定工作负载中的紧凑谓词宽度，其中 b 为给定参数。

**定义 8（结构化重叠过滤查询，Structured Overlap-Filtered Query）。** 给定数据集 $\mathcal D$、分类树 $T$、距离函数 $\delta$ 及查询 $Q=(\mathbf q,U,k)$，可行集合为

$$
\mathcal D_U
=
\{o_i\in\mathcal D\mid L_i\cap F_T(U)\ne\varnothing\}
=
\bigcup_{u\in U}\mathcal D_u.
$$

精确结果为 $\mathcal D_U$ 中按定义 4 的距离与 ID 排序规则选出的前 $k_Q=\min\{k,|\mathcal D_U|\}$ 个不同对象。近似结果与 Recall@k 沿用定义 6，将其中的可行集合替换为 $\mathcal D_U$。过滤与去重必须精确满足，近似性只体现在最近邻选择。

令 $\operatorname{Anc}^{+}(v)$ 为包含 v 自身的祖先集合，$A_i=\bigcup_{v\in L_i}\operatorname{Anc}^{+}(v)$，有

$$
L_i\cap F_T(U)\ne\varnothing
\quad\Longleftrightarrow\quad
A_i\cap U\ne\varnothing.
$$

因此查询既可以通过展开后代标签执行，也可以在祖先展开的对象标签上执行；两种归约保持相同答案，其实际表示与执行成本均属于研究对象。

**定义 9（对象交叠度，Object Overlap Multiplicity）。** 对于 $\mathcal D_U\ne\varnothing$ 的查询，定义

$$
\mu(U)=\frac{\sum_{u\in U}|\mathcal D_u|}{|\mathcal D_U|}.
$$

该值描述可行对象在各查询分支中的平均成员次数，满足 $1\le\mu(U)\le|U|$。节点子树互不相交不意味着其对象集合不相交；跨分支多标签对象可使 $\mu(U)>1$。单标签对象下 $\mu(U)=1$。交叠度用于描述工作负载，不作为无需计算即可获取的在线输入。

**定义 10（资源约束下的查询处理目标，Resource-Constrained Query Processing）。** 给定静态数据集、分类树、距离函数、查询工作负载分布 $\mathcal P$，以及常驻内存预算 $B$、给定并发条件下的总峰值内存预算 $B_{\mathrm{peak}}$ 和目标平均召回率 $\rho\in(0,1]$，设计索引 $I$ 与搜索策略 $\pi$，使

$$
\begin{aligned}
\min_{I,\pi}\quad
& \mathbb E_{Q\sim\mathcal P}[T_{I,\pi}(Q)]\\
\text{s.t.}\quad
& M_{\mathrm{resident}}(I)\le B,\\
& M_{\mathrm{peak}}(I,\pi)\le B_{\mathrm{peak}},\\
& \mathbb E_{Q\sim\mathcal P\mid k_Q>0}
  [\operatorname{Recall@k}_{I,\pi}(Q)]\ge\rho.
\end{aligned}
$$

常驻内存包括向量、标签、分类树、posting/bitmap、全局及局部索引、路由和持久缓存；共享数据只计一次。峰值内存还包括并发查询的临时索引、候选及去重结构。查询时间从接收紧凑输入开始，到产生最终结果结束，包括谓词解析、合格对象 ID 生成、临时索引构造、搜索及去重合并。离线构建成本单独报告。工作负载可包含此前未物化的节点组合，索引选择与参数调优不得使用测试查询真值。所有返回对象还必须满足定义 8 的过滤、去重与数量条件。

上述目标的平均召回率通过独立测试工作负载估计，不构成逐查询或任意分布的理论保证。首版范围固定为静态树和静态向量数据上的单机内存查询。该细化的候选研究点是利用显式层级及对象交叠共享索引与搜索工作；不能仅凭支持 overlap、增加预算或规定对象去重宣称新颖性。[文献比较与研究假设](overlap-refinement.md)给出证据、强基线、最小实验及边界。

# Naive Solutions

以下先以单节点查询说明四种直接方案。对于定义 8 的节点集合 U，将查询子树替换为其子树并集，并在局部索引列表中消除重复，便可得到对应的结构化 overlap 基线。这些方案分别从过滤顺序与索引粒度出发，为分析查询时间、结果质量和索引空间之间的折中提供基线。以下讨论采用相同的距离函数与标识并列规则，所有合并操作均按对象标识去重。

## Filter Then Scan

该方案首先根据查询节点 $u$ 确定可行集合 $\mathcal D_u$，随后计算查询向量与所有可行对象之间的距离，并选出排序最前的 $k_Q$ 个对象。可为每个直接标签维护倒排表，通过合并查询子树内的标签倒排表获得 $\mathcal D_u$。

令 $n_u=|\mathcal D_u|$，$C_{\mathrm{filter}}(u)$ 为子树访问、倒排表合并及去重的总成本。对于单次距离计算为 $O(d)$ 的距离函数，使用大小不超过 $k_Q$ 的堆维护结果时，查询时间为

$$
O\!\left(
C_{\mathrm{filter}}(u)+n_ud+n_u\log(k_Q+1)
\right).
$$

该方案返回精确答案，可作为近似算法的真值基线。其距离计算量随可行对象数量线性增长，因此，当查询覆盖较大子树时，扫描成本可能成为主要开销。对于较小的可行集合，该方案也可能比访问向量索引更高效。

## Global Search Then Filter

该方案在全体对象上建立一个向量索引。对于查询 $Q$，首先检索全局前 $m$ 个候选对象，其中 $\min\{k,n\}\le m\le n$，随后保留满足 $P_u$ 的对象，并返回其中排序最前的至多 $k_Q$ 个对象。若结果不足，可扩大 $m$ 并继续搜索。

令 $C_m(\mathbf q)$ 为检索得到的候选集合，则输出为

$$
\widehat R(Q)
=
\operatorname{TopK}_{o_i\in C_m(\mathbf q)\cap\mathcal D_u}
\left(
 s_Q(o_i);
 \min\{k_Q,|C_m(\mathbf q)\cap\mathcal D_u|\}
\right).
$$

若 $C_m(\mathbf q)$ 是按相同排序规则得到的精确全局前 $m$ 个对象，且其中已有 $k_Q$ 个可行对象，则可以得到精确答案；若采用 ANN 候选，即使结果数量达到 $k_Q$，也不能据此保证结果精确。固定的候选预算无法保证覆盖过滤集合中的最近邻：当可行对象在全局排序中位置较后时，需要检索大量不满足条件的对象，最坏情况下须检查全部对象。该方案只需一个全局索引，但查询成本与可行对象在查询邻域中的分布密切相关。

## Search Per Label and Merge

该方案为每个具有直接标签对象的节点 $v$ 建立局部向量索引，其对象集合为

$$
B_v=\{o_i\in\mathcal D\mid v\in L_i\}.
$$

对于查询节点 $u$，访问其子树内所有非空直接标签索引，分别检索至多 k 个局部近邻，再将局部结果按对象标识去重并合并为最终 top-k。由于

$$
\mathcal D_u
=
\bigcup_{v\in\operatorname{Desc}^{+}(u)}B_v,
$$

该组织方式能够覆盖内部节点直接标签和叶子标签。仅当所有直接标签均位于叶子时，才可将其简化为逐叶子索引搜索。

若每个局部索引均返回精确 top-k，则合并结果也是精确的。原因在于：任意全局前 $k_Q$ 对象，在每个包含它的局部集合中，都不可能有 k 个不同对象排在它之前。多标签导致的集合重叠不改变这一性质，但会引入重复候选；若局部搜索使用 ANN，则需独立评价最终合并结果的召回率。

令 $b_u$ 为查询子树内非空直接标签索引的数量，则局部搜索阶段产生的候选条目数至多为 $kb_u$。较粗粒度的查询可能访问较多局部索引，产生多次搜索启动和候选合并开销。此外，对象可能在多个直接标签索引中重复出现，逻辑索引成员总数为 $\sum_{i=1}^{n}|L_i|$；具体向量与图结构的存储成本取决于实现是否共享数据。

## Build an Index for Every Subtree

该方案为每个具有非空可行集合的分类节点 $u$ 建立独立向量索引，索引覆盖整个 $\mathcal D_u$。查询时只访问对应节点的索引，因此无需在线合并多个类别的搜索结果。若采用精确检索，则返回 $R^*(Q)$；若采用 ANN，则在该可行集合内部产生近似结果。

这种组织方式将查询范围的整合工作移至索引构建阶段。由于对象同时属于其标签节点及相应祖先节点的可行集合，逻辑索引成员总数为

$$
S_{\mathrm{subtree}}
=
\sum_{u\in V}|\mathcal D_u|
=
\sum_{i=1}^{n}
\left|
\bigcup_{v\in L_i}\operatorname{Anc}^{+}(v)
\right|,
$$

其中，$\operatorname{Anc}^{+}(v)$ 表示包含节点自身的祖先集合。对于单标签且所有标签均位于深度 h 的叶子这一特例，根节点深度为 0 时，有 $S_{\mathrm{subtree}}=n(h+1)$。该式描述逻辑成员重复量，实际字节开销还取决于向量、标识及索引结构的共享方式。即使共享底层向量，独立子树索引仍可能重复维护搜索结构，从而增加构建与存储成本。

上述方案表明，直接扫描的开销主要取决于可行集合规模，全局检索后过滤的开销取决于有效近邻在全局候选中的分布，逐标签索引的开销受查询覆盖的索引数量影响，而独立子树索引通过更多预处理和索引成员重复减少在线范围整合。由此引出的研究问题是：能否利用层级集合的包含关系共享索引结构或搜索工作，在给定内存预算和最终召回目标下改善不同粒度查询的执行效率。
