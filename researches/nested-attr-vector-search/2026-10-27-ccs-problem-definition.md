# CCS 层级属性向量检索：问题定义（形式化）

日期：2026-10-27。背景：ACM CCS 2012（DAG，1,928 概念、6 层、8.2% 多父）与 SciHTC（186,160 篇真实标注论文）。本定义集面向论文写作，可被正文直接引用；实测事实见 [`2026-10-27-ccs-background-and-data.md`](2026-10-27-ccs-background-and-data.md)。

**定义 1（向量）.** 向量 $\mathbf{v}=(v_1,v_2,\dots,v_d)$ 是实数的元组，其中 $v_i\in\mathbb{R}$，$d\in\mathbb{N}^{+}$ 为向量的维度。

**定义 2（对象与对象集）.** 对象 $o=(\mathbf{v}_o,\sigma(o))$ 由一个 $d$ 维向量 $\mathbf{v}_o$ 与直接概念集 $\sigma(o)\subseteq\mathcal{V}$ 组成；对象集 $\mathcal{O}=\{o_1,o_2,\dots,o_n\}$ 包含 $n\in\mathbb{N}^{+}$ 个对象。由直接概念集派生**标签集** $L_o=\bigcup_{u\in\sigma(o)}\mathrm{Anc}(u)$（用于标签编码，不用于定义非递归语义）。在 SciHTC 中每篇论文只有一个最深概念 $u_o$，故 $|\sigma(o)|=1$、$|L_o|$ 的实测均值为 $2.82$（$=\mathrm{Anc}$ 大小）。

**定义 3（分类体系）.** 分类体系 $\mathcal{C}=(\mathcal{V},\mathcal{E})$ 是一个有向无环图（DAG），其中 $\mathcal{V}$ 为概念（目录节点）集合，$\mathcal{E}\subseteq\mathcal{V}\times\mathcal{V}$ 为"父 $\to$ 子"边，$\rho\in\mathcal{V}$ 为根。对 $c\in\mathcal{V}$，定义其**祖先集合**与**后代集合**（自反闭包）：

$$
\mathrm{Anc}(c)=\{c\}\cup\{a \mid a \rightsquigarrow c\},\qquad
\mathrm{Desc}(c)=\{c\}\cup\{b \mid c \rightsquigarrow b\},
$$

其中 $a\rightsquigarrow b$ 表示从 $a$ 到 $b$ 存在有向路径。CCS 2012 实测 $|\mathcal{V}|=1{,}928$，最大深度 $6$，扇出中位数 $4$；$159$ 个概念满足 $|\mathrm{Par}(c)|>1$（多父），因此 $\mathrm{Anc}$ 与 $\mathrm{Desc}$ 必须按可达闭包计算，不能由单亲指针递推。

**定义 4（作用域绑定）.** 绑定函数 $\sigma:\mathcal{O}\to 2^{\mathcal{V}}$ 给出对象**直接**所属的概念集；CCS 标注中通常 $|\sigma(o)|=1$（作者指定的最深概念 $u_o$）。多标签对象允许 $|\sigma(o)|>1$，此时各直接概念并列，不互相派生。

**定义 5（递归与非递归作用域）.** 给定概念 $c\in\mathcal{V}$，两类作用域定义为

$$
S_{\mathrm{rec}}(c)=\{o\in\mathcal{O}\mid \sigma(o)\cap \mathrm{Desc}(c)\neq\varnothing\},\qquad
S_{\mathrm{nrec}}(c)=\{o\in\mathcal{O}\mid c\in \sigma(o)\}.
$$

单绑定时等价于 $o\in S_{\mathrm{rec}}(c)\iff u_o\in\mathrm{Desc}(c)$，$o\in S_{\mathrm{nrec}}(c)\iff u_o=c$。递归作用域包含 $c$ 自身直接绑定的对象（因 $c\in\mathrm{Desc}(c)$）；非递归作用域只看**直接**绑定，不看派生标签 $L_o$。多父概念 $c$ 可能经不同路径进入同一作用域，集合运算须去重。

**例子 1.** 设 CCS 片段为 `Information systems → Information retrieval → Retrieval models and ranking`，且 `Retrieval models and ranking` 另有父 `Computing methodologies → Machine learning`（多父情形）。对象 $o$ 绑定最细概念 $u_o=$ `Retrieval models and ranking`，则 $L_o$ 含 4 个概念。于是 $o\in S_{\mathrm{rec}}$(`Information systems`)、$S_{\mathrm{rec}}$(`Information retrieval`)、$S_{\mathrm{rec}}$(`Computing methodologies`)、$S_{\mathrm{rec}}$(`Machine learning`)；但 $o\notin S_{\mathrm{nrec}}$(`Information retrieval`)（其直接标签不是该概念），且 $o\in S_{\mathrm{nrec}}$(`Retrieval models and ranking`)。若另一对象绑定在 `Information retrieval` 本身，则它属于 $S_{\mathrm{rec}}$(`Information systems`)、$S_{\mathrm{rec}}$(`Information retrieval`) 与 $S_{\mathrm{nrec}}$(`Information retrieval`)，但不属于 $S_{\mathrm{rec}}$(`Retrieval models and ranking`)。

**定义 6（精确目录范围 top-$k$ 检索）.** 给定对象集 $\mathcal{O}$、查询向量 $\mathbf{q}\in\mathbb{R}^d$、概念 $c\in\mathcal{V}$、模式 $m\in\{\mathrm{rec},\mathrm{nrec}\}$ 与 $k\in\mathbb{N}^{+}$，精确检索返回

$$
R=\{\,o_1^{\star},o_2^{\star},\dots,o_k^{\star}\,\}
=\mathop{\arg\min}_{o\in S_m(c)}{}^{k}\;\mathrm{dist}(\mathbf{q},\mathbf{v}_o),
$$

其中 $\mathrm{dist}(\mathbf{q},\mathbf{v})=\lVert\mathbf{q}-\mathbf{v}\rVert_2$，$\arg\min^{k}$ 取距离最小的 $k$ 个对象，$o_k^{\star}$ 为第 $k$ 近对象。精确计算需对 $S_m(c)$ 中每个对象计算一次距离，复杂度为 $O(|S_m(c)|\cdot d)$，另加作用域解析 $O(|\mathrm{Desc}(c)|)$（DAG 遍历）；当 $|S_m(c)|$ 与 $d$ 较大时不可接受。

**定义 7（近似目录范围 top-$k$ 检索）.** 给定查询 $(\mathbf{q},c,m,k)$ 与失败概率 $\delta\in[0,1]$，近似检索的任务是返回结果 $R$，使得其与精确结果 $R^{\star}$ 的 $k$ 近邻重合度满足 $\Pr[\,|R\cap R^{\star}|\ge \rho k\,]\ge 1-\delta$，其中 $\rho\in[0,1]$ 为目标召回率（$\rho=1$ 即精确）。实际系统以召回率 $\mathrm{recall@}k=|R\cap R^{\star}|/k$ 与端到端延迟为权衡轴。

**定义 8（目录维护：MOVE 与 MERGE）.** 目录维护操作改变概念拓扑：$\mathrm{MOVE}(u,w)$ 将 $u$ 的整棵子图重挂到 $w$ 下；$\mathrm{MERGE}(u,w)$ 将 $u$ 并入已存在的 $w$，冲突子概念递归调和。二者须满足**作用域语义约束**：

- **MOVE** 不改变任何对象的直接绑定，因而 $\forall c:\ S_{\mathrm{nrec}}(c)$ 不变；仅当 $c$ 位于 $u$ 的旧祖先链或新祖先链上时 $S_{\mathrm{rec}}(c)$ 才变化（旧链移除 $u$ 子图的成员、新链加入）。
- **MERGE** 将 $u$ 的直接绑定对象迁移到 $w$：$S_{\mathrm{nrec}}(u)$ 清空、$S_{\mathrm{nrec}}(w)$ 增加，$S_{\mathrm{rec}}$ 沿两条祖先链相应变化；重叠子概念的成员集取并集。

在此约束下比较各实现的维护代价，并区分"只改目录结构"（$O(|\mathrm{Desc}(u)|)$ 量级）与"必须改写对象侧标签"（$O(|S_{\mathrm{rec}}(u)|\cdot|\mathrm{Anc}|)$ 量级）两类成本。

**定义 9（问题的四维目标）.** 记作用域解析代价为 $T_{\mathrm{dir}}$、检索代价为 $T_{\mathrm{ann}}$。本文的优化目标是在给定召回率 $\rho$ 下同时约束：

1. **质量**：$\mathrm{recall@}k\ge\rho$（递归与非递归两种模式分别报告，含 scope 内零结果查询计数）；
2. **延迟**：$T_{\mathrm{dir}}+T_{\mathrm{ann}}$ 的均值与尾部（P95/P99）；
3. **资源**：索引构建时间、索引与标签存储（含对象 × 标签数的乘积项）；
4. **可维护性**：MOVE/MERGE 的单次代价与摊销界。

**与相邻问题的边界.** 本问题输出单查询的 $k$ 个对象，不产生对象对，因而不是 vector join；谓词是**拓扑的**（祖先/后代可达），不是平坦标量条件，因而与 ordinary filtered ANN 的 flat predicate 不同——$\mathrm{Desc}(c)$ 的解析本身是可测成本项（目录解析延迟），且必须与 ANN 检索成本分开报告。
