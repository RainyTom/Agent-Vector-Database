# 面向层级知识体系的向量检索：Exact/Subtree 双语义与 Laminar Edge Sharing 索引（研究草案 v4）

状态：研究草案（2026-10-20）。本文以 **ACM CCS** 为主应用背景，形式化“递归子树（Subtree）”与“非递归直属（Exact）”两类查询，提出 Laminar Edge Sharing（LES）索引与两段式 level-0 分桶，给出可行性证明、空间/度数分析与实验设计。

**边界声明（重要）：**
1. Exact 与 Subtree 在直接标签表示下都是 overlap 过滤（单标签 vs 后代标签集），本文不主张“多标签过滤无法表达层级语义”；候选贡献是共享邻接与执行结构，不是表达力。
2. LES v1 只共享 level-0 边；上界层按作用域保留。合并不同作用域的上界层会破坏 HNSW 的分层不变量，本文不声称合并切片与 B4 具有相同访问语义。
3. LES 的查询原型、Exact 工作负载、overlap 过滤基线与真实 CCS 数据尚未实现；当前只有 B1/B2/B4 的 Subtree 合成数据结果与 LES 静态分析，不构成 LES 优于任何基线的证据。
4. “综述直属中间节点”是待验证假设 H1，不是分类事实；Exact 的定义仅为“直接标注”。

---

## 摘要

学术检索、企业知识库与商品目录都把对象组织在层级分类下；对象可挂多个类目，类目成员沿祖先继承。查询有两种自然模式：**Subtree** 检索某节点整棵子树（领域全景），**Exact** 只检索直接标注在该节点的对象（不包含仅由后代继承的对象）。在直接标签表示下两者分别是“与后代标签集相交”和“与单标签相交”的 overlap 过滤，因此问题不在于可表达性，而在于执行：直接标签编码让 Subtree 需要展开后代标签并做多索引合并，祖先闭包编码则让 Exact 退化为大规模差集；过滤图搜索仍需在遍历中判定谓词，而双语义逐作用域物化使索引数量与空间翻倍。

本文提出 **LES**：每条 level-0 边按其在层级上的最小激活节点只存一次，并区分 **直属桶** 与 **跨子树桶**；DFS 先序使子树成为连续区间，查询只做区间切片，热路径不做谓词判定。Exact 查询读取单节点直属段，Subtree 查询读取子树区间，antichain 按节点分别执行后合并。我们不共享上界层——LES v1 使用各作用域自己的上界结构下降到 level-0，再在共享 level-0 并图上搜索，从而避免跨图合并层的非法性问题。

在 SIFT100k 的 85 节点合成层级上，逐作用域物化的 level-0 有 760 万条有向边、83 MB（仅 Subtree）；LES 去重后 366 万条唯一边，两段条目 382 万条（多重度 1.045），仅 level-0 条目与共享向量估算约 33 MB。代价是根作用域平均切片出度从 19.4 升到 36.6。真实 CCS 数据、Exact 工作负载、LSSG/JAG 类基线与 LES 查询评测列为待补实验。

---

## 1 应用背景

### 1.1 为什么以 ACM CCS 为主场景

ACM Computing Classification System（CCS）是计算机领域论文的标准分类体系：深度 4–6 的树状结构，论文按主题挂载 **2–5 个直接标签**。直接标注的位置与论文体裁可能存在相关性，但本文把它作为待验证假设：

> **假设 H1（待验证）.** 综述、框架与立场论文更可能直接标注在中间节点，细分研究论文更可能标注在叶子。

必须强调：**Exact 的严格含义是“数据记录直接标注了该 CCS 概念”，它本身不代表该论文是综述、框架或立场论文。** 不同深度的直属标注数量、标签位置与体裁的关系、空作用域比例，必须在真实数据上统计（§5.4），不能由标签位置反推体裁。

CCS 同时具备层级继承、多标签重叠与粒度混合，且两种查询意图都真实存在：递归检索某领域全貌，或非递归只检索直接标注在该概念上的记录。

### 1.2 两类查询语义及其与 overlap 过滤的关系

**Subtree（递归）.** 查询节点 $u$ 的可行集是 $u$ 及其全部子孙的直接标注对象之并：

$$
\mathcal D_u=\{i\mid L_i\cap\operatorname{Desc}^{+}(u)\neq\varnothing\}.
$$

**Exact（非递归）.** 可行集只包含直接挂载于 $u$ 的对象：

$$
\mathcal B_u=\{i\mid u\in L_i\}.
$$

**两者都是 overlap 过滤。** 保留直接标签 $L_i$ 时：

$$
\text{Exact}(u):\ L_i\cap\{u\}\neq\varnothing,
\qquad
\text{Subtree}(u):\ L_i\cap\operatorname{Desc}^{+}(u)\neq\varnothing.
$$

因此本文**不主张**“多标签过滤无法表达层级语义”。差异在编码与执行：

- **直接标签编码**：Exact 是单标签 overlap，候选可由该标签倒排直接得到；Subtree 需要展开 $\operatorname{Desc}^{+}(u)$，访问子树内全部标签的倒排/索引并合并去重。
- **祖先闭包编码**（每个对象打上全部祖先标签）：Subtree 变成单标签 overlap；Exact 变成“命中 $u$ 再减去后代标签命中”的差集。
- 两种编码各在一侧付出展开、差集、扇出或候选合并成本。已有 overlap-filter ANN（如 [LSSG](https://www.alphaxiv.org/abs/2609.15058)）可直接执行这两种标签查询，但查询期仍需标签/谓词判定，并承担候选与合并开销。

**本文目标.** 用一个共享邻接结构同时服务 Exact 与 Subtree，减少后代展开、逐作用域物化与多路合并；以直接标签 overlap 过滤（LSSG 类）、属性感知图（[JAG](https://www.alphaxiv.org/abs/2602.10258)）、双语义物化与段树共享图为对照，检验其在空间或吞吐上是否更优。

### 1.3 执行成本与现有方案

**（1）直接标签倒排 + Subtree：展开与合并.** 展开 $\operatorname{Desc}^{+}(u)$ 后求并子树内全部标签的倒排/索引，合并候选并按对象 ID 去重；粗粒度子树的扇出、临时内存与启动开销随分支数增长。

**（2）祖先闭包编码 + Exact：差集.** 打上全部祖先标签后，Exact 必须做

$$
\mathcal B_u
=\mathcal D_{\text{tag}}(u)\setminus\Bigl(\bigcup_{v\in\operatorname{Desc}(u)}\mathcal D_{\text{tag}}(v)\Bigr).
$$

CCS 中高层节点的后代可达数十至数百个；求并侧很大而结果可能很小。接到过滤图搜索上时，邻居多数属于被排除的后代，会持续触发谓词失败与死胡同。

**（3）全局图 + 谓词过滤 / 属性感知图.** 包括全局 HNSW + 后过滤，以及 LSSG、ACORN、UNG、JAG 等过滤感知导航。它们仍需在遍历中判定标签/谓词，候选预算与选择率耦合；低选择率子树或 Exact 下仍有候选浪费与无效探索。

**（4）双语义逐作用域物化.** 为每个 $\mathcal D_u$ 与 $\mathcal B_u$ 分别建图，索引数量 $2|V|$，成员总数 $\sum_u(|\mathcal D_u|+|\mathcal B_u|)$，边、向量与元数据按作用域重复；目录调整时重建范围大。

**（5）逐标签索引 + 合并.** 粗粒度 Subtree 访问子树内全部标签索引；祖先闭包编码下 Exact 还要做差集，启动、合并与去重随分支数增长，多标签对象使向量在多个索引重复。

**（6）段树/区间图索引（iRangeGraph 风格）.** 面向数据顺序上的连续区间；子树成员由标签决定、多标签使其不是数据序上的连续区间，需要额外映射；Exact 的“排除后代”也不是区间运算。

这些成本指向同一问题：能否用**一套共享邻接**同时服务两种模式，在保持过滤精确的同时降低空间与执行开销，并给出可验证的分层搜索语义。

### 1.4 设计需求

- **R1 双语义精确.** Exact 不得泄漏后代标签；Subtree 必须包含后代；结果按对象 ID 去重。
- **R2 任意粒度与 antichain.** 根、中间节点、叶、并列类目都要服务。
- **R3 热路径无谓词.** 理想情况下邻接本身即“该作用域可用”。
- **R4 空间低于双语义物化.**
- **R5 构建可承受、只依赖静态数据.**
- **R6 可复现、可度量.** 确定性结果，索引字节与构建时间如实计入。

### 1.5 其他场景

企业知识库 RAG（部门/产品目录子树，块多标签）、商品目录（跨类目商品）与 CCS 同构：层状作用域、继承、多标签去重、粗细粒度并存。CCS 是主场景，其余用于外部有效性讨论。

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

**定义 2（带直接标签的数据集）** $\mathcal D=\{(\mathbf x_i,L_i)\}_{i=1}^{n}$，$\mathbf x_i\in\mathbb R^d$，$\varnothing\neq L_i\subseteq V$，标签可位于任意深度。直接标签是唯一事实来源；继承关系由定义 3 导出。ID 唯一。

**定义 3（直接集、子树集与激活集）**

$$
\mathcal B_u=\{i\mid u\in L_i\},\qquad
\mathcal D_u=\{i\mid L_i\cap\operatorname{Desc}^{+}(u)\neq\varnothing\}=\bigcup_{v\in\operatorname{Desc}^{+}(u)}\mathcal B_v,
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

Exact 只返回直接挂载于 $U$ 中节点的对象；Subtree 返回这些节点子树内的全部对象；均按对象 ID 去重。

**定义 5（精确结果）** 排序键 $s_Q(i)=(\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i)$ 字典序；$k_Q=\min(k,|\mathcal S_m(U)|)$；$R^*(Q)$ 为作用域内按 $s_Q$ 升序的前 $k_Q$ 个不同对象。空作用域返回空。

**定义 6（近似与召回）** $\widehat R(Q)\subseteq\mathcal S_m(U)$、$|\widehat R(Q)|\le k_Q$、无重复；$k_Q>0$ 时 $\operatorname{Recall@k}(Q)=|\widehat R(Q)\cap R^*(Q)|/k_Q$，$k_Q=0$ 时要求返回空且不计入平均召回。近似只允许最近邻遗漏。

**定义 7（交叠度）** $\mu_m(U)=\sum_{u\in U}|\mathcal S_m(u)|/|\mathcal S_m(U)|$。Exact 的直接标签重叠通常较少，Subtree 可显著大于 1。

**定义 8（索引与代价）** 索引 $I$ 由 $\mathcal D,T$ 构建，构建期不得读取测试查询或真值。$M(I)$ 为常驻字节：向量（每对象一份）、邻接条目、激活标记、桶偏移、树/标签、每节点入口与上界层；共享数据只计一次。$T_{\text{build}}$ 为构图、边归属、排序与落盘时间。$T_{I,\pi}(Q)$ 从接收查询到结果，包含模式解析、候选生成、搜索、距离、去重与合并；LES 的模式解析为 $O(1)$（选择区间端点）。峰值内存另计。

**定义 9（研究问题）** 给定 $\mathcal D,T,\delta$、查询分布 $\mathcal P$（含两种模式与 antichain）、召回目标 $\rho$、内存预算 $B$，

$$
\min_{I,\pi}\ \mathbb E_{Q\sim\mathcal P}[T_{I,\pi}(Q)]
\quad\text{s.t.}\quad
M(I)\le B,\quad
\mathbb E_{\mathcal P\mid k_Q>0}[\operatorname{Recall@k}(Q)]\ge\rho,
$$

且每个查询返回 $\widehat R(Q)\subseteq\mathcal S_m(U)$、无重复、数量为 $\min(k,|\mathcal S_m(U)|)$（不足时返回全部）。

### 2.3 双语义的具体差异

设类目 $u=$ *Information retrieval*，子节点 $v=$ *Search engine architectures*。论文 $p_1$ 直接挂 $u$，$p_2$ 直接挂 $v$，$p_3$ 直接挂 $u$ 与 *Machine learning*。则 $\mathcal B_u=\{p_1,p_3\}$，$\mathcal D_u=\{p_1,p_2,p_3\}$。Exact$(u)$ 必须排除 $p_2$（即使向量更近）；Subtree$(u)$ 包含三者。注意：这里没有任何“无法用 overlap 表达”的语义——Exact 是标签 $\{u\}$ 的 overlap，Subtree 是标签集 $\operatorname{Desc}^{+}(u)$ 的 overlap；差异在于直接标签编码下后者的展开成本，以及祖先闭包编码下前者的差集成本。

### 2.4 边界情形

- **Exact 作用域为空**：叶子常无直属记录；Exact 必须返回空，不能回退到 Subtree。
- **Subtree 非空但直属为空**：Subtree 仍要工作。
- **不足 $k$**：返回全部；召回分母为作用域大小。beam search 不保证此性质，必须有作用域计数与精确扫描回退（§3.8）。
- **多标签重复**：同一对象通过多个标签命中时只返回一次。
- **内部节点标签**：直属中间节点不等于属于任一特定后代；这正是两种模式的差别。
- **距离并列**：按 ID 升序。
- **模式混合**：默认整查询统一模式；混合模式作为扩展。

### 2.5 指标

- `qps_at_r95`：平均 Recall@10 ≥ 0.95 且违规为 0 时的吞吐；否则为 0。
- Recall@10、空结果正确率、p95、索引字节、构建时间、违规数。
- **分模式与分工作负载报告**（$Q_{\text{leaf}},Q_{\text{subtree}},Q_{\text{exact}},Q_{\text{antichain}}$），并计入后代展开、候选合并、去重与临时内存。

### 2.6 假设

静态层级与向量；单机内存、单线程；构建期 $T$ 与 $L_i$ 已知、查询不可见；距离平方 L2（SIFT 整数值用 u8 精确编码）；不处理更新、删除、并发、权限与多版本。

---

## 3 方案：Laminar Edge Sharing（LES）

### 3.1 概览

LES 用一套按“最小激活节点”去重的 **level-0** 边同时服务 Exact 与 Subtree：

1. Exact 的激活位置是共同直接标签 $L_i\cap L_j$；Subtree 的激活位置是共同可行祖先的极小元 $\min(A_i\cap A_j)$。
2. 每个对象维护按 DFS 先序排序的 level-0 桶表；同一节点 $u$ 的桶内，**直属条目在前、跨子树条目在后**。
3. 查询只做区间切片：Exact$(u)$ 取 $u$ 的直属段；Subtree$(u)$ 取 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 的整段。
4. **上界层不共享**：LES v1 保留每个作用域自己的上界层与入口，用于下降到 level-0；只共享 level-0 边。这样分层不变量只依赖一个已构建好的作用域上界结构。

### 3.2 激活集与两种边激活

- **Subtree 激活**：$M^{\text{sub}}_{ij}=A_i\cap A_j$，向上封闭，极小元 $\min(M^{\text{sub}}_{ij})$（§3.3）。
- **Exact 激活**：$M^{\text{exact}}_{ij}=L_i\cap L_j$，两端都被直接挂载的共同节点。

*CCS 例子.* $p_1$ 直属 $u$，$p_2$ 直属 $v\in\operatorname{Desc}^{+}(u)$：$M^{\text{exact}}_{12}=\varnothing$（不进入 Exact$(u)$），$M^{\text{sub}}_{12}=\operatorname{Anc}^{+}(u)$，极小元 $\{u\}$。$p_1$ 直属 $u$，$p_3$ 直属 $u$ 与 ML：$M^{\text{exact}}=\{u\}$，$M^{\text{sub}}=\{u,\text{ML}\}$，极小元 $\{u,\text{ML}\}$。

### 3.3 最小激活

**定义 10** $\min(M)=\{u\in M\mid \nexists v\in M,\ v\in\operatorname{Desc}^{+}(u),\ v\neq u\}$，即 $M$ 中没有后代也在 $M$ 的节点；它是 antichain。

- Subtree 的最小激活规模不超过 $|L_i|\cdot|L_j|$（本文 ≤9，通常为 1）。
- Exact 的激活就是 $L_i\cap L_j$；不需要闭包。

### 3.4 两段式分桶（level-0）

每个对象的 level-0 桶表按 $\mathrm{tin}$ 升序排列；同一节点 $u$ 的条目分为连续两段：

| 段 | 内容 | 服务的查询 |
|---|---|---|
| $B_{\text{exact}}(u)$ | 两端都直接挂载于 $u$ 的边（$u\in L_i\cap L_j$） | Exact$(u)$ |
| $B_{\text{cross}}(u)$ | Subtree 最小激活为 $u$、且该边不在 $B_{\text{exact}}(u)$ 的边 | Subtree$(u)$ 及祖先 |

**归属规则：**

1. 从各作用域图收集 level-0 有向边；同一条 $(i,j)$ 只处理一次。
2. Exact 段：对每个 $u\in L_i\cap L_j$，在对象 $i$ 的 $B_{\text{exact}}(u)$ 追加 $j$。
3. Subtree 段：对每个 $m\in\min(A_i\cap A_j)$，若 $m\notin L_i\cap L_j$，在 $B_{\text{cross}}(m)$ 追加 $j$；若 $m\in L_i\cap L_j$，直属段已覆盖 Subtree 的切片，不再重复。
4. 每个桶内按邻居 ID 去重；对象桶表按 $(\mathrm{tin},\text{段},j)$ 排序，记录每节点直属段起止。

**为什么直属段覆盖 Subtree.** 若边在 $u$ 直属，则 $u\in A_i\cap A_j$，且 $u$ 的子树切片包含 $u$ 的直属段。若该边还有更深的 Subtree 最小激活 $m\in\operatorname{Desc}^{+}(u)$，则在 $B_{\text{cross}}(m)$ 另存一份。仅由后代继承而来的边不会进入 $B_{\text{exact}}(u)$。

### 3.5 切片代数

DFS 先序记录 $\mathrm{tin}(u),\mathrm{tout}(u)$：

$$
\mathrm{Slice}_{\text{exact}}(i,u)=B_{\text{exact}}(i,u),
\qquad
\mathrm{Slice}_{\text{subtree}}(i,u)=\bigcup_{v\in\operatorname{subtree}(u)}\bigl(B_{\text{exact}}(i,v)\cup B_{\text{cross}}(i,v)\bigr),
$$

后者是对象 $i$ 的桶表中 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 的连续片段（直属段与跨子树段都按节点先序，且同一节点内直属段在前）。

- **Exact$(u)$**：只扫描 $u$ 的直属段；
- **Subtree$(u)$**：扫描整个子树区间；
- **Antichain**：对每个节点按模式取切片，独立搜索后按 ID 去重合并。

热路径不读取标签、不执行差集。

### 3.6 存储布局

**共享部分（level 0）：**

```
对象 i 的 level-0 桶表（按 tin 升序）：
  [ u 直属段 | u 跨子树段 ][ v 直属段 | v 跨子树段 ] ...
全局条目数组 E0：所有对象按对象顺序拼接
对象偏移 O0[n+1]；每节点直属段偏移 D0[m]（start, end）
条目编码：邻居 ID 4 B + 激活先序 1–2 B
```

**每作用域部分（上界层）：** 各作用域自己的 HNSW 上界邻接与层高、入口；规模约为 level-0 的 $1/M$，按作用域计。

另存共享向量（$n\times d$ 字节；u8 时 12.8 MB）、树与标签。空间

$$
M_{\text{LES}}\approx nd
+\sum_{\ell=0}E_{\text{bucket},\ell}\cdot(4+\lceil\log_2 m\rceil/8)
+4n+\text{upper levels}+O(n+m),
$$

其中 $\ell=0$ 为共享桶，$\ell\ge1$ 按作用域计。

### 3.7 构建步骤

1. DFS 先序编号，得到 $\mathrm{tin}/\mathrm{tout}$ 与父子关系。
2. 每个对象计算 $A_i$ 与直接标签掩码。
3. 对每个节点构建作用域图：Subtree 图 $G^{\text{sub}}_u$（成员 $\mathcal D_u$）与 Exact 图 $G^{\text{exact}}_u$（成员 $\mathcal B_u$，仅当 $|\mathcal B_u|$ 足够大时构建）；记录上界层与入口。本文用 HNSW（$M=24$、$efC=200$、level-0 截断到最近 24）。
4. 遍历所有图的 level-0 边，按 $(i,j)$ 去重，计算两种激活集，按 §3.4 生成桶条目。
5. 每个对象按 $(\mathrm{tin},\text{段},j)$ 排序、去重；计算全局条目数组、对象偏移与直属段偏移。
6. 写共享向量、level-0 条目与偏移、每作用域上界层/入口、树与标签。

步骤 4 与构图解耦，便于与 B3/B4/B5 使用同一套归属逻辑做公平对照。

### 3.8 查询算法

单节点查询 `Search(u, q, mode, k, ef)`：

```
scope_size = (mode == exact ? |B_u| : |D_u|)      # 预计算
if scope_size == 0: return empty
if scope_size <= k: return ExactScan(scope)        # 保证返回全部

if mode == exact:
    lo ← D0[u].start; hi ← D0[u].end               # u 的直属段
    entry ← upper_entry_exact[u]
else:
    lo ← tin(u);  hi ← tout(u)                     # 子树区间
    entry ← upper_entry_sub[u]

ep ← entry; d ← δ(q, x_ep)
for ℓ = h(u) down to 1:                            # 作用域自己的上界层
    changed ← true
    while changed:
        changed ← false
        for j in UpperNeighbors(u, ep, ℓ):         # 不共享
            dj ← δ(q, x_j)
            if (dj, j) < (d, ep): ep ← j; d ← dj; changed ← true

visited ← {ep}; top ← max-heap{(d, ep)}; cand ← min-heap{(d, ep)}
while cand ≠ ∅:
    (dc, c) ← pop-min(cand)
    if |top| = ef and dc > max(top): break
    for j in Slice(c, lo, hi, 0):                  # 共享 level-0 切片
        if j ∈ visited: continue
        visited ← visited ∪ {j}
        dj ← δ(q, x_j)
        if |top| < ef or dj < max(top):
            push(cand, (dj, j)); push(top, (dj, j))
            if |top| > ef: pop-max(top)
return k smallest (δ, id) in top
```

- `Slice(c,lo,hi,0)` 在对象 $c$ 的 level-0 桶表二分区间并返回邻居；同一邻居可能因多个激活出现多次，`visited` 去重。
- **小作用域回退**：当作用域大小 $\le k$ 时直接枚举并精确扫描。这把“返回全部可行对象”的正确性要求与 beam search 解耦；更大的回退阈值可按实测选择。
- `ExactScan` 用直接标签倒排/位图枚举 $\mathcal B_u$；`SubtreeScan` 枚举子树内标签的并。
- antichain：逐节点执行后合并去重。不能把不同节点的切片并成一次搜索：$i$ 可能只在 $u_1$ 可行、$j$ 只在 $u_2$ 可行，二者都在并集中，但边 $(i,j)$ 的激活集不含 $u_1,u_2$（Exact 尤其明显）。

### 3.9 分层语义与正确性

**为什么不能直接共享上界层.** B4 的每个作用域图有自己的层高、入口与上界邻接。把不同作用域的边合并后按端点激活筛选，只能保证“边属于该作用域”，不能保证它在查询作用域的 HNSW 中处于同一层，也不能保证入口合法。若不做处理，上层搜索的不变量会被破坏。本文明确**不采用**“合并 B4 的全部层再切片即等价于 B4”的说法。

**LES v1 的选择.** 只共享 level-0：

- 上界层（$\ell\ge1$）按作用域保留 B4 的邻接、层高与入口（规模小）；
- level-0 是共享的两段桶并图；
- 查询先用作用域自己的上界结构下降到 level-0，再在并图上做 level-0 beam。

这样分层不变量只依赖一个已构建好的作用域上界结构，level-0 的图是明确的新并图。

**引理 1（Exact 切片）** $\mathrm{Slice}_{\text{exact}}(i,u)=\{j\mid u\in L_i\cap L_j\}$（对边全集中存在的边）。

*证明.* （$\subseteq$）条目在 $B_{\text{exact}}(u)$ 生成时要求 $u\in L_i\cap L_j$。（$\supseteq$）若 $u\in L_i\cap L_j$，构建规则 2 追加该边。$\square$

**引理 2（Subtree 切片）** $\mathrm{Slice}_{\text{subtree}}(i,u)=\{j\mid i,j\in\mathcal D_u\}$（对边全集中存在的边）。

*证明.* （$\subseteq$）条目位于某 $v\in\operatorname{subtree}(u)$ 的直属段或跨子树段。直属段要求 $v\in L_i\cap L_j$，故 $u\in A_i\cap A_j$；跨子树段要求 $v\in\min(A_i\cap A_j)$，向上封闭给出 $u\in A_i\cap A_j$。两种情形都有 $i,j\in\mathcal D_u$。（$\supseteq$）若 $i,j\in\mathcal D_u$，则 $u\in A_i\cap A_j$；从 $u$ 下降到仍在交集中的子节点，停在某极小元 $v\in\operatorname{subtree}(u)$。若 $v\in L_i\cap L_j$，边在 $B_{\text{exact}}(v)$；否则在 $B_{\text{cross}}(v)$；两段都在区间内。$\square$

**命题 1（包含性）** 对任意 $u$，LES 的 level-0 切片包含 B4 中 $G_u$ 的全部 level-0 边。

*证明.* $G_u$ 的每条边两端都在 $\mathcal D_u$，其 Subtree 最小激活 $v\in\operatorname{subtree}(u)$，条目落在区间内；Exact 段同理。因此并图的候选集不小于原图，但度数可能更大。$\square$

**定理 1（查询语义，LES v1）** 查询使用作用域自己的上界层与共享 level-0 切片；切片过滤精确（引理 1/2），antichain 合并覆盖并集并按 ID 去重；近似仅来自 level-0 beam 的 $ef$ 截断。该定理**不**声称 LES 与 B4 的访问序列相同；B4 的上界层与 level-0 图都是作用域专用的，LES 的 level-0 是并图。

**开放问题：共享上界层.** 若要把上界层也去重，至少需要以下之一：
- **S1 全局层高**：每个对象在所有作用域使用同一层高，条目记录层号；合并图是一个新的分层图，需证明其不变量与入口高度。
- **S2 作用域层级资格**：条目携带“在哪些作用域中属于第 $\ell$ 层”的资格，查询按 $(u,\ell)$ 过滤；空间换语义。
- **S3 重新定义共享图**：不继承 B4 的层结构，重新设计构建与搜索，并证明连通性/可导航性。

在这些方案完成前，LES 只共享 level-0。

**连通性风险.** 并图包含各作用域原图，level-0 连通性不弱于原图；但 Exact 图只对 $|\mathcal B_u|$ 达到阈值的节点构建，小 Exact 作用域依赖精确扫描回退，不依赖图。

### 3.10 复杂度与空间

- **构建**：构图 $O(\sum_u n_u\log n_u)$；边归属 $O(E_{\text{obs}}\cdot(|L|+m/64))$；排序 $O(\sum_i d_i\log d_i)$。Exact 图只对 $|\mathcal B_u|\ge\tau$ 的节点构建，额外 level-0 边数 $\le\sum_{|\mathcal B_u|\ge\tau}|\mathcal B_u|\cdot M$。
- **空间**：$E_{\text{bucket}}=\sum_e(|L_i\cap L_j|+|\{m\in\min(A_i\cap A_j):m\notin L_i\cap L_j\}|)$ 条 level-0 条目；向量每对象一份；上界层按作用域计。
- **查询**：每次弹出 $O(\log d_i+\deg_{u,m}(i))$；Exact 度数通常远小于 Subtree；Subtree 上界 $\deg_u(i)\le\sum_{v\in\operatorname{subtree}(u)}\deg_v(i)$。

### 3.11 与基线的结构对比

| 维度 | 直接标签 overlap / 全局过滤 | 逐标签 B3 | 双语义物化 B4 | LES v1 |
|---|---|---|---|---|
| Exact 语义 | 单标签 overlap（直接编码）或差集（祖先编码） | 差集 + 多索引 | 独立 Exact 图 | 直属段切片 |
| Subtree 语义 | 后代标签展开 + 过滤 | 多索引合并 | 独立子树图 | 子树区间切片 |
| 查询期谓词 | 有 | 有 | 无 | 无（level-0） |
| 上界层 | 全局图 | 各索引 | 每作用域 | 每作用域（不共享） |
| 边重复 | 无 | 跨标签重复 | 每作用域重复 | level-0 按激活去重 |
| 向量重复 | 1× | 每标签 1× | 每作用域 1× | 1× |

### 3.12 参数

- 边来源：level-0 边来自 HNSW（$M=24$、$efC=200$、截断 24）；可替换。
- Exact 图的构建阈值 $\tau$（例如 $2k$）；更小的作用域用精确扫描。
- $ef$ 按模式与召回曲线分别选择；LES 的度数更高，不能沿用 B4 的 $ef=21$。
- u8 距离用于 SIFT；通用数据可存 float。
- 大树的激活集用有序祖先表/压缩位图；激活先序用 2–4 B。

---

## 4 基线

| 编号 | 基线 | 关键点 |
|---|---|---|
| B0 | 直接标签倒排 + 精确扫描 | Exact 单标签倒排；Subtree 后代标签并；召回 1.0，计展开/合并/临时内存 |
| B1 | 直接标签倒排 + 全局 ANN 过滤 | 候选生成后按标签过滤；低选择率下候选浪费 |
| B2 | overlap-filter ANN（LSSG 类） | 直接执行标签交查询；查询期仍需谓词判定，作为主要强对照 |
| B2' | 属性感知图（JAG 类） | 多属性共享导航；过滤感知对照 |
| B3 | 逐标签 HNSW + 合并/差集 | 直接利用标签分区；Exact 在祖先编码下需差集 |
| B4 | 双语义逐作用域物化 | 每个 $\mathcal D_u$ 与 $\mathcal B_u$ 独立建图；无过滤，最强无谓词对照 |
| B5 | 段树共享图（iRangeGraph 风格） | 最接近的共享机制，界定 LES 的增量 |

当前实现只有 B0 的扫描路径、B1/B2 的简化版本（全局 HNSW + 过滤/小范围扫描）与 B4 的 Subtree 版本；B2 的 LSSG 复现、B2'、B3、B5 与 B4 的 Exact 版本尚未实现。

## 5 实验设计

### 5.1 数据

**目标数据（首选）.** SciHTC / arXiv / Semantic Scholar 的论文向量（标题+摘要嵌入）与 ACM CCS 直接标签。保留真实长尾：不同深度的直属标注、多标签分布、空 Exact 作用域。层级与标签来自真实记录，不人工合成。

**当前基准.** 两个工作负载：
1. 原始 SIFT100k + 85 节点层级（每对象 1–3 个直接标签），用于与既有工程结果对比；
2. **CCS 形状工作负载**（`prep_ccs.py`）：1365 节点（深度 5、4 叉、1024 叶），每对象 1–5 个直接标签（均值 2.1），25% 的主标签落在中间节点，叶类目按 Zipf(1.1) 偏斜并与 512 个向量中心相关；含 1000 条 Subtree 与 1000 条 Exact 查询及各自 oracle。标签流行度按 PathSeer 数据集的真实元数据校准（Paper 741 类、Zipf 斜率约 -2.4；SIFT1M 为 10/12/100 类、单标签），但树形状与中间节点直属比例仍是显式假设。详见[基线改造研究](baseline-adaptation.md)。

### 5.2 工作负载

| 类型 | 定义 | 目的 |
|---|---|---|
| $Q_{\text{leaf}}$ | 叶节点 Subtree（叶无后代，等价 Exact） | 垂直领域检索 |
| $Q_{\text{subtree}}$ | 粗/中节点 Subtree | 领域全景；后代展开与合并 |
| $Q_{\text{exact}}$ | 中高层节点 Exact | 直属检索；差集/谓词失败；空与小作用域 |
| $Q_{\text{antichain}}$ | 2–3 节点并，Subtree 与 Exact 各半 | 跨领域组合与去重 |

每类按作用域大小分层采样；$k=10$；$Q_{\text{exact}}$ 单独报告空作用域、$|\mathcal B_u|<k$ 与正常作用域的比例和延迟。避免只报加权平均。

### 5.3 协议与指标

`qps_at_r95`、分类型 Recall@10、空结果正确率、p95、索引字节、构建时间、违规数；索引空间按 level-0 直属段/跨子树段/上界层/向量/偏移分解。所有方法计入后代标签展开、候选合并、去重与临时内存。详见[基准协议](benchmark-protocol.md)。

### 5.4 需在真实数据上核验的统计

- 不同深度的**直属标注数量**与分布（检验 H1，但不把标签位置当作体裁）；
- 论文体裁（综述/框架/立场/研究）与标注深度的关系（需要外部体裁标签，不能用 CCS 位置替代）；
- 空 Exact 作用域比例、$|\mathcal B_u|<k$ 比例；
- 多标签交叠度 $\mu_{\text{exact}}$ 与 $\mu_{\text{subtree}}$；
- 子树大小与分支扇出分布。

## 6 当前结果

### 6.1 基线（合成数据，Subtree 工作负载，已测）

| 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|
| B0 精确扫描（Subtree） | ≈1,400 | 1.0 | ms 级 | 64 MB | 0 |
| B1/B2 简化的全局 HNSW + 过滤/扫描 | 3,104 | 0.9987 | 0.83 ms | 64 MB | 16 s |
| B4 逐作用域物化（仅 Subtree） | **94,698** | 0.9570 | 20.9 µs | 83 MB | 30.8 s |

上表**只覆盖 Subtree 合成工作负载**；Exact 模式、LSSG/JAG 对照与 B4 的双语义版本尚未实现，不能据此比较双语义优劣。

### 6.2 LES 两段式 level-0 分析（已测）

边来自当前 85 个 Subtree 图的 level-0（每作用域截断到最近 24）：

| 指标 | 数值 |
|---|---:|
| 观测有向边（各作用域之和） | 7,602,554 |
| 唯一有向边 | 3,658,543 |
| 重复比例 | 51.9% |
| 直属段条目 $B_{\text{exact}}$ | 1,966,214 |
| 跨子树段条目 $B_{\text{cross}}$ | 1,856,024 |
| 两段合计 | 3,822,238（多重度 1.045） |
| 直属成员数 $\sum_u|\mathcal B_u|$ | 115,847（80 节点非空，最大 5,894） |
| 每作用域平均 level-0 出度 | 13.0–20.9 |
| 根 Subtree 切片平均出度 | 36.59（1.9×） |
| 深度 1 切片平均出度 | 25.5–31.8（1.3–1.6×） |

空间估算（**仅 level-0 条目 + 共享向量**）：条目约 $3.82\text{M}\times5\text{B}=19.1$ MB，向量 12.8 MB，偏移与元数据约 1.5 MB，合计约 **33 MB**。这不包括上界层、Exact 专属图与树/标签之外的运行时结构，也**不是**与“双语义 B4”可比的完整空间；当前 B4 的 83 MB 只是 Subtree 版本。

### 6.3 CCS 形状工作负载（2026-10-20）

同机、单线程、u8 精确距离、ef=21：

| 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|
| B4 逐作用域物化，Subtree | **74,706** | 0.9641 | 28.4 µs | 194 MB | 57 s |
| B0 直接标签倒排 + 精确扫描，Exact | **191,914** | 1.0000 | 11.3 µs | 向量 51 MB | 0 |

Exact 作用域 p50/p95/max 为 126/396/6,973，扫描已经非常快：**为 Exact 物化图在此 workload 上没有收益**。这支持“小作用域精确回退”，也说明 LES 的直属桶必须先与扫描比较，而不是与图索引比较。Subtree 作用域 p50/p95/max 为 10,334/49,305/86,699，图搜索仍有明显价值。详细统计与基线改造计划见[基线改造研究](baseline-adaptation.md)。

### 6.4 Filtered-HNSW / ACORN 对照（同 1000 条 Subtree 查询）

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|---:|
| B0 Subtree 精确扫描 | – | 3,008 | 1.0000 | 1.16 ms | 51 MB | 0 |
| B4 逐作用域物化 | 21 | **74,706** | 0.9641 | 28 µs | 194 MB | 57 s（单线程） |
| Filtered-HNSW（FAISS HNSW + 位图 selector） | 384 | **1,282** | **0.9524** | 1.07 ms | 75 MB | 4.8 s（多线程） |
| Filtered-HNSW | 512 | 991 | 0.9642 | 1.53 ms | 75 MB | 5.0 s |
| ACORN（占位 metadata，filter-agnostic） | 1024 | 394 | 0.8479 | 3.85 ms | 70 MB | 0.8 s |
| ACORN（主标签 metadata，单标签编码） | 512 | 620 | 0.8395 | 2.48 ms | 70 MB | 0.9 s |

同召回下 B4 约为 Filtered-HNSW 的 75×、精确扫描的 25×，代价是 2.6× 索引字节。ACORN 的位图改造版未达到 0.95 召回；它们没有使用层级感知构图，不能作为完整 ACORN 复现。位图预计算成本 7–8 µs/query。对照仍缺：同内存预算的 B4 变体、单线程 FAISS 构建、完整多标签 ACORN 编码。

### 6.5 待补实验（论文成立的前提）

1. 实现 LES 构建（两段分桶、对象排序、直属段偏移）与 Exact/Subtree 查询原型（含小作用域回退）；
2. 生成 $Q_{\text{exact}}$ 与四类工作负载，加入精确 oracle、空作用域规则与违规检查；
3. 实现 B2（LSSG 类 overlap 过滤）、B2'（JAG 类）、B3、B5 与 B4 的双语义版本；
4. 在真实 CCS/SciHTC 数据上统计 §5.4 并重跑 §5；
5. 1M 规模复核空间、构建与吞吐。

## 7 效度威胁与局限

- **分层语义未完全解决.** LES v1 只共享 level-0；共享上界层需要 S1/S2/S3 之一并重新证明不变量。当前不能声称与 B4 同访问语义。
- **双语义未完成评测.** Exact 工作负载、空/小作用域回退、两段式查询与 LES 原型均未实现；33 MB 只是 level-0 条目估算，不能与双语义 B4 直接比较。
- **假设 H1 未验证.** 标签位置与论文体裁的关系未知；Exact 的定义仅为直接标注，不能反推综述。
- **新颖性未确认.** LES 与 iRangeGraph 的段树单元图、UNG 的标签导航、LSSG 的 overlap 过滤、JAG 的属性感知图在目标上相近；需逐机制对照后才能判断“两段桶 + 区间切片 + level-0 共享”是否有独立贡献。
- **规模与更新.** 仅 100k、静态、单线程；1M 与目录调整的重建成本未测。
- **距离.** 平方 L2；其他度量需重新审视编码与几何性质。

## 8 相关工作（简）

过滤 ANN：Filtered-DiskANN、ACORN、UNG、Curator、LSSG；范围/嵌套索引：iRangeGraph；多属性共享图：JAG。本文的候选差异是“按最小激活节点去重的 level-0 两段桶 + 先序区间切片”，且必须与直接标签 overlap 基线在同一协议下比较。

---

## 附录 A：强基线 B4 的实现与消融摘要

B4 每作用域独立构图（hnswlib），查询期自定义紧凑格式：u8 精确距离（SIFT 整数值，块 656→226 B）、向量前置布局、level-0 最近 24 条（226→178 B，索引 108→83 MB）、作用域分组执行、两处预取。参数 $M=24$、$efC=200$、$ef=21$。以上只针对 Subtree 合成工作负载。

消融（交错测量）：分组执行约 +35%；u8 约 +38%；PC1 约 +5%；紧凑块+预取至 86,788 qps；截断与 $ef=21$ 至 94,698 qps。负结果：共享向量、变量长度邻接、小范围扫描、透明大页、两查询交错 beam、多 seed、按作用域分配 $ef$、循环展开。两查询交错实验中发现共享 visited 数组会被并发搜索覆盖 generation，必须私有化。

## 每日进展

按日期的实验记录、数据与构建日志见 [2026-10-20-exps/](2026-10-20-exps/)。

## 复现

```bash
bash autoresearch.sh

g++ -O2 -std=c++17 production/nested-attr-vector-search/src/les_analysis.cpp -o /tmp/les_analysis
/tmp/les_analysis --meta production/nested-attr-vector-search/work/fixtures/meta.txt \
  --tree production/nested-attr-vector-search/work/fixtures/tree.txt \
  --labels production/nested-attr-vector-search/work/fixtures/labels.bin \
  --index-dir production/nested-attr-vector-search/work/index
```

代码：`production/nested-attr-vector-search/src/{prep.cpp,baseline.cpp,les_analysis.cpp,harness.py}`；协议：[benchmark-protocol.md](benchmark-protocol.md)。
