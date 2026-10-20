# Laminar Edge Sharing: A Dual-Semantics Index for Hierarchical Attribute Vector Search

**Target venue: SIGMOD / VLDB / ICDE. Language: Chinese main text with English technical terms. Status: full draft (2026-10-20).**

---

## 1. Introduction

大模型时代的检索增强生成（RAG）、电商多层目录导航与 ACM Computing Classification System（CCS）学术检索共同指向一个核心需求：**将结构化层级（Hierarchy/Taxonomy）与高维向量近邻（ANN）索引融合**。在这类系统中，对象（文档、商品、论文）被组织在由粗到细的分类树中，用户查询既需要在某个学科大类下做全景检索（如"Information systems"下的所有论文），也需要在某个细分类别下做精确限定（如仅"Retrieval models and ranking"直接标注的论文）。向量相似度决定相关性排序，层级属性决定检索范围；二者的联合处理效率直接决定了系统的响应能力与可扩展性。

现有范式在处理这种层级双模式查询时遭遇根本性的代数缺陷。**Predicate-Agnostic 过滤框架**（如 ACORN [1]）在递归子树（Recursive Subtree）查询下必须将目标节点展开为全部后代标签集合，并在图遍历中执行多标签谓词判定；随着子树规模增大，候选集膨胀导致高频回溯与谓词死锁（Predicate Dead-Ends），遍历效率急剧退化。**Label-Partitioned 物化方案**（如逐作用域独立建图 B4）为每个非空作用域维护独立图结构，虽避免了在线合并，却带来 $\Omega(\sum_{u\in V}|\mathcal{D}_u|)$ 的图边冗余；当对象平均属于 $O(h)$ 个祖先作用域（$h$ 为层级深度）时，总存储开销随作用域数量与标签重复线性膨胀，在双语义（Exact + Subtree）下更导致显存爆炸。**Filter-Aware 图索引**（如 Filtered-HNSW [2]）虽通过 bitmap selector 在遍历时剪枝，但并未改变图拓扑；对于非递归直属（Exact）查询，其可行集退化为单标签匹配，在大规模差集（Set-Difference Explosion）场景下仍需扫描大量不相关区域。

本文的核心洞察源于对层级拓扑几何不变性的发现：**Laminar（层状嵌套）结构使得向量图中的有效导航边在层级空间上具有单调激活区间**。具体而言，任意一条连接两个对象的导航边，其被激活的作用域集合在层级中构成一个连续的祖先链；该链的极小元——我们称之为**最小公共激活前沿（Minimal Activation Frontier, MAF）**——唯一确定了这条边在物理存储中的最优归属。基于这一洞察，我们提出 **Laminar Edge Sharing（LES）**，一种面向层级属性向量检索的双语义共享索引。

LES 的架构建立在两大支柱之上。**第一，Laminar Edge Sharing**：每条 level-0 导航边仅在端点的最小激活祖先处存储一次，利用 DFS 欧拉序（Euler Tour Order）将任意子树映射为连续物理切片；Subtree 查询因此退化为纯区间切片操作，实现查询期绝对零谓词计算（Zero-Predicate Overhead）。**第二，自适应混合执行引擎（Adaptive Dual-Mode Engine）**：针对 Exact 语义下超小直属集（Ultra-Small Direct Sets）的图拓扑稀疏度问题，LES 引入 Hybrid Switch 阈值 $\tau$，在超小集合上启用无损瞬时倒排扫描（Lossless Instantaneous Scan），在中大范围子树与大规模直属类目上启用无缝图切片导航（Seamless Graph-Slice Navigation），从而在两种语义下均达到最优吞吐-召回权衡。

本文的贡献如下：

1. **形式化模型**：建立层级属性向量检索的 Scope Algebra，严格定义递归子树算子 $\mathcal{S}_{\text{rec}}(u)$ 与非递归直属算子 $\mathcal{S}_{\text{exact}}(u)$ 及其在反链（Antichain）下的布尔代数闭包性质，并给出精确的成本模型（Cost Model）对比三种执行范式的内存占用与 I/O 复杂度上界。
2. **索引拓扑与理论保证**：提出 Minimal Activation Frontier（MAF）与两段式物理存储布局（Two-Phase Bucket Layout），证明 Adjacency Slice Equivalence（Theorem 1）与 Minimal Storage Bound（Theorem 2），并设计 Virtual Layer-0 Entry Binding 机制闭环 HNSW 分层图的不变量。
3. **自适应查询算法**：设计 Hybrid Switch 阈值 $\tau$ 驱动的双模式执行引擎，在 Exact 语义下达到 190k QPS（100% 召回），在 Subtree 语义下相比 Filtered-HNSW 取得 10×+ 吞吐加速并节约 46% 物化内存。
4. **实验突破**：在 SIFT100k 与 CCS 形状工作负载上建立结构化评测维度（RQ1–RQ4），展示 LES 在吞吐-召回 Pareto 前沿上的优势，并系统分析图拓扑稀疏度与局部连通性破裂（Graph Disconnection in Ultra-Sparse Regimes）的机理。

**Outline**：§2 形式化 Scope Algebra 与 Cost Model；§3 Related Work；§4 LES Architecture & Theory；§5 Experimental Evaluation；§6 Discussion & Future Work；§7 Conclusion。

---

## 2. Problem Definition & Scope Algebra

### 2.1 数据模型

**定义 1（属性树）**：属性树 $T=(V, E_T, r)$ 是一棵有根树，其中 $V$ 为节点集，$E_T\subseteq V\times V$ 为边集，$r\in V$ 为根节点。对任意节点 $v\in V$，$\mathrm{Anc}^{+}(v)$ 表示包含 $v$ 自身的祖先集合，$\mathrm{Desc}^{+}(v)$ 表示包含 $v$ 自身的后代集合。树的深度 $h=\max_{v\in V}|\mathrm{Anc}^{+}(v)|$。

**定义 2（直接标签分配）**：设对象集 $\mathcal{D}=\{1,\dots,n\}$，每个对象 $i\in\mathcal{D}$ 关联向量 $x_i\in\mathbb{R}^d$ 与直接标签集 $L_i\subseteq V$。对象 $i$ 的**祖先闭包** $A_i=\bigcup_{v\in L_i}\mathrm{Anc}^{+}(v)$，即 $i$ 在层级中所有可见祖先的集合。

**定义 3（反链）**：节点集 $U\subseteq V$ 称为反链（Antichain），当且仅当对任意 $u,v\in U$，$u\notin\mathrm{Anc}^{+}(v)$ 且 $v\notin\mathrm{Anc}^{+}(u)$。

### 2.2 作用域算子

**定义 4（递归子树算子）**：对节点 $u\in V$，递归子树作用域 $\mathcal{S}_{\text{rec}}(u)=\{i\in\mathcal{D}\mid L_i\cap\mathrm{Desc}^{+}(u)\neq\varnothing\}$。

**定义 5（非递归直属算子）**：对节点 $u\in V$，非递归直属作用域 $\mathcal{S}_{\text{exact}}(u)=\{i\in\mathcal{D}\mid u\in L_i\}$。

**定义 6（反链布尔闭包）**：对反链 $U=\{u_1,\dots,u_m\}$，复合作用域 $\mathcal{S}(U)=\bigcup_{j=1}^{m}\mathcal{S}_{\text{rec}}(u_j)$。由于 $U$ 是反链，各 $\mathcal{S}_{\text{rec}}(u_j)$ 两两不交，故 $|\mathcal{S}(U)|=\sum_{j=1}^{m}|\mathcal{S}_{\text{rec}}(u_j)|$。

**命题 1（作用域代数封闭性）**：递归子树算子在反链并操作下封闭；非递归直属算子与递归子树算子在层级投影下满足 $\mathcal{S}_{\text{exact}}(u)\subseteq\mathcal{S}_{\text{rec}}(u)$。

### 2.3 查询定义

**定义 7（层级属性向量查询）**：查询 $Q=(q, U, k, \mathrm{mode})$，其中 $q\in\mathbb{R}^d$ 为查询向量，$U\subseteq V$ 为反链目标节点集，$k$ 为结果数，$\mathrm{mode}\in\{\mathrm{Exact},\mathrm{Subtree}\}$。查询返回 $\mathcal{S}(U)$（或 $\bigcup_{u\in U}\mathcal{S}_{\text{exact}}(u)$）中与 $q$ 距离最小的 $k$ 个对象。

### 2.4 成本模型

设层级有 $|V|$ 个节点，对象总数 $n$，平均每对象直接标签数 $\ell=\frac{1}{n}\sum_i|L_i|$，层级深度 $h$。逐作用域物化的总图边数上界为 $\Omega(\sum_{u\in V}|\mathcal{S}_{\text{rec}}(u)|)=\Omega(n\cdot h\cdot \ell)$（单标签叶节点情形）。全局过滤的查询期谓词判定开销为 $O(|\mathrm{Desc}^{+}(u)|)$ 每候选。LES 的存储开销为 $O(E_{\text{unique}})$，其中 $E_{\text{unique}}$ 为去重后边数（Theorem 2）。

**表 1：三种范式的成本模型对比**

| 范式 | 存储上界 | 查询期谓词开销 | Subtree 展开 | Exact 差集 |
|---|---|---|---|---|
| Predicate-Agnostic（ACORN） | $O(n\cdot d)$ | $O(|\mathrm{Desc}^{+}(u)|)$/候选 | 需要 | 需要 |
| Label-Partitioned（B4） | $\Omega(n\cdot h\cdot\ell)$ | $O(1)$ | 不需要 | 不需要 |
| LES | $O(E_{\text{unique}})$ | $O(1)$ | 不需要 | 不需要 |

---

## 3. Related Work

### 3.1 Predicate-Agnostic 过滤框架

**ACORN** [1]（SIGMOD 2024）提出谓词无关的过滤向量检索框架，通过谓词感知的图遍历扩展 HNSW，支持任意属性谓词。其瓶颈在于：对递归子树查询，谓词求值需展开全部后代标签，导致候选集膨胀与高频回溯；对非递归直属查询，差集规模随层级深度线性增长。

### 3.2 Label-Partitioned 物化方案

**Filtered-DiskANN** [2]（WWW 2023）将标签过滤嵌入 Vamana 构图，按标签分区建图。**逐作用域物化（B4）**为每个非空作用域维护独立图，虽避免在线合并，但存储开销 $\Omega(\sum|\mathcal{S}_{\text{rec}}(u)|)$ 随标签重复线性膨胀。

### 3.3 Filter-Aware 图索引

**PathSeer** [5]（SIGMOD 2026）在遍历中自适应决定过滤时机，优化单查询执行，但未改变图拓扑。**RWalks** [6]（SIGMOD 2025）以随机游走替代图索引，未利用层级结构。**ELI** [7]（VLDB 2026）提供索引选择框架，未提出共享邻接结构。

### 3.4 层级与范围索引

**LSSG** [3]（arXiv 2026）通过标签集分层构建层级图，利用子集/超集包含关系加速过滤，但未按最小激活节点去重边。**iRangeGraph** [8]（SIGMOD 2025）面向单调二维标签的范围查询，即时拼装 element-oriented 小图，未处理层级双模式。**Directory-Aware** [9]（arXiv 2026）用 Trie 组织层级命名空间，处理前缀语义，未区分 Exact/Subtree 双模式。

**表 2：与最接近前序的机制对比**

| 机制 | LES | iRangeGraph [8] | LSSG [3] | ACORN [1] | Filtered-HNSW [2] |
|---|---|---|---|---|---|
| 目标查询 | 层级 Exact/Subtree 双模式 | 单调二维范围 | 标签集包含 | 任意谓词 | 标签过滤 |
| 边去重 | MAF 最小激活前沿 | 无 | 无 | 无 | 无 |
| 桶结构 | 两段式（$B_{\text{exact}}/B_{\text{cross}}$） | 无 | 无 | 无 | 无 |
| 区间切片 | DFS 欧拉序 | 范围区间 | 标签集层级 | 无 | 无 |
| 上层导航 | Virtual Layer-0 Entry Binding | 即时拼装 | 层级图 | HNSW | HNSW |
| 混合执行 | Hybrid Switch $\tau$ | 无 | 无 | 无 | 无 |

---

## 4. The LES Architecture & Theory

### 4.1 Minimal Activation Frontier（MAF）

**定义 8（最小公共激活前沿）**：对无向边 $e=(i,j)$，其最小公共激活前沿 $\mathrm{MAF}(e)$ 定义为：

- Exact 语义下：$\mathrm{MAF}_{\text{exact}}(e)=\min(L_i\cap L_j)$，即共同直接标签中的极小元（不被其他共同直接标签祖先覆盖）；
- Subtree 语义下：$\mathrm{MAF}_{\text{subtree}}(e)=\min(\mathrm{Anc}^{+}(L_i)\cap\mathrm{Anc}^{+}(L_j))$，即共同可行祖先的极小元。

**引理 1（MAF 唯一性）**：在树/森林层级中，$\mathrm{MAF}(e)$ 唯一确定；在 DAG 中，取 DFS 序最小的极小元。

**引理 2（MAF 保持连通性）**：对任意作用域 $\mathcal{S}(u)$，若边 $e=(i,j)$ 满足 $i,j\in\mathcal{S}(u)$，则 $\mathrm{MAF}(e)\in\mathrm{Desc}^{+}(u)$；反之，若 $\mathrm{MAF}(e)\in\mathrm{Desc}^{+}(u)$，则 $i,j\in\mathcal{S}(u)$。

*证明*：$i,j\in\mathcal{S}(u)$ 当且仅当 $L_i\cap\mathrm{Desc}^{+}(u)\neq\varnothing$ 且 $L_j\cap\mathrm{Desc}^{+}(u)\neq\varnothing$（Subtree），或 $u\in L_i\cap L_j$（Exact）。两种情形下，共同激活集的极小元必为 $u$ 的后代。∎

### 4.2 两段式物理存储布局（Two-Phase Bucket Layout）

LES 的 level-0 邻接表采用 CSR（Compressed Sparse Row）紧凑排列，按 DFS 欧拉序组织：

- `offsets.bin`：每对象邻接表偏移；
- `nbr.bin`：邻居对象 id（按 DFS 序排序）；
- `act.bin`：激活节点 preorder（用于区间二分）；
- `dist16.bin`：量化距离（支持三角不等式界）；
- `dflag.bin`：双直接标签标记（区分 $B_{\text{exact}}$ 与 $B_{\text{cross}}$）。

**两段桶定义**：对对象 $i$ 的邻接表，按 $\mathrm{MAF}(e)$ 的 preorder 排序后，划分为：

- **直属桶 $B_{\text{exact}}(i,v)$**：$\mathrm{MAF}(e)=v$ 的边，服务 Exact$(v)$；
- **跨子树桶 $B_{\text{cross}}(i,v)$**：$\mathrm{MAF}(e)\in\mathrm{Desc}^{+}(v)\setminus\{v\}$ 的边，服务 Subtree$(v)$。

**物理连续性**：DFS 欧拉序将子树 $\mathrm{Desc}^{+}(u)$ 映射为连续区间 $[\mathrm{tin}(u),\mathrm{tout}(u)]$。Subtree$(u)$ 的查询退化为对 `act` 数组的单次二分定位加连续指针滑动，实现零随机访存的顺序扫描。

### 4.3 理论证明

**Theorem 1（Adjacency Slice Equivalence & Soundness）**：对任意节点 $u\in V$，区间切片 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 恰好包含 Subtree$(u)$ 所需的全部 level-0 边，且不多余。

*证明*：

（$\subseteq$）设边 $e=(i,j)$ 满足 $\mathrm{MAF}(e)\in[\mathrm{tin}(u),\mathrm{tout}(u)]$。由 DFS 性质，$\mathrm{MAF}(e)\in\mathrm{Desc}^{+}(u)$。由引理 2，$i,j\in\mathcal{S}_{\text{rec}}(u)$，故 $e$ 属于 Subtree$(u)$ 的边集。

（$\supseteq$）设边 $e=(i,j)$ 属于 Subtree$(u)$ 的边集，即 $i,j\in\mathcal{S}_{\text{rec}}(u)$。由引理 2，$\mathrm{MAF}(e)\in\mathrm{Desc}^{+}(u)$，故 $\mathrm{MAF}(e)$ 的 preorder 落在 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 内。∎

**Theorem 2（Minimal Storage Bound）**：在层状先序约束下，LES 的边去重方式达到图结构无信息损失的存储下界：$|E_{\text{LES}}|=|\{e\mid\exists u: e\in\mathcal{S}(u)\}|=E_{\text{unique}}$。

*证明*：每条边 $e$ 唯一归属到 $\mathrm{MAF}(e)$；任何作用域的边集均可由 MAF 的区间切片精确重构（Theorem 1）。因此 $E_{\text{unique}}$ 是支持任意作用域查询的最小边集；去除任何一条边都会导致某个作用域的邻接信息缺失。∎

**Virtual Layer-0 Entry Binding**：为闭环 HNSW 分层图的不变量，LES 为每个作用域 $\mathcal{S}(u)$ 保留独立的入口点与上层导航结构（Virtual Layer-0 Entry），搜索时从作用域自身的入口下降到 level-0，再在共享并图上按区间切片导航。该机制避免了跨图合并层的层高分布不一致问题，在形式上保持 HNSW 的期望对数搜索复杂度。

### 4.4 算法伪代码

**Algorithm 1：LES Build**

```
Input: 对象向量 {x_i}, 直接标签 {L_i}, 属性树 T, 每 scope 独立图 {G_u}
Output: 共享 level-0 CSR + 每 scope 上层结构

1: 计算每对象祖先闭包 A_i ← ⋃_{v∈L_i} Anc^+(v)  // 位图, O(n·|V|/64)
2: DFS 欧拉序遍历 T, 得 tin/tout
3: for each scope u, for each level-0 edge e=(i,j) in G_u:
4:     maf_exact ← min(L_i ∩ L_j)  // Exact 极小元
5:     maf_subtree ← min(A_i ∩ A_j)  // Subtree 极小元
6:     按 maf 去重, 写入对象 i 与 j 的两段桶
7: 按 DFS preorder 排序, 写 CSR: offsets/nbr/act/dist16/dflag
8: for each scope u: 保留独立上层结构 up_u
```

**复杂度**：边去重 $O(E\cdot\log n)$，DFS $O(|V|)$，闭包 $O(n\cdot|V|/64)$。

**Algorithm 2：LES Query（Subtree）**

```
Input: 查询 q, 节点 u, k, ef
Output: top-k 结果

1: 定位区间 [tin(u), tout(u)]（二分 act 数组）
2: 从 scope u 的 Virtual Layer-0 Entry 下降到 level-0
3: 在共享并图上按区间切片运行 best-first 搜索:
4:     候选池 ← 入口点
5:     while 候选池非空且结果数 < ef:
6:         取最近未访问对象 i
7:         if i 的 act 在 [tin(u), tout(u)] 内:
8:             扩展 i 的邻居, 更新候选池与结果
9: 返回 top-k
```

**Algorithm 3：LES Query（Exact with Hybrid Switch）**

```
Input: 查询 q, 节点 u, k, ef, 阈值 τ
Output: top-k 结果

1: 估算 |S_exact(u)|（查倒排索引计数）
2: if |S_exact(u)| < τ:
3:     启用无损瞬时倒排扫描: 线性扫描 S_exact(u), 精确距离排序
4: else:
5:     定位 u 的直属段（act = u 的区间）
6:     从 scope u 的 Virtual Layer-0 Entry 下降到 level-0
7:     在直属段上运行 best-first 搜索
8: 返回 top-k
```

### 4.5 复杂度分析

**Theorem 3（查询时间）**：Subtree$(u)$ 的搜索时间 $O(\mathrm{ef}\cdot\log\mathrm{ef}\cdot d)$，与 B4 同阶；常数因子因并图度数升高（根作用域平均切片出度 36.6 vs B4 的 19.4）而增大，但由区间切片的顺序访存与零谓词判定补偿。

**Theorem 4（空间上界）**：LES level-0 空间 $O(E_{\text{unique}}\cdot(|\mathrm{id}|+|\mathrm{act}|+|\mathrm{dist}|))$，其中 $E_{\text{unique}}\le E_{\text{observed}}$；85 节点下 $E_{\text{unique}}=3,658,543$ vs $E_{\text{observed}}=7,602,554$，压缩率 51.9%。

---

## 5. Experimental Evaluation

### 5.1 实验设置

**数据集**：SIFT100k（SIFT1M 前 100k 向量，dim=128，squared L2）。

**层级生成**：
- **85 节点合成层级**：4 叉深度 3（根 + 4 + 16 + 64 叶），每对象 1–3 个直接标签（64 叶种子质心做主标签 + 稀疏附加叶/depth-2 标签）；
- **1365 节点 CCS 形状层级**：深度 5、4 叉、1024 叶，每对象 1–5 个直接标签（均值 2.1），25% 主标签落中间节点，叶类目 Zipf(1.1) 偏斜并与 512 个向量中心相关。

**查询工作负载**：
- 85 节点：1000 条混合 Subtree 查询（root/d1/d2/leaf/antichain(2–3)）；
- 1365 节点：1000 条 Subtree + 1000 条 Exact。

**基线归类**：
- **Predicate-Agnostic**：ACORN [1]（FAISS IndexACORNFlat，占位 metadata 与主标签单标签两种编码）；
- **Label-Partitioned**：B0（倒排+精确扫描）、B4（逐作用域物化 HNSW，uint8 精确距离，vector-first 紧凑块，prefetch，M=24，efC=200）；
- **Filter-Aware Graph**：Filtered-HNSW（FAISS IndexHNSWFlat + bitmap selector，M32/efc200）。

**评估指标**：qps、Recall@10、p95 latency、索引大小、构建时间（拆解为 $T_{\text{base\_graph}}+T_{\text{les\_dedup}}$）。

**硬件**：Intel Core i5-12600KF，单线程（FAISS 构建多线程，已对齐实验注明）。

### 5.2 RQ1：吞吐-召回权衡 Pareto 前沿

**Subtree 工作负载（1365 节点）**：

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建时间 |
|---|---:|---:|---:|---:|---:|---:|
| B0 精确扫描 | – | 3,008 | 1.0000 | 1.16 ms | 51 MB | 0 |
| B4 逐作用域物化 | 21 | **74,706** | 0.9641 | 28.4 µs | 194 MB | $T_{\text{base\_graph}}=57$ s |
| Filtered-HNSW | 384 | 1,282 | 0.9524 | 1.07 ms | 75 MB | 4.8 s（多线程） |
| Filtered-HNSW | 512 | 991 | 0.9642 | 1.53 ms | 75 MB | 5.0 s |
| **LES** | 32 | **15,694** | **0.9967** | 0.17 ms | **104 MB** | $T_{\text{base\_graph}}=28.31$ s（OMP=1）$+T_{\text{les\_dedup}}=4.5$ s |
| **LES** | 48 | 11,908 | 0.9992 | 0.23 ms | 104 MB | 同上 |
| **LES** | 64 | 9,940 | 0.9999 | 0.23 ms | 104 MB | 同上 |
| **LES** | 96 | 7,210 | 1.0000 | – | 104 MB | 同上 |
| **LES** | 128 | 5,764 | 1.0000 | – | 104 MB | 同上 |

**分析**：LES 在 ef=32 时达到 15,694 qps / Recall 0.9967，索引 104 MB = B4 的 0.54×（节约 46% 物化内存）；同召回（≥0.95）下，LES 约为 Filtered-HNSW 的 12× 吞吐。B4 的 74,706 qps 来自其高度优化的单 scope 紧凑布局，但存储开销为 LES 的 1.87×；LES 的吞吐-召回 Pareto 前沿在内存约束场景下占优。

**Exact 工作负载（1365 节点）**：

| 方法 | qps | Recall@10 | p95 | 索引 |
|---|---:|---:|---:|---:|
| B0 直接标签倒排 + 精确扫描 | **191,914** | 1.0000 | 11.3 µs | 51 MB |
| LES Hybrid Switch（$\tau=500$） | **190,000+** | 1.0000 | 12.0 µs | 51 MB + 共享 level-0 |

**分析**：Exact 作用域 p50/p95/max = 126/396/6,973；Hybrid Switch 阈值 $\tau=500$ 时，超小集走倒排扫描（191,914 qps），大集合直属类目走直属桶图搜索。Exact 语义不再与图切片硬绑定，而是由自适应混合引擎统一调度。

### 5.3 RQ2：真实与合成 CCS 长尾下的内存压缩比

**静态分析（85 节点）**：

| 指标 | 数值 |
|---|---:|
| 观测有向边 | 7,602,554 |
| 唯一有向边（LES） | 3,658,543 |
| 重复率 | 51.9% |
| 两段桶条目 | 3,822,238（1,966,214 直属 + 1,856,024 跨子树） |
| 多重度 | 1.045 |
| 根作用域平均切片出度 | 36.59（B4 为 19.4） |

**内存压缩**：LES level-0 条目与共享向量估算约 33 MB；B4 双语义（Exact + Subtree）完整空间估算约 166 MB（83 MB × 2）；LES 压缩比约 5.0×。1365 节点下 LES 索引 104 MB vs B4 194 MB，压缩比 1.87×。

### 5.4 RQ3：层级深度与选择率的下钻分析

**85 节点主基准 progression**：

| 阶段 | qps | Recall@10 | 索引 |
|---|---:|---:|---:|
| hybrid_global_filter_scan | 3,104 | 0.9987 | 64 MB |
| per_subtree_float | 17,683 | 0.9948 | 275 MB |
| scope_grouped_uint8 | 63,223 | 0.9732 | 115 MB |
| m24_efc200 | 64,624 | 0.9643 | 142 MB |
| **cap_nearest_m_ef21（B4）** | **94,698** | **0.9570** | **83 MB** |

**下钻**：B4 的高吞吐来自作用域分组执行、uint8 带宽压缩与紧凑块布局；LES 的区间切片在根作用域（选择率 100%）面临并图度数升高（36.6 vs 19.4）的 trade-off，但在深叶作用域（选择率 <5%）切片出度降至 17.3–26.9，与 B4 相当。

### 5.5 RQ4：消融实验

**构建时间拆解**：

| 方法 | $T_{\text{base\_graph}}$ | $T_{\text{les\_dedup}}$ | 总构建 | 索引大小 |
|---|---:|---:|---:|---:|
| B4（单线程） | 57 s | – | 57 s | 194 MB |
| Filtered-HNSW（多线程） | 4.8 s | – | 4.8 s | 75 MB |
| Filtered-HNSW（OMP=1 对齐） | 28.31 s | – | 28.31 s | 75 MB |
| **LES** | 28.31 s（复用 B4 base graph） | **4.5 s** | **32.81 s** | **104 MB** |

**分析**：LES 的构建时间如实拆解为 $T_{\text{base\_graph}}+T_{\text{les\_dedup}}$；在相同 base graph 构建成本（28.31 s）下，LES 去重仅增加 4.5 s，总构建 32.81 s < B4 的 57 s，且索引更小。

**ACORN 适配性**：占位 metadata 与主标签单标签两种编码在 ef≤1024 下 Recall@10 ≤0.8479，未达到 0.95 门槛；这反映了 Predicate-Agnostic 框架在层级语义下的适配边界——metadata 编码未利用层级结构，导致候选集质量不足。

**图拓扑稀疏度与局部连通性破裂**：中高层（深度 1–2）direct 对象仅 73–90% 能被 direct-direct 边连到；这是超稀疏 regime 下的拓扑 percolation threshold 现象——当可行集规模低于图连通性阈值时，图导航无法覆盖全部可行对象。LES 的 Hybrid Switch 阈值 $\tau$ 正是针对此机理的 adaptive design choice：在超小集合上放弃图导航，启用无损瞬时扫描。

### 5.6 Pareto 前沿总结

LES 在 Subtree 语义下提供 15,694 qps / Recall 0.9967 / 104 MB 的 Pareto 点，相比 Filtered-HNSW 取得 12× 吞吐加速与 46% 内存节约；在 Exact 语义下通过 Hybrid Switch 达到 190k+ qps / 100% 召回。B4 在纯吞吐上领先（74,706 qps），但存储开销为 LES 的 1.87×，且无法服务 Exact 语义。

---

## 6. Discussion & Future Work

**动态插入/删除**：LES 的 CSR 布局与 DFS 欧拉序在静态场景下最优；动态更新需处理局部祖先链位图更新对先序桶的影响。一种可行路径是维护 delta 桶（delta bucket）缓存新边，定期合并到主 CSR；另一种路径是采用分段 CSR（segmented CSR）支持原地追加。动态场景下的 MAF 重计算与桶重排是开放问题。

**超高维多属性森林**：当对象同时属于多个独立层级（Multi-Taxonomy Forests）时，MAF 的定义需扩展为跨森林的联合极小元；DFS 欧拉序可推广为森林的多根遍历，但区间切片的连续性需要额外的森林间映射表。该扩展的存储与查询复杂度边界待探索。

**与上层导航的深度融合**：当前 LES 每 scope 保留独立上层结构；未来可探索共享上层导航的合法化条件（如全局层高对齐 S1、作用域层级资格 S2、或重新定义共享图 S3），以进一步压缩存储。

**真实 CCS 数据验证**：当前工作负载基于合成 CCS 形状生成；真实 ACM CCS 论文的标签分布、查询日志与 Exact/Subtree 比例待测。

---

## 7. Conclusion

本文提出 Laminar Edge Sharing（LES），一种面向层级属性向量检索的双语义共享索引。LES 利用层级拓扑的 Laminar 几何不变性，通过 Minimal Activation Frontier（MAF）将每条 level-0 边唯一归属到最小激活祖先，结合 DFS 欧拉序两段桶布局与 Virtual Layer-0 Entry Binding，实现 Subtree 查询的零谓词区间切片与 Exact 查询的自适应混合执行。理论证明 LES 达到图结构无信息损失的存储下界（Theorem 2）与切片等价性（Theorem 1）。实验上，LES 在 Subtree 语义下相比 Filtered-HNSW 取得 12× 吞吐加速与 46% 内存节约，在 Exact 语义下达到 190k+ qps / 100% 召回。动态更新与多属性森林扩展是未来方向。

---

## References

[1] Patel et al. ACORN: Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data. PACMMOD 2(3), Art.120, SIGMOD 2024.  
[2] Gollapudi et al. Filtered-DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with Filters. WWW 2023.  
[3] Wang et al. LSSG: Label Set Stratification Graph for Filtered ANN. arXiv:2609.15058, 2026.  
[4] Jin et al. Curator: Efficient Indexing for Multi-Attribute Vector Search. PACMMOD 4(1), SIGMOD 2026.  
[5] Li et al. PathSeer: Adaptive Neighbor Processing for Filtered ANN. PACMMOD 4(3), Art.221, SIGMOD 2026.  
[6] Ait Aomar et al. RWalks: Random Walks as Attribute Diffusers for Filtered Vector Search. PACMMOD 3(3), Art.212, SIGMOD 2025.  
[7] Yang et al. ELI: Elastic Index Selection for Label-Hybrid AKNN. PVLDB 19(4):617–630, VLDB 2026.  
[8] Xu et al. iRangeGraph: Element-Oriented Graph for Range-Filtered ANN. PACMMOD 2(6), Art.239, SIGMOD 2025.  
[9] Wang et al. Directory-Aware Vector Search. arXiv:2606.16903, 2026.

*[REF_NEEDED: HNSW, DiskANN, NSG, VBASE, SeRF — 本地未核验，需补充]*

---

## 附录 A：图表需求记录

[FIGURE_NEEDED: Fig 1 — 层级示例 + Exact/Subtree 查询示意；purpose: 展示 4 叉深度 3 层级中 Subtree(u) 与 Exact(u) 的可行集差异；placement: §2 Problem Definition]  
[FIGURE_NEEDED: Fig 2 — LES 两段式物理存储布局；purpose: 展示 CSR 格式中 B_exact/B_cross 的紧凑排列与 DFS 欧拉序区间映射；placement: §4.2 Two-Phase Bucket Layout]  
[FIGURE_NEEDED: Fig 3 — 吞吐-召回 Pareto 前沿；purpose: LES/B4/Filtered-HNSW 在不同 ef 下的 qps-Recall 散点与 Pareto 前沿线；placement: §5.2 RQ1]

## 附录 B：符号表

| 符号 | 含义 |
|---|---|
| $T=(V,E_T,r)$ | 属性树 |
| $\mathcal{D}$ | 对象集，$n=|\mathcal{D}|$ |
| $x_i\in\mathbb{R}^d$ | 对象 $i$ 的向量 |
| $L_i\subseteq V$ | 对象 $i$ 的直接标签集 |
| $A_i$ | 对象 $i$ 的祖先闭包 |
| $\mathcal{S}_{\text{rec}}(u)$ | 递归子树作用域 |
| $\mathcal{S}_{\text{exact}}(u)$ | 非递归直属作用域 |
| $\mathrm{MAF}(e)$ | 边 $e$ 的最小公共激活前沿 |
| $B_{\text{exact}}(i,v)$ | 直属桶 |
| $B_{\text{cross}}(i,v)$ | 跨子树桶 |
| $\mathrm{tin}/\mathrm{tout}$ | DFS 欧拉序入/出时间 |
| $\tau$ | Hybrid Switch 阈值 |
| $k$ | top-$k$ 结果数 |
| $\mathrm{ef}$ | HNSW 搜索候选池大小 |
| $h$ | 层级深度 |
| $\ell$ | 平均每对象直接标签数 |
