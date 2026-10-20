# 层级属性向量检索的 Laminar Edge Sharing 索引（研究草案）

状态：研究草案（2026-10-20），取代此前的工程记录。本文给出问题定义、研究假设、提出的索引结构与搜索算法、强基线及初步测量。LES（Laminar Edge Sharing）目前完成了空间/度数分析，端到端搜索原型尚未评测；除注明外，不主张新颖性成立。此前的“每节点物化图”实现降级为强基线 B4，其工程细节只作为基线记录。

## 1. 问题定义

### 1.1 层级与数据

**定义 1（属性层级）** 有限有根树 $T=(V,E,r)$，$|V|=m$，节点代表类别，子节点是父类别的细分。$\operatorname{Desc}^{+}(u)=\{v\mid v=u\ \lor\ u\leadsto v\}$，$\operatorname{Anc}^{+}(u)$ 为含自身的祖先集。

**定义 2（带层级标签的数据集）** $\mathcal D=\{o_i=(\mathbf x_i,L_i)\}_{i=1}^{n}$，$\mathbf x_i\in\mathbb R^d$，$L_i\subseteq V$ 为直接标签集（对象可多标签，标签可在任意深度）。类别成员沿祖先继承：对象属于 $u$ 当且仅当其某个直接标签位于 $\operatorname{Desc}^{+}(u)$。ID 唯一，重复向量仍是不同对象。

**定义 3（激活集与可行集）** 对象 $i$ 的激活集 $A_i=\bigcup_{v\in L_i}\operatorname{Anc}^{+}(v)$，是向上封闭的节点集合。查询节点 $u$ 的可行集

$$
\mathcal D_u=\{o_i\in\mathcal D\mid u\in A_i\}.
$$

向上封闭性给出核心结构性质：$v\in A_i \Rightarrow \operatorname{Anc}^{+}(v)\subseteq A_i$；若 $v\in\operatorname{Desc}^{+}(u)$ 则 $\mathcal D_v\subseteq\mathcal D_u$。

**定义 4（紧凑层级查询）** 查询节点集合 $U\subseteq V$ 为非空 antichain（互非祖先），其覆盖标签集 $F_T(U)=\bigcup_{u\in U}\operatorname{Desc}^{+}(u)$，可行集

$$
\mathcal D_U=\bigcup_{u\in U}\mathcal D_u=\{o_i\mid A_i\cap U\neq\varnothing\}.
$$

$|U|=1$ 退化为单节点查询；任一非空节点集合可通过删除被祖先覆盖的后代化为 antichain，覆盖范围不变。

**定义 5（查询与精确结果）** 查询 $Q=(\mathbf q,U,k)$，距离 $\delta$（本文为平方 L2），排序键 $s_Q(o_i)=(\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i)$ 字典序。$k_Q=\min(k,|\mathcal D_U|)$，精确结果 $R^*(Q)$ 为 $\mathcal D_U$ 中按 $s_Q$ 升序的前 $k_Q$ 个不同对象。

**定义 6（近似结果与召回）** $\widehat R(Q)\subseteq\mathcal D_U$、$|\widehat R(Q)|\le k_Q$ 且无重复。当 $k_Q>0$，

$$
\operatorname{Recall@k}(Q)=\frac{|\widehat R(Q)\cap R^*(Q)|}{k_Q}.
$$

近似只允许最近邻遗漏；过滤、去重与数量必须精确。$k_Q=0$ 的查询不计入平均召回。

**定义 7（索引、空间与时间）** 索引 $I$ 由静态数据与 $T$ 构建，构建期不得读取查询集或真值。$M(I)$ 为常驻字节（向量、邻接、标签、树、入口、偏移；共享数据只计一次），$T_{I,\pi}(Q)$ 为查询时间（谓词解析、候选生成、搜索、去重、合并）。峰值内存另计。

**定义 8（研究问题）** 给定 $\mathcal D,T,\delta$、查询分布 $\mathcal P$、召回目标 $\rho\in(0,1]$ 与内存预算 $B$，设计 $I,\pi$ 使

$$
\min_{I,\pi}\ \mathbb E_{Q\sim\mathcal P}[T_{I,\pi}(Q)]
\quad\text{s.t.}\quad
M(I)\le B,\quad
\mathbb E[\operatorname{Recall@k}(Q)]\ge\rho,\quad
\widehat R(Q)\subseteq\mathcal D_U,\ \text{无重复}.
$$

本文固定 $\delta$ 为平方 L2、$k=10$、$\rho=0.95$，测量端到端吞吐（$\text{qps}=1/\mathbb E[T]$）与 $M(I)$、构建时间。

### 1.2 为什么这是一个独立问题

- 谓词族是**层状（laminar）**的：每个查询是若干子树的并，且 $T$ 在构建期已知；这与任意布尔过滤、范围过滤、集合包含均不同。
- 对象**多标签**使不同分支的可行集重叠；同一对象在一次查询中只能返回一次。重叠不是重复存储的理由，而是共享机会。
- 查询覆盖粗到细：根、深度 1、深度 2、叶与 antichain 并存，单一固定粒度的索引必然在某些粒度上退化。

## 2. 背景与相关工作

**应用背景。** 检索增强生成、企业知识库与学术检索中，文档同时属于多个层级分类（如 ACM CCS、商品类目、组织目录）；用户查询既可能指定粗类目，也可能指定细类目或若干并列类目。把层级当作标量过滤会丢失继承语义，把每个类目单独建索引会重复存储并在粗查询上合并爆炸。

**相关工作与差异。** 过滤 ANN 系统（Filtered-DiskANN、ACORN、UNG、Curator）研究“向量 + 谓词”的联合导航；其中 UNG 用标签导航图，ACORN 用谓词无关的子图遍历，Curator 按谓词构造临时索引。范围/嵌套索引（iRangeGraph）利用 segment-tree 单元图在范围查询间复用边。JAG 研究多属性尺度的共享导航边。它们证明了“共享图”在过滤器与范围上的价值，但：

- 多数方法仍需在查询期判定谓词或过滤候选；
- 面向层状子树并集、且对象多标签重叠的**边级去重**与**查询期无谓词判定**的表示，未被系统刻画；
- 共享带来的度数膨胀与空间节省之间的定量边界缺少测量。

本文不声称首次提出“共享图”；LES 的候选增量是：把每条边按其**最小激活节点**分桶，使查询 $u$ 只需扫描 preorder 区间 $\operatorname{subtree}(u)$ 内的桶，从而在无查询期谓词判定的前提下精确还原每个作用域的图。是否足以构成独立贡献，需与 iRangeGraph/UNG 逐机制对照后确认（尚未完成）。

**研究问题。**
- RQ1：一条边集能否同时服务所有层状作用域，且每个作用域的诱导图与原独立图等价？
- RQ2：去重带来的空间收益与度数膨胀带来的查询代价之间，边界在哪里？
- RQ3：在固定召回下，LES 是否优于全局过滤与逐作用域物化？

## 3. 提出的索引：Laminar Edge Sharing（LES）

### 3.1 边的激活集

设边 $e=(i,j)$。它在作用域 $u$ 中可用当且仅当 $i,j\in\mathcal D_u$，即 $u\in A_i\cap A_j$。由于 $A_i,A_j$ 向上封闭，$M_e=A_i\cap A_j$ 也向上封闭，其极小元构成一个 antichain $\min(M_e)$（两对象标签对的最小公共祖先集合，$|L_i|,|L_j|\le3$ 时规模 ≤9，通常为 1）。

**存储规则。** 每条有向边在 $\min(M_e)$ 的每个节点下存一次，记入对象 $i$ 的桶。节点按 DFS 先序编号，则 $\operatorname{subtree}(u)$ 是连续区间 $[\mathrm{tin}(u),\mathrm{tout}(u)]$；对象 $i$ 的桶表按 $\mathrm{tin}$ 排序。

**引理 1（区间切片精确还原作用域图）** 对任意 $u$，对象 $i$ 在 $u$ 下可扫描的邻居集合恰为

$$
\Gamma_u(i)=\{j\mid \exists m\in\min(M_{(i,j)}): m\in\operatorname{subtree}(u)\}.
$$

*证明.* ($\subseteq$) 若 $m\in\operatorname{subtree}(u)$，则 $u\in\operatorname{Anc}^{+}(m)\subseteq M_{(i,j)}$（向上封闭），故 $i,j\in\mathcal D_u$。( $\supseteq$) 若 $i,j\in\mathcal D_u$，则 $u\in M_{(i,j)}$；$M$ 向上封闭且非空，其极小元中必有某个 $m$ 满足 $u\in\operatorname{Anc}^{+}(m)$，即 $m\in\operatorname{subtree}(u)$。$\square$

因此查询 $u$ 无需求值谓词、无需过滤候选：对每个弹出的对象，二分定位区间并扫描桶内邻居即可。共享边的语义与“为 $u$ 单独构图”在边集上一致；若每个作用域的独立图由相同构图过程产生，则 LES 的诱导图就是这些图的并。

### 3.2 空间与度数

设独立物化时所有作用域的边总数为 $E_{\text{obs}}=\sum_u |E(G_u)|$，去重后唯一有向边为 $E_{\text{uniq}}$，桶条目为 $E_{\text{bucket}}=\sum_e |\min(M_e)|$（通常 $E_{\text{bucket}}\approx E_{\text{uniq}}$）。

- 空间：$O(E_{\text{bucket}})$ 条邻接 + $O(n)$ 个向量（每个对象只存一次）+ 每个对象的桶偏移；对比物化的 $O(E_{\text{obs}})$ 条邻接与 $O(\sum_u|\mathcal D_u|)$ 份向量。
- 查询：弹出对象 $i$ 时扫描度数为
$$
\deg_u(i)=\bigl|\{e\ \text{incident to } i:\ M_e\cap\operatorname{subtree}(u)\neq\varnothing\}\bigr|,
$$
满足 $\deg_u(i)\le\sum_{m\in\operatorname{subtree}(u)}\deg_m(i)$。粗作用域扫描并集边，细作用域只扫自己的桶。

这是 LES 的核心权衡：空间按去重率下降，粗查询的每跳代价按度数膨胀上升；细查询（叶、深度 2）几乎不受影响。

### 3.3 搜索算法

对查询 $u$：从 $u$ 的入口对象（任一成员，构建期记录）开始，执行与 HNSW 相同的上界层贪心 + level-0 beam。区别仅在邻居枚举：`neighbors(i,u)` 读取 $i$ 的桶表中 $\mathrm{tin}\in[\mathrm{tin}(u),\mathrm{tout}(u)]$ 的连续切片。距离在共享的 uint8 向量数组上计算；visited 以全局对象 ID 标记。停止条件与 HNSW 相同（最近候选超过当前第 ef 个保留距离）。由于切片精确等于 $G_u$ 的邻接，算法语义与“在 $G_u$ 上搜索”一致；差异仅来自 $G_u$ 本身是并图，度数更大。

复杂度：每次弹出 $O(\log d_i+\deg_u(i))$，其中 $d_i$ 为对象桶表长度；总代价 $O(\sum \deg_u(i))$ 次距离计算，与独立图相同阶，但度数不同。

## 4. 基线

| 编号 | 基线 | 说明 |
|---|---|---|
| B1 | 精确扫描 | 倒排/位图得到 $\mathcal D_U$，逐点计算距离；召回 1.0，代价随 $\vert\mathcal D_U\vert$ 线性 |
| B2 | 全局 HNSW + 后过滤 | 单图 + 谓词过滤；`ef` 随选择率放大；低选择率退化 |
| B3 | 逐标签 HNSW + 合并 | 每个直接标签一个索引，查询子树内全部标签索引并合并；粗查询启动次数多 |
| B4 | 逐作用域物化 HNSW | 每个树节点一个独立图，无查询期过滤；本文的强基线（原工程实现） |
| B5 | 段树共享图（iRangeGraph 风格） | 计划中；用于检验 LES 与已有共享机制的区别 |

B4 的工程实现（uint8 精确距离、PC1 布局、向量前置紧凑块、最近 M 截断、预取）只作为基线效率，不作为贡献。

## 5. 实验协议

固定 SIFT100k、85 节点 4 叉深度 3 的层级、每对象 1–3 个直接标签（属性与几何相关）、1000 条混合查询（8% 根、20% 深度 1、38% 深度 2、16% 叶、18% 双/三节点 antichain）、k=10。精确 oracle 离线计算；返回 ID 必须可行、无重复。指标：`qps_at_r95`（Recall@10 ≥ 0.95 且零违规时的吞吐）、Recall@10、p95、索引字节、构建时间。详见[基准协议](../benchmark-protocol.md)。

## 6. 结果

### 6.1 基线（已测）

| 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|
| B1 精确扫描 | ≈1,400 | 1.0 | ms 级 | 64 MB | 0 |
| B2 全局 HNSW + 过滤/小范围扫描 | 3,104 | 0.9987 | 0.83 ms | 64 MB | 16 s |
| B4 逐作用域物化 HNSW | **94,698** | 0.9570 | 20.9 µs | 83 MB | 30.8 s |

B4 的数字为确定性查询结果，主机吞吐抖动 ±5%。B3 与 B5 尚未实现；这是本文当前的主要缺口。

### 6.2 LES 的空间/度数分析（已测）

在 B4 的 85 个作用域图上（每作用域按最近 24 条截断），用 `les_analysis.cpp` 计算：

| 指标 | 数值 |
|---|---:|
| 观测有向边（各作用域之和） | 7,602,554 |
| 去重后有向边 | 3,658,543 |
| 重复比例 | 51.9% |
| 每作用域平均出度 | 13.0–20.9 |
| 根作用域切片平均出度 | 36.59（膨胀 1.9×） |
| 深度 1 切片平均出度 | 25.5–31.8（1.3–1.6×） |
| 深度 2 / 叶 | 约 17–27 / 与独立图接近 |

即：边存储可减半，向量从按成员重复（约 51 MB）降为每对象一份（12.8 MB）；代价是根查询每跳扫描约 1.9× 邻居。考虑桶表偏移与激活标记，LES 的常驻空间估算约 30–35 MB（B4 为 83 MB）。端到端 qps/召回尚未测量。

### 6.3 待补实验

1. 实现 LES 构建（边→桶、preorder 区间）与查询原型，测 `qps_at_r95` 与 Recall；
2. 实现 B3（逐标签）与 B5（段树共享）作为强对照；
3. 用相同 ef 扫描比较 B4/LES 的召回-吞吐曲线，验证“空间减半、粗查询变慢”的边界；
4. 1M 规模复核空间与构建时间。

## 7. 讨论与局限

- LES 与 iRangeGraph/UNG 的关系需要逐机制对照；当前不能声称新颖性成立。
- “并图”会使粗作用域的度数高于独立图，可能提高召回但增加每跳代价；净效果取决于查询混合比例，这正是 RQ2。
- 本文当前只有 B1/B2/B4 的完整测量与 LES 的空间分析；LES 搜索、B3/B5 是未完成部分，不应在论文中当作已验证结果。
- 单机单线程、静态数据、合成属性；未覆盖更新、并发、缓存与真实目录数据。

## 附录 A：强基线 B4 的实现与消融摘要

B4 的工程细节仅用于说明基线强度，不构成方法贡献。每作用域独立图在构建期由 hnswlib 生成，查询期使用自定义紧凑格式：

- uint8 精确距离：SIFT 取值为 0–255 的整数，int32 累加平方 L2 上界 $128\cdot255^2<2^{24}$，float 转换精确；元素块从 656 B 降到 226 B。
- 向量前置块布局：向量 128 B 在块首，随后是 uint16 计数与邻居；距离所需缓存行与块首重合。
- 最近 M 条邻居：level-0 平均出度 21.2（2M=48 槽），按距离排序后只存最近 24 条，块 226→178 B，索引 108→83 MB。
- 作用域分组执行与两处软件预取（邻居向量行、弹出候选的邻接行）。
- 参数 M=24、efC=200、ef=21。

消融（交错测量，同一 SIFT100k 工作组）：

| 变更 | 吞吐 | 召回 | 结论 |
|---|---:|---:|---|
| 全局过滤 + 小范围扫描 | 3,104 | 0.9987 | 基线 |
| 逐作用域物化（float，逐查询） | 17,683 | 0.9948 | 物化本身收益大 |
| + 作用域分组执行 | ≈+35% | 不变 | 缓存局部性 |
| + uint8 精确距离 | ≈+38% | 不变 | 带宽与核 |
| + PC1 内部 ID 布局 | ≈+5% | 不变 | 地址局部性 |
| + 紧凑块 + 预取 | 86,788 | 0.9609 | 系统优化 |
| + 最近 M 截断、ef=21 | 94,698 | 0.9570 | 最终基线 |

负结果：共享向量（第二随机流）、变量长度邻接（偏移间接）、小范围扫描、transparent huge pages、两查询交错 beam、多 seed、按作用域分配 ef、2 路循环展开，均中性或更差。两查询交错实验中发现共享 visited 数组会被并发搜索互相覆盖 generation，必须使用私有 visited；最终实现为单查询顺序执行。

## 8. 代码与复现

- `production/nested-attr-vector-search/src/prep.cpp`：层级、标签、工作组与精确 oracle。
- `production/nested-attr-vector-search/src/baseline.cpp`：B2/B4 候选（构建 = 逐作用域构图 + 紧凑导出；查询 = 分组 beam + 预取）。
- `production/nested-attr-vector-search/src/les_analysis.cpp`：LES 边去重与切片度数分析。
- `production/nested-attr-vector-search/src/harness.py`：固定基准、计时、oracle 复核。
- 入口：`bash autoresearch.sh`；分析工具：
  ```bash
  g++ -O2 -std=c++17 production/nested-attr-vector-search/src/les_analysis.cpp -o /tmp/les_analysis
  /tmp/les_analysis --meta production/nested-attr-vector-search/work/fixtures/meta.txt \
    --tree production/nested-attr-vector-search/work/fixtures/tree.txt \
    --labels production/nested-attr-vector-search/work/fixtures/labels.bin \
    --index-dir production/nested-attr-vector-search/work/index
  ```
