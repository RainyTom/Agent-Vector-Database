# 面向层级知识体系的向量检索：Exact/Subtree 双语义与 Laminar Edge Sharing 索引（研究草案 v3）

状态：研究草案（2026-10-20）。本文以 **ACM CCS** 为主应用背景，形式化“递归子树（Subtree）”与“非递归直属（Exact）”两类层级查询，提出 Laminar Edge Sharing（LES）索引与两段式分桶，给出正确性证明、空间/度数分析与实验设计。**LES 已完成结构、证明与静态分析；端到端查询原型与 Exact 工作负载尚未实现，因此不把 LES 的查询性能当作已验证结果，也不主张新颖性成立。** 逐作用域物化实现（B4）作为强基线，工程细节见附录 A。

---

## 摘要

学术检索、企业知识库与商品目录都把对象组织在层级分类下；对象可挂多个类目，类目成员沿祖先继承。检索需求并非单一：用户既要**递归地**检索某类目整棵子树（领域全景检索），也要**非递归地**只检索直接挂在该类目下的对象（综述、框架、立场论文通常直接挂在中间类目，而不属于任何具体叶子）。这两类语义正交，且在扁平多标签系统上分别触发“后代倒排求并再做差集”的灾难与过滤图搜索的谓词死胡同。为每个子树（以及每个直属集合）独立建图又使索引空间按 $\sum_u|\mathcal D_u|$ 膨胀。

本文形式化 Exact/Subtree 双语义查询与统一代价模型，提出 **Laminar Edge Sharing（LES）**：每条图边按其在层级上的最小激活节点只存一次，并区分 **直属桶 $B_{\text{exact}}$** 与 **跨子树桶 $B_{\text{cross}}$**；DFS 先序使子树成为连续区间，查询只做区间切片，**热路径不执行任何谓词判定**。我们证明两类切片分别精确等于 Exact 与 Subtree 的作用域边集，并给出空间/度数边界。在 SIFT100k 的 85 节点层级上，逐作用域物化需要 760 万条有向边、83 MB；LES 去重后为 366 万条唯一边，两段桶合计 382 万条（激活多重度 1.045），估算常驻约 33 MB，代价是根作用域每跳平均出度从 19.4 升到 36.6。强基线 B4 在 Recall@10 0.9570 时达到 94,698 qps。真实 CCS 数据、Exact 工作负载与 LES 查询评测列为待补实验。

---

## 1 应用背景

### 1.1 为什么以 ACM CCS 为主场景

ACM Computing Classification System（CCS）是计算机领域论文的标准分类体系：深度 4–6 的树状结构，节点数千，论文按主题挂载 **2–5 个直接标签**，语义上分为两类典型对象：

- **细分研究论文**挂在叶子或深层节点（如 *Information retrieval → Search engine architectures → ...*），主题高度垂直；
- **综述、框架、立场与全景导论论文**往往**直接挂在中间甚至较粗的节点**（如 *Information retrieval* 本身），因为它们讨论的是领域整体而非某个具体算子。

这种“粗粒度直属对象 + 细粒度下钻对象”共存的长尾分布，使 CCS 同时具备层级继承、多标签重叠与粒度混合三个特征；它既是学术界人人熟悉的标准，也正好压住了向量检索系统最难处理的两类查询。

### 1.2 两类正交的查询语义

**递归查询（Subtree / exploratory search）.** 用户想看属于 *Information systems → Information retrieval* 整棵子树的论文：包括直接挂在 *Information retrieval* 的综述，也包括挂在 *Search engine architectures*、*Relevance assessment* 等所有后代的细分论文。可行集是 $u$ 及其全部子孙的并集。

**非递归查询（Exact / survey & foundational search）.** 用户想找关于 *Information retrieval* 本身的概览、范式与全局基准，而**排除**过度细化在具体叶子上的实验论文。可行集只包含**直接挂载**在 $u$ 的论文，不能包含任何仅通过后代标签继承而来的论文。

两者不是同一查询的不同参数，而是相反方向：Subtree 向下扩张，Exact 严格截断。真实系统必须同时支持，且支持若干类目的组合（antichain）：

- Subtree antichain：*Machine learning* ∪ *Computer vision* 的全景；
- Exact antichain：*Information retrieval* 与 *HCI* 两个类目的直属综述；
- 混合语义：一个类目取 Subtree，另一个取 Exact（本文把模式作为查询参数，允许每个查询统一选择，混合模式作为扩展）。

### 1.3 现有方案在 CCS 上的失效方式

**（1）扁平多标签 + 差集：Exact 查询的倒排灾难.** 若把树展开成扁平标签（每个对象打上全部祖先标签），则 Exact 查询必须做

$$
\mathcal D_{\text{exact}}(u)
=\mathcal D_{\text{tag}}(u)\setminus\Bigl(\bigcup_{v\in\operatorname{Desc}(u)}\mathcal D_{\text{tag}}(v)\Bigr).
$$

CCS 的中高层节点后代可达数十至数百个。执行该差集需要把大量后代倒排表求并，再与父节点做集合差；内存带宽被打满，且结果集合可能很小。更严重的是，若把它接到过滤图搜索上，图中邻居绝大多数属于被排除的后代，贪心搜索会持续遇到谓词失败，形成**密集谓词失败与死胡同**。

**（2）全局图 + 后过滤：两种语义都退化.** Subtree 在低选择率子树下候选预算被浪费；Exact 的选择率往往更低（综述是长尾），固定候选预算无法保证过滤后 top-k。过滤式图搜索通常按“合格候选数”计数，深选择率下退化为近全图遍历。

**（3）逐子树物化：空间爆炸.** 若为每个节点物化 $\mathcal D_u$，成员总数为 $\sum_u|\mathcal D_u|$；CCS 深度 4–6、论文 2–5 个标签，该值可达对象数的数倍。若同时物化 Exact 集合 $\mathcal B_u$，索引数量变为 $2|V|$，边与元数据进一步膨胀；目录调整时重建范围大。

**（4）逐标签索引 + 合并：扇出爆炸.** 粗粒度 Subtree 要访问子树内全部标签索引；Exact 还要做差集，启动、候选合并与去重成本随分支数增长，对象多标签使同一向量在多个索引中重复。

**（5）段树/区间图索引（iRangeGraph 风格）.** 面向数据顺序上的连续区间；子树成员由标签决定，且多标签使集合不是数据序上的连续区间，直接套用需要额外映射与去重；Exact 的“排除后代”更不是区间运算。

### 1.4 设计需求

- **R1 双语义精确.** Exact 不得泄漏后代标签；Subtree 必须包含后代；结果按对象 ID 去重。
- **R2 任意粒度与 antichain.** 根、中间节点、叶、并列类目都要服务。
- **R3 热路径无谓词.** 逐候选做标签/位图/差集判定会把过滤成本摊到每次距离计算；理想情况下邻接本身即“该作用域可用”。
- **R4 空间低于双语义物化.** 边与向量不应按每个作用域重复。
- **R5 构建可承受、只依赖静态数据.**
- **R6 可复现、可度量.** 确定性结果，索引字节与构建时间如实计入。

### 1.5 其他场景

企业知识库 RAG（部门/产品目录子树，块多标签）、商品目录（跨类目商品）与 CCS 同构：层状作用域、继承、多标签去重、粗细粒度并存。CCS 是主场景，其余作为外部有效性讨论。

---

## 2 问题定义

### 2.1 符号

| 符号 | 含义 |
|---|---|
| $T=(V,E,r)$ | 属性层级，$m=\lvert V\rvert$ |
| $L_i\subseteq V$ | 对象 $i$ 的**直接**标签集 |
| $\mathcal B_u=\{i\mid u\in L_i\}$ | 直接挂载在 $u$ 的对象（Exact 作用域） |
| $\mathcal D_u=\bigcup_{v\in\operatorname{Desc}^{+}(u)}\mathcal B_v$ | 子树作用域（Subtree） |
| $A_i=\bigcup_{v\in L_i}\operatorname{Anc}^{+}(v)$ | 激活集（$i$ 可行的节点集合） |
| $\mathcal S_m(U)$ | 模式 $m\in\{\text{exact},\text{subtree}\}$ 下查询节点集合 $U$ 的作用域 |
| $\delta$ | 距离（平方 L2） |
| $s_Q(i)$ | 排序键 $(\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i)$ |
| $k_Q$ | $\min(k,\lvert\mathcal S_m(U)\rvert)$ |

### 2.2 定义

**定义 1（属性层级）** 有限有根树 $T=(V,E,r)$；$\operatorname{Desc}^{+}(u)$ 含自身；$\operatorname{Anc}^{+}(u)$ 对偶。子节点是父类别的细分。

**定义 2（带直接标签的数据集）** $\mathcal D=\{(\mathbf x_i,L_i)\}_{i=1}^{n}$，$\mathbf x_i\in\mathbb R^d$，$\varnothing\ne L_i\subseteq V$，标签可位于任意深度。直接标签是唯一事实来源；继承成员关系由定义 3 导出。ID 唯一。

**定义 3（直接集、子树集与激活集）**

$$
\mathcal B_u=\{i\mid u\in L_i\},\qquad
\mathcal D_u=\{i\mid L_i\cap\operatorname{Desc}^{+}(u)\ne\varnothing\}=\bigcup_{v\in\operatorname{Desc}^{+}(u)}\mathcal B_v,
$$

$$
A_i=\bigcup_{v\in L_i}\operatorname{Anc}^{+}(v),\qquad
u\in A_i\iff i\in\mathcal D_u.
$$

$A_i$ 向上封闭；$\mathcal B_u\subseteq\mathcal D_u$；$v\in\operatorname{Desc}^{+}(u)\Rightarrow\mathcal D_v\subseteq\mathcal D_u$。

**定义 4（查询模式与作用域）** 查询 $Q=(\mathbf q,U,k,m)$，$U\subseteq V$ 非空 antichain，$m\in\{\text{exact},\text{subtree}\}$。作用域

$$
\mathcal S_{\text{exact}}(U)=\bigcup_{u\in U}\mathcal B_u,
\qquad
\mathcal S_{\text{subtree}}(U)=\bigcup_{u\in U}\mathcal D_u.
$$

Exact 模式只返回直接挂载于 $U$ 中节点的对象；Subtree 模式返回这些节点子树内的全部对象。两者均按对象 ID 去重。

**定义 5（精确结果）** 排序键 $s_Q(i)=(\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i)$ 字典序；$k_Q=\min(k,|\mathcal S_m(U)|)$；$R^*(Q)$ 为作用域内按 $s_Q$ 升序的前 $k_Q$ 个不同对象。空作用域返回空。

**定义 6（近似与召回）** $\widehat R(Q)\subseteq\mathcal S_m(U)$、$|\widehat R(Q)|\le k_Q$、无重复；$k_Q>0$ 时 $\operatorname{Recall@k}(Q)=|\widehat R(Q)\cap R^*(Q)|/k_Q$，$k_Q=0$ 时要求返回空且不计入平均召回。近似只允许最近邻遗漏。

**定义 7（交叠度）** $\mu_m(U)=\sum_{u\in U}|\mathcal S_m(u)|/|\mathcal S_m(U)|$，刻画多标签造成的跨节点重叠；Exact 模式下该值通常接近 1（直接标签重叠较少），Subtree 模式可显著大于 1。

**定义 8（索引与代价）** 索引 $I$ 由 $\mathcal D,T$ 构建，构建期不得读取测试查询或真值。$M(I)$ 为常驻字节：向量（每对象一份）、各层邻接条目、激活标记、对象桶偏移、树/标签、每节点入口与高度；共享数据只计一次。$T_{\text{build}}$ 为构图、边归属、排序与落盘时间。$T_{I,\pi}(Q)$ 从接收查询到返回结果，包含模式解析、候选生成、搜索、距离、去重与合并；LES 的模式解析为 $O(1)$（选择区间端点），热路径无谓词。

**定义 9（研究问题）** 给定 $\mathcal D,T,\delta$、查询分布 $\mathcal P$（含两种模式与 antichain）、召回目标 $\rho$、内存预算 $B$，

$$
\min_{I,\pi}\ \mathbb E_{Q\sim\mathcal P}[T_{I,\pi}(Q)]
\quad\text{s.t.}\quad
M(I)\le B,\quad
\mathbb E_{\mathcal P\mid k_Q>0}[\operatorname{Recall@k}(Q)]\ge\rho,
$$

且每个查询返回 $\widehat R(Q)\subseteq\mathcal S_m(U)$、无重复、数量为 $\min(k,|\mathcal S_m(U)|)$（不足时返回全部）。

### 2.3 双语义的具体差异（CCS 例子）

设类目 $u=$ *Information retrieval*，子节点 $v=$ *Search engine architectures*。论文：

- $p_1$ 直接挂 $u$（综述）；
- $p_2$ 直接挂 $v$（细分论文）；
- $p_3$ 同时直接挂 $u$ 与 *Machine learning*。

则 $\mathcal B_u=\{p_1,p_3\}$，$\mathcal D_u=\{p_1,p_2,p_3\}$。Exact$(u)$ 返回 $\{p_1,p_3\}$ 中的近邻，$p_2$ 即使向量更近也必须排除；Subtree$(u)$ 返回三者。若把树展平为祖先标签，Exact$(u)$ 必须从 $\{p_1,p_2,p_3\}$ 中减去后代集合 $\{p_2\}$；在真实 CCS 中这个差集的并侧有数百个后代，成本与谓词失败率都不可忽略。

### 2.4 边界情形

- **Exact 作用域为空**：叶子通常没有直属论文；Exact 查询必须返回空，不能回退到子树。延迟计入，召回不计。
- **Subtree 作用域非空但直属为空**：Subtree 仍要工作。
- **不足 $k$**：返回全部；召回分母为作用域大小。
- **多标签重复**：同一对象通过多个直接标签命中 Exact/Subtree 时只返回一次。
- **内部节点标签**：直属中间节点不等于属于任一特定后代；Exact 与 Subtree 的区别正源于此。
- **距离并列**：按 ID 升序。
- **模式混合**：若允许一个查询对不同节点取不同模式，作用域为各节点各自模式作用的并；本文默认整查询统一模式，混合作为扩展。

### 2.5 指标

- `qps_at_r95`：平均 Recall@10 ≥ 0.95 且过滤违规为 0 时的吞吐；否则为 0。
- Recall@10（定义 6）、空结果正确率、p95、索引字节、构建时间、违规数。
- 分模式与分工作负载报告（$Q_{\text{leaf}},Q_{\text{subtree}},Q_{\text{exact}},Q_{\text{antichain}}$），避免均值掩盖 Exact 的退化。

### 2.6 假设

静态层级与向量；单机内存、单线程；构建期 $T$ 与 $L_i$ 已知、查询不可见；距离平方 L2（SIFT 整数值用 u8 精确编码）；不处理更新、删除、并发、权限与多版本。

---

## 3 方案：Laminar Edge Sharing（LES）

### 3.1 概览

LES 用一套按“最小激活节点”去重的边同时服务 Exact 与 Subtree：

1. 每条边 $(i,j)$ 在两种语义下各有激活位置：Exact 的激活是**共同直接标签** $L_i\cap L_j$；Subtree 的激活是**共同可行祖先**的极小元 $\min(A_i\cap A_j)$。
2. 每个对象维护按 DFS 先序排序的桶表；同一节点 $u$ 的桶内，**直属条目在前、跨子树条目在后**。
3. 查询只做区间切片：Exact$(u)$ 取 $u$ 的直属段；Subtree$(u)$ 取 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 的整段。热路径没有谓词判定。

### 3.2 激活集与两种边激活

- **Subtree 激活**：$M^{\text{sub}}_{ij}=A_i\cap A_j$，向上封闭，极小元 $\min(M^{\text{sub}}_{ij})$（§3.3）。
- **Exact 激活**：$M^{\text{exact}}_{ij}=L_i\cap L_j$，即两端都被直接挂载的共同节点。Exact 不需要闭包：只有直接标签才构成直属关系。

*CCS 例子.* $p_1$ 直属 $u$，$p_2$ 直属 $v\in\operatorname{Desc}^{+}(u)$：$M^{\text{exact}}_{12}=\varnothing$（不能进入 Exact$(u)$），$M^{\text{sub}}_{12}=\operatorname{Anc}^{+}(u)\cap\operatorname{Anc}^{+}(v)=\operatorname{Anc}^{+}(u)$，极小元 $\{u\}$。
$p_1$ 直属 $u$，$p_3$ 直属 $u$ 与 ML：$M^{\text{exact}}=\{u\}$，$M^{\text{sub}}=\{u,\text{ML}\}$（极小元 $\{u,\text{ML}\}$）。

### 3.3 最小激活

**定义 10** $\min(M)=\{u\in M\mid \nexists v\in M,\ v\in\operatorname{Desc}^{+}(u),\ v\ne u\}$，即 $M$ 中没有后代也在 $M$ 的节点；它是 antichain。

- Subtree 的最小激活集规模不超过 $|L_i|\cdot|L_j|$（本文 ≤9，通常为 1）。
- Exact 的最小激活就是 $L_i\cap L_j$ 本身（直接标签是 antichain 吗？对象的直接标签可包含祖先-后代对吗？本文数据模型允许，但 Exact 语义下若 $u$ 与 $v\in\operatorname{Desc}^{+}(u)$ 同时直接挂载，则两者都成立；无需取极小元。为保证桶不重复，仍对 $L_i\cap L_j$ 去重即可）。

### 3.4 两段式分桶

每个对象在层 $\ell$ 的桶表按 $\mathrm{tin}$ 升序排列；对同一节点 $u$，条目分为连续两段：

| 段 | 内容 | 服务的查询 |
|---|---|---|
| $B_{\text{exact}}(u)$ | 两端都直接挂载于 $u$ 的边（$u\in L_i\cap L_j$） | Exact$(u)$ |
| $B_{\text{cross}}(u)$ | Subtree 最小激活为 $u$、且该边不在 $B_{\text{exact}}(u)$ 的边 | Subtree$(u)$ 及其祖先 |

**归属规则（构建期）：**

1. 从各作用域的图中收集有向边；同一条 $(i,j)$ 只处理一次。
2. Exact 段：对每个 $u\in L_i\cap L_j$，在对象 $i$ 的 $B_{\text{exact}}(u)$ 追加邻居 $j$。
3. Subtree 段：对每个 $m\in\min(A_i\cap A_j)$，若 $m\notin L_i\cap L_j$，在 $B_{\text{cross}}(m)$ 追加 $j$；若 $m\in L_i\cap L_j$，该边已由 $B_{\text{exact}}(m)$ 覆盖 Subtree 的切片，不再重复。
4. 每个桶内按邻居 ID 去重；对象桶表按 $(\mathrm{tin}, \text{段}, j)$ 排序，并记录每个节点直属段的起止偏移。

**为什么 Exact 段覆盖 Subtree.** 若边在 $u$ 直属（两端直接挂 $u$），则 $u\in M^{\text{sub}}_{ij}$，且 $u$ 的子树切片包含 $u$ 的直属段；因此 Subtree$(u)$ 能看到它。若该边同时有更深的 Subtree 最小激活 $m\in\operatorname{Desc}^{+}(u)$，则在 $B_{\text{cross}}(m)$ 另存一份，供 Subtree$(m)$ 使用。反之，仅由后代继承而来的边不会进入 $B_{\text{exact}}(u)$，Exact 语义不被污染。

### 3.5 切片代数

DFS 先序记录 $\mathrm{tin}(u),\mathrm{tout}(u)$，则

$$
\mathrm{Slice}_{\text{exact}}(i,u)=B_{\text{exact}}(i,u),
\qquad
\mathrm{Slice}_{\text{subtree}}(i,u)=\bigcup_{v\in\operatorname{subtree}(u)}\bigl(B_{\text{exact}}(i,v)\cup B_{\text{cross}}(i,v)\bigr),
$$

后者是对象 $i$ 的桶表中 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 的连续片段（因为直属段与跨子树段都按节点先序排列，且同一节点内直属段在前）。查询：

- **Exact$(u)$**：只扫描对象桶表中 $u$ 的直属段；
- **Subtree$(u)$**：扫描整个子树区间（自然吞并直属段）；
- **Antichain**：对 $U$ 中每个节点按模式取对应切片，独立搜索后按 ID 去重合并。

整个热路径不需要读取标签、位图或执行差集。

### 3.6 存储布局

对每个层 $\ell$：

```
对象 i 的桶表（按 tin 升序）：
  [ u 的直属段 | u 的跨子树段 ][ v 的直属段 | v 的跨子树段 ] ...
全局条目数组 E_ℓ：所有对象按对象顺序拼接
对象偏移 O_ℓ[n+1]；每节点直属段偏移 D_ℓ[n]（start, end）
条目编码：邻居 ID 4 B + 激活先序 1–2 B
```

另存共享向量（$n\times d$ 字节；u8 时 12.8 MB）、每节点入口 $\mathrm{seed}(u)$ 与高度 $h(u)$、树与标签。空间

$$
M_{\text{LES}}\approx nd
+\sum_\ell E_{\text{bucket},\ell}\cdot(4+\lceil\log_2 m\rceil/8)
+4n(L_{\max}+1)+O(n+m).
$$

### 3.7 构建步骤

1. DFS 先序编号，得到 $\mathrm{tin}/\mathrm{tout}$ 与父子关系。
2. 每个对象计算 $A_i$（祖先闭包）与直接标签掩码。
3. 对每个节点构建作用域图：Subtree 图 $G^{\text{sub}}_u$（成员 $\mathcal D_u$）与 Exact 图 $G^{\text{exact}}_u$（成员 $\mathcal B_u$，仅当 $|\mathcal B_u|$ 足够大时构建）。本文用 HNSW（$M=24$、$efC=200$、level-0 截断到最近 24）；也可替换为其他构造器。
4. 遍历所有图的有向边，按 $(i,j)$ 去重后计算两种激活集；按 §3.4 规则生成桶条目。
5. 每个对象每层按 $(\mathrm{tin}, \text{段}, j)$ 排序、去重；计算全局条目数组、对象偏移与直属段偏移。
6. 写共享向量、条目与偏移、每节点入口/高度、树与标签。

步骤 4 与构图解耦，便于与 B3/B4/B5 使用同一套边归属逻辑做公平对照。

### 3.8 查询算法

单节点查询 `Search(u, q, mode, k, ef)`：

```
if mode == exact:
    lo ← D[u].start; hi ← D[u].end          # u 的直属段
    seed ← firstDirect[u]
else:
    lo ← tin(u);  hi ← tout(u)              # 子树区间
    seed ← seed[u]
ep ← seed; d ← δ(q, x_ep)
for ℓ = h(u) down to 1:                      # 上界层贪心，使用同一区间规则
    changed ← true
    while changed:
        changed ← false
        for j in Slice(ep, lo, hi, ℓ):
            dj ← δ(q, x_j)
            if (dj, j) < (d, ep): ep ← j; d ← dj; changed ← true
visited ← {ep}; top ← max-heap{(d, ep)}; cand ← min-heap{(d, ep)}
while cand ≠ ∅:
    (dc, c) ← pop-min(cand)
    if |top| = ef and dc > max(top): break
    for j in Slice(c, lo, hi, 0):
        if j ∈ visited: continue
        visited ← visited ∪ {j}
        dj ← δ(q, x_j)
        if |top| < ef or dj < max(top):
            push(cand, (dj, j)); push(top, (dj, j))
            if |top| > ef: pop-max(top)
return k smallest (δ, id) in top
```

`Slice(c, lo, hi, ℓ)` 在对象 $c$ 的层 $\ell$ 桶表中二分定位区间并返回邻居；同一邻居可能因多个激活位置出现多次，`visited` 去重。antichain：对每个 $u\in U$ 按模式执行，合并结果并按 ID 去重。不能把不同节点的切片并成一次搜索：$i$ 可能只在 $u_1$ 可行、$j$ 只在 $u_2$ 可行，二者都在并集中，但边 $(i,j)$ 的激活集不含 $u_1,u_2$（Exact 模式尤其明显）。

### 3.9 正确性

**引理 1（Exact 切片）** $\mathrm{Slice}_{\text{exact}}(i,u)=\{j\mid u\in L_i\cap L_j\}$。

*证明.* （$\subseteq$）条目在 $B_{\text{exact}}(u)$ 生成时要求 $u\in L_i\cap L_j$。（$\supseteq$）若 $u\in L_i\cap L_j$，构建规则 2 在 $B_{\text{exact}}(u)$ 追加该边（若该边存在于边全集）。$\square$

**引理 2（Subtree 切片）** $\mathrm{Slice}_{\text{subtree}}(i,u)=\{j\mid i,j\in\mathcal D_u\}$。

*证明.* （$\subseteq$）条目位于某 $v\in\operatorname{subtree}(u)$ 的直属段或跨子树段。直属段要求 $v\in L_i\cap L_j$，故 $u\in A_i\cap A_j$；跨子树段要求 $v\in\min(A_i\cap A_j)$，向上封闭给出 $u\in A_i\cap A_j$。两种情形都有 $i,j\in\mathcal D_u$。（$\supseteq$）若 $i,j\in\mathcal D_u$，则 $u\in A_i\cap A_j$；从 $u$ 下降到仍在交集中的子节点，停在某极小元 $v\in\operatorname{subtree}(u)$。若 $v\in L_i\cap L_j$，边在 $B_{\text{exact}}(v)$；否则在 $B_{\text{cross}}(v)$；两段都在区间内。$\square$

**定理 1（查询语义）** 在 LES 上按上述算法执行的 beam search，与在“边集为该模式切片所定义的图”上执行同参数 beam search 访问相同候选；Exact 不包含仅由后代继承的对象，Subtree 包含；antichain 合并后覆盖并集且按 ID 去重。过滤与去重精确，近似只来自 $ef$ 截断。

*证明.* 由引理 1/2，每次弹出的邻居集合相同，距离与堆操作相同；antichain 的作用域是各模式作用域之并，逐节点搜索覆盖并集，去重按 ID。$\square$

**注.** LES 的 Subtree 图 $G_u$ 是“边全集中两端可行”的边之并，可能比“为 $u$ 单独构图”的图度数更大（实测 1.0–1.9×）；定理保证的是 LES 自身图上的语义，与 B4 的召回差异是经验问题。

### 3.10 复杂度与空间

- **构建**：构图 $O(\sum_u n_u\log n_u)$；边归属 $O(E_{\text{obs}}\cdot(|L|+m/64))$；排序 $O(\sum_i d_i\log d_i)$。Exact 图只对 $|\mathcal B_u|\ge\tau$ 的节点构建，额外边数 $\le\sum_{|\mathcal B_u|\ge\tau}|\mathcal B_u|\cdot M$。
- **空间**：$E_{\text{bucket}}=\sum_e(|L_i\cap L_j|+|\{m\in\min(A_i\cap A_j):m\notin L_i\cap L_j\}|)$ 条条目；向量每对象一份。
- **查询**：每次弹出 $O(\log d_i+\deg_{u,m}(i))$；Exact 的度数通常远小于 Subtree；Subtree 度数上界 $\deg_u(i)\le\sum_{v\in\operatorname{subtree}(u)}\deg_v(i)$。

### 3.11 与基线的结构对比

| 维度 | 全局过滤 B2 | 逐标签 B3 | 双语义物化 B4 | LES |
|---|---|---|---|---|
| Exact 语义 | 差集 + 谓词判定 | 差集 + 多索引 | 独立 Exact 索引 | 直属段切片 |
| Subtree 语义 | 低选择率退化 | 多索引合并 | 独立子树索引 | 子树区间切片 |
| 查询期谓词 | 有 | 有 | 无 | 无 |
| 边重复 | 无 | 跨标签重复 | 每作用域重复 | 按激活去重 |
| 向量重复 | 1× | 每标签 1× | 每作用域 1× | 1× |
| 索引数量 | 1 | $\sum_i|L_i|$ | $2|V|$ | 1 套共享结构 |

### 3.12 参数

- 边来源：HNSW（$M=24$、$efC=200$、截断 24）；可替换。
- Exact 图的构建阈值 $\tau$（例如 $2k$）：小于阈值的节点用精确扫描。
- $ef$ 按模式与召回曲线分别选择；LES 的度数更高，不能沿用 B4 的 $ef=21$。
- u8 距离用于 SIFT；通用数据可存 float。
- 大树的激活集用有序祖先表/压缩位图；激活先序用 2–4 B。

---

## 4 基线

| 编号 | 基线 | 关键点 |
|---|---|---|
| B1 | 精确扫描（按模式枚举作用域） | 召回 1.0；Exact 差集成本单独计 |
| B2 | 全局 HNSW + 谓词/位图过滤 | 工业常见；Exact 上差集与谓词失败是主要退化来源 |
| B3 | 逐标签 HNSW + 合并/差集 | 直接利用标签分区；Exact 需后代并集再做差 |
| B4 | 双语义物化 HNSW | 每个 Subtree 与 Exact 集合独立建图；无过滤，最强对照 |
| B5 | 段树共享图（iRangeGraph 风格） | 与 LES 最接近的共享机制，界定 LES 的增量 |

## 5 实验设计

### 5.1 数据

**目标数据（首选）**：SciHTC / arXiv / Semantic Scholar 的论文向量（标题+摘要嵌入）与 ACM CCS 直接标签，保留真实的长尾（综述直属中间节点、细分论文在叶子）。层级节点、多标签分布与作用域大小从真实标签统计得到，不人工合成。

**当前基准**：SIFT100k + 合成 85 节点层级（每对象 1–3 个直接标签，属性与几何相关），用于验证结构与工程实现；**尚未包含真实 CCS 的长尾分布，也没有 Exact 工作负载**。这是现阶段的效度威胁（§7）。

### 5.2 工作负载

| 类型 | 定义 | 目的 |
|---|---|---|
| $Q_{\text{leaf}}$ | 叶节点 Subtree（等价于 Exact，因叶无后代） | 垂直领域检索 |
| $Q_{\text{subtree}}$ | 粗/中节点 Subtree | 领域全景 |
| $Q_{\text{exact}}$ | 中高层节点 Exact | 综述/框架检索；B2 的差集与谓词失败 |
| $Q_{\text{antichain}}$ | 2–3 个节点的并，Subtree 与 Exact 各半 | 跨领域组合 |

每类按作用域大小分层采样；$k=10$；确保 $k_Q>0$ 的查询占多数，空 Exact 查询单独统计。报告每种类型的 qps/召回，不只报加权平均。

### 5.3 指标

`qps_at_r95`、分类型 Recall@10、空结果正确率、p95、索引字节、构建时间、违规数；索引空间按模式分解（直属段/跨子树段/向量/偏移）。

---

## 6 当前结果

### 6.1 基线（合成数据，Subtree 工作负载，已测）

| 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|
| B1 精确扫描 | ≈1,400 | 1.0 | ms 级 | 64 MB | 0 |
| B2 全局 HNSW + 过滤/扫描 | 3,104 | 0.9987 | 0.83 ms | 64 MB | 16 s |
| B4 逐作用域物化（Subtree） | **94,698** | 0.9570 | 20.9 µs | 83 MB | 30.8 s |

Exact 工作负载与 B3/B5 尚未实现，因此上表**不能**说明双语义下的优劣；尤其 B2 在 $Q_{\text{exact}}$ 上的退化尚未实测。

### 6.2 LES 两段式空间/度数分析（已测，边来自 Subtree 图）

| 指标 | 数值 |
|---|---:|
| 观测有向边（各作用域之和） | 7,602,554 |
| 唯一有向边 | 3,658,543 |
| 重复比例 | 51.9% |
| 直属段条目 $B_{\text{exact}}$ | 1,966,214 |
| 跨子树段条目 $B_{\text{cross}}$ | 1,856,024 |
| 两段合计 | 3,822,238（多重度 1.045） |
| 直属成员数 $\sum_u|\mathcal B_u|$ | 115,847（80 个节点非空，最大 5,894） |
| 每作用域平均出度 | 13.0–20.9 |
| 根 Subtree 切片平均出度 | 36.59（1.9×） |
| 深度 1 切片平均出度 | 25.5–31.8（1.3–1.6×） |

空间估算：条目约 $3.82\text{M}\times5\text{B}=19.1$ MB，向量 12.8 MB，偏移与元数据约 1.5 MB，合计约 **33 MB**，对比 B4 的 83 MB。若为 $|\mathcal B_u|\ge\tau$ 的节点额外构建 Exact 图，条目会增加，最坏上界 $\sum|\mathcal B_u|\cdot M\approx2.78\text{M}$，需要实测后再定论。

### 6.3 待补实验（论文成立的前提）

1. 实现 LES 构建（两段分桶、对象排序、直属段偏移）与 Exact/Subtree 查询原型；
2. 生成 $Q_{\text{exact}}$ 与四种工作负载，加入精确 oracle 与违规检查；
3. 实现 B3（逐标签）与 B5（段树共享）基线；
4. 在真实 CCS/SciHTC 数据上重跑 §5，报告分类型结果；
5. 1M 规模复核空间、构建与吞吐。

## 7 效度威胁与局限

- **双语义未完成评测.** Exact 语义、两段式查询与 LES 原型均未实现；本文只能给出结构与空间分析，不能宣称精度或吞吐优势。
- **合成层级缺少 CCS 长尾.** 当前标签分布没有“综述直属中间节点”的真实特征，Exact 查询的规模与选择率会与真实数据不同。
- **新颖性未确认.** LES 与 iRangeGraph 的段树单元图、UNG 的标签导航在共享边上目标相近；需逐机制对照后才能判断“双语义两段桶 + 区间切片”是否构成独立贡献。
- **规模与更新.** 仅 100k、静态、单线程；1M 规模与目录调整的重建成本未测。
- **距离.** 平方 L2；其他度量需重新审视精确编码与几何性质。

## 8 相关工作（简）

过滤 ANN：Filtered-DiskANN、ACORN、UNG、Curator；范围/嵌套索引：iRangeGraph；多属性共享图：JAG。Exact/Subtree 双语义与“最小激活两段桶”是本文的候选差异点，尚需完整对照。

---

## 附录 A：强基线 B4 的实现与消融摘要

B4 每作用域独立构图（hnswlib），查询期自定义紧凑格式：u8 精确距离（SIFT 整数值，块 656→226 B）、向量前置布局、level-0 最近 24 条（226→178 B，索引 108→83 MB）、作用域分组执行、两处预取。参数 $M=24$、$efC=200$、$ef=21$。

消融（交错测量）：分组执行约 +35%；u8 约 +38%；PC1 约 +5%；紧凑块+预取至 86,788 qps；截断与 $ef=21$ 至 94,698 qps。负结果：共享向量、变量长度邻接、小范围扫描、透明大页、两查询交错 beam、多 seed、按作用域分配 $ef$、循环展开。两查询交错实验中发现共享 visited 数组会被并发搜索覆盖 generation，必须私有化；最终为单查询顺序执行。

## 复现

```bash
bash autoresearch.sh

g++ -O2 -std=c++17 production/nested-attr-vector-search/src/les_analysis.cpp -o /tmp/les_analysis
/tmp/les_analysis --meta production/nested-attr-vector-search/work/fixtures/meta.txt \
  --tree production/nested-attr-vector-search/work/fixtures/tree.txt \
  --labels production/nested-attr-vector-search/work/fixtures/labels.bin \
  --index-dir production/nested-attr-vector-search/work/index
```

代码：`production/nested-attr-vector-search/src/{prep.cpp,baseline.cpp,les_analysis.cpp,harness.py}`；协议：[../benchmark-protocol.md](../benchmark-protocol.md)。
