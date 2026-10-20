# Laminar Edge Sharing：层级属性向量检索的 Exact/Subtree 双语义共享索引

**状态：初稿（2026-10-20）。语言：中文正文，保留英文术语。目标 venue：SIGMOD/VLDB/ICDE。**

---

## 1. Introduction

向量检索通过表示空间中的距离刻画对象与查询的相关性，为文本、图像等非结构化数据提供检索方式。实际应用中，用户通常需要通过结构化属性限定检索范围：先确定哪些对象有资格进入结果，再从这些对象中返回与查询向量最近的 $k$ 个对象。这类"先过滤、再排序"的混合查询已在过滤 ANN（filtered approximate nearest neighbor）研究中得到广泛处理 [1, 2, 5]。

当属性具有层级关系时，检索范围可以随用户需求在不同粒度之间变化。以学术论文检索为例：ACM Computing Classification System（CCS）把论文组织为由粗到细的分类结构，一篇论文可同时归属于多个主题，主题成员沿祖先继承。用户检索"高效的语义检索索引"时，既可以在较宽的学科范围（如"Information systems"）内搜索，也可以进一步限定到某个细分类别（如"Retrieval models and ranking"）。查询指定一个分类节点后，该节点及其后代类别共同确定允许返回的论文集合，语义向量距离决定集合内部的结果顺序。

这种查询具有明确的层级约束语义，且存在两种自然模式：

- **Subtree（递归）**：检索指定节点 $u$ 的整棵子树，即所有标签属于 $u$ 的后代集合的对象；
- **Exact（非递归）**：只检索直接标注在 $u$ 的对象，不包含仅由后代继承的对象。

在直接标签表示下，Subtree 是"与后代标签集相交"的 overlap 过滤，Exact 是"与单标签相交"的 overlap 过滤。因此问题不在于可表达性，而在于执行：直接标签编码让 Subtree 需要展开后代标签并做多索引合并；祖先闭包编码让 Exact 退化为大规模差集；过滤图搜索仍需在遍历中判定谓词；双语义逐作用域物化使索引数量与空间翻倍。

**核心观察**：层级是层状（laminar）结构。同一条边被多个作用域重复使用时，其"最小激活节点"在层级上有明确归属。因此可以把边按归属存一次，查询按归属区间取回。

**候选贡献**（未确认，需逐机制对照）：我们提出 Laminar Edge Sharing（LES），用一套按最小激活节点去重的 level-0 邻接结构同时服务 Exact 与 Subtree。LES 的候选增量是"两段桶 + 区间切片 + level-0 共享"的组合：与 iRangeGraph [8] 的单调二维标签范围图不同，LES 处理层级双模式；与 UNG/LSSG [3] 的按标签建图不同，LES 按最小激活节点去重边；与 JAG 等标签共享边的工作不同，LES 区分直属/跨子树桶并给出前序区间执行。该组合是否有独立贡献尚未确认，需要逐机制对照与外部检索。

**Outline**：§2 形式化定义；§3 相关工作；§4 LES 设计（数据结构、构建、查询、正确性、复杂度）；§5 实验；§6 讨论；§7 结论。

---

## 2. Problem Definition

### 2.1 数据模型

设对象集 $\mathcal D=\{1,\dots,n\}$，每个对象 $i$ 有向量 $x_i\in\mathbb R^d$ 与直接标签集 $L_i\subseteq V$，其中 $V$ 是层级（树或森林）的节点集。层级边定义祖先关系：$\mathrm{Anc}^{+}(v)$ 表示包含 $v$ 自身的祖先集合，$\mathrm{Desc}^{+}(v)$ 表示包含 $v$ 自身的后代集合。对象 $i$ 的**可行祖先集** $A_i=\bigcup_{v\in L_i}\mathrm{Anc}^{+}(v)$，即 $i$ 在层级中"可见"的所有节点。

### 2.2 查询语义

查询 $Q=(q,u,k,\mathrm{mode})$，其中 $q\in\mathbb R^d$ 是查询向量，$u\in V$ 是目标节点，$k$ 是结果数，$\mathrm{mode}\in\{\mathrm{Exact},\mathrm{Subtree}\}$。

- **Subtree$(u)$**：可行集 $\mathcal D_u=\{i\in\mathcal D\mid L_i\cap\mathrm{Desc}^{+}(u)\neq\varnothing\}$，返回 $\mathcal D_u$ 中与 $q$ 最近的 $k$ 个对象；
- **Exact$(u)$**：可行集 $\mathcal B_u=\{i\in\mathcal D\mid u\in L_i\}$，返回 $\mathcal B_u$ 中与 $q$ 最近的 $k$ 个对象。

**antichain 查询**：给定互不祖先的节点集 $U$，可行集为各节点可行集的并，按节点分别执行后合并。

### 2.3 评估指标

- **Recall@k**：返回结果与精确 top-$k$ 的交集大小除以 $k$；
- **qps**：每秒查询数；**p95 latency**：95 分位延迟；
- **索引大小**：字节数；**构建时间**：秒。

### 2.4 现有方案的执行瓶颈

**B0 精确扫描**：对可行集线性扫描，$O(|\mathcal D_u|\cdot d)$ 距离计算。Subtree 作用域大时不可行。

**B1/B2 全局 HNSW + 过滤**：遍历全局图时判定谓词（直接标签或祖先闭包）。Subtree 需要展开后代标签并合并多个搜索；Exact 需要大规模差集；谓词判定在热路径上。

**B4 逐作用域物化**：每个非空作用域维护独立图。Subtree 查询只访问对应图，无需在线合并，但索引数量与空间随作用域数量与标签重复线性增长；双语义（Exact + Subtree）使空间翻倍。

**Filtered-HNSW** [1, 2]：bitmap selector 在遍历时剪枝，仍需遍历不相关区域；Subtree 的展开与合并开销仍在。

**ACORN** [1]：谓词无关的过滤检索，但 metadata 编码与过滤策略需适配层级语义；我们的实验显示其在 CCS 形状工作负载上召回不达标（§5.4）。

---

## 3. Related Work

### 3.1 过滤 ANN

**ACORN** [1]（SIGMOD 2024）提出谓词无关的过滤向量检索框架，通过谓词感知的图遍历扩展 HNSW，支持任意属性谓词。与本文的关系：ACORN 是通用过滤框架，未利用层级结构；我们的实验显示其在层级工作负载上召回不达标（§5.4）。

**Filtered-DiskANN** [2]（WWW 2023）提出 FilteredVamana 与 StitchedVamana，将标签过滤嵌入 DiskANN/Vamana 构图与查询。与本文的关系：Filtered-DiskANN 按标签建图，未按最小激活节点去重边；LES 的"两段桶"区分直属/跨子树，Filtered-DiskANN 未处理。

**PathSeer** [5]（SIGMOD 2026）在过滤 ANN 中自适应邻居处理，按谓词选择性动态决定过滤时机。与本文的关系：PathSeer 优化单查询执行，未改变索引结构；LES 是索引结构方案。

**RWalks** [6]（SIGMOD 2025）以随机游走作为属性扩散器组织嵌入与属性，替代图索引。与本文的关系：非图路线，未利用层级。

**ELI** [7]（VLDB 2026）针对标签混合 AKNN 的弹性索引选择，自适应决定索引粒度。与本文的关系：ELI 是索引选择框架，未提出层级共享邻接结构。

### 3.2 层级/范围索引

**LSSG** [3]（arXiv 2026，SIGMOD 2027 accepted 声明）通过渐进式标签集分层构建层级图，利用标签子集/超集包含关系加速标签过滤 ANN。与本文的关系：**最接近前序**。LSSG 按标签集分层，未按最小激活节点去重边；LES 的两段桶与区间切片是新增机制。

**iRangeGraph** [8]（SIGMOD 2025）面向范围过滤近邻搜索的专用图索引，查询时按谓词范围即时拼装 element-oriented 小图。与本文的关系：iRangeGraph 处理单调二维标签的范围查询，未处理层级双模式（Exact/Subtree）。

**Directory-Aware** [9]（arXiv 2026）感知目录/前缀层级的向量库查询与维护框架，用 Trie 结构组织层级命名空间。与本文的关系：**最接近前序**。Directory-Aware 处理前缀语义，未区分 Exact/Subtree 双模式；我们复现了其 DSQ/DSM 机制（§5.7）。

**SeRF**（SIGMOD 2023，本地未核验）面向范围过滤 ANN 的段图压缩索引。与本文的关系：范围过滤，未处理层级。

### 3.3 图索引基础

**HNSW**（TPAMI 2020，本地未核验）分层可导航小世界图，主流图 ANN 索引基础。LES 的上层结构复用 HNSW。

**DiskANN**（NeurIPS 2019，本地未核验）SSD 友好的 Vamana 图索引。

**NSG**（PVLDB 2019，本地未核验）基于单调相对邻图的剪枝构图。

**VBASE**（VLDB 2023，本地未核验）提出单调松弛图，统一解释过滤向量搜索中图遍历失败现象。

### 3.4 与本文的关系总表

| 论文 | venue/年份 | 一句话贡献 | 与本文关系 |
|---|---|---|---|
| ACORN [1] | SIGMOD 2024 | 谓词无关过滤检索框架 | baseline（通用过滤，未利用层级） |
| Filtered-DiskANN [2] | WWW 2023 | 标签过滤嵌入 Vamana 构图 | baseline（按标签建图，未去重边） |
| LSSG [3] | arXiv 2026 | 标签集分层图 | **最接近前序**（分层，未去重边/两段桶） |
| Curator [4] | SIGMOD 2026 | 低选择性过滤的聚类树+倒排 | baseline（低选择性场景） |
| PathSeer [5] | SIGMOD 2026 | 自适应邻居处理 | baseline（执行优化，非索引结构） |
| RWalks [6] | SIGMOD 2025 | 随机游走属性扩散器 | baseline（非图路线） |
| ELI [7] | VLDB 2026 | 标签混合索引选择 | baseline（索引选择框架） |
| iRangeGraph [8] | SIGMOD 2025 | 范围过滤即时拼装小图 | **最接近前序**（范围，非层级双模式） |
| Directory-Aware [9] | arXiv 2026 | 目录/前缀层级 Trie 框架 | **最接近前序**（前缀语义，未双模式） |

---

## 4. Our Approach: Laminar Edge Sharing

LES 的目标是用一套按最小激活节点去重的 level-0 邻接结构，同时服务 Exact 与 Subtree 两种查询，避免双语义逐作用域物化的空间翻倍与多索引合并。

### 4.1 核心观察

逐作用域物化（B4）对每个非空作用域维护独立图，空间随作用域数量与标签重复线性增长；过滤 HNSW 保留单一图但遍历中判定谓词，Subtree 需要展开后代标签并做多索引合并；直接标签与祖先闭包两种编码各有偏科。我们的观察是：**层级是层状（laminar）的，同一条边被多个作用域重复使用时，其"最小激活节点"在层级上有明确归属**。因此可以把边按归属存一次，查询按归属区间取回。

### 4.2 最小激活节点

对一条无向边 $(i,j)$，定义其最小激活节点为对象 $i,j$ 激活标签集的交集中的极小元：

- Exact 语义下，激活是共同直接标签 $L_i\cap L_j$，极小元是其中不被其他共同直接标签祖先覆盖的标签；
- Subtree 语义下，激活是共同可行祖先 $(\mathrm{Anc}^{+}(L_i)\cap\mathrm{Anc}^{+}(L_j))$ 的极小元。

由于层级是树/森林，每条边的最小激活节点唯一（或在 DAG 下取 DFS 序最小的极小元）。**一条边只存一次**，归属到其最小激活节点。

### 4.3 数据结构：两段桶与 DFS 先序

对象的 level-0 邻接表按 DFS 先序排序，同一节点下分两个桶：

- **直属桶 $B_{\text{exact}}(v)$**：最小激活节点 $=v$ 的边；Exact$(v)$ 只读这段；
- **跨子树桶 $B_{\text{cross}}(v)$**：最小激活节点 $=$ $v$ 的某个后代的边；Subtree$(v)$ 读整段 $[\mathrm{tin}(v),\mathrm{tout}(v)]$（含所有后代的 $B_{\text{exact}}$ 与 $B_{\text{cross}}$）。

DFS 先序把子树映射为连续区间，Subtree 查询变为一次区间切片，热路径无需谓词判定。antichain 查询按节点分别执行后合并。

**LES v1 只共享 level-0 边**。上界层与入口按作用域保留：搜索在作用域自己的上界结构中下降到 level-0，再在共享 level-0 并图上按区间切片搜索。这样避免跨图合并层的非法问题（不同图的高层层高分布不一致，直接拼接会破坏 HNSW 的分层不变量）。

**CSR 布局**：共享 level-0 用 CSR（compressed sparse row）存储：`offsets.bin`（每对象偏移）、`nbr.bin`（邻居 id）、`act.bin`（激活节点 preorder）、`dist16.bin`（量化距离，供三角剪枝）、`dflag.bin`（双直接标签标记）。上层结构每 scope 独立存储 `up_<v>.bin`。

### 4.4 构建算法

**输入**：对象向量 $\{x_i\}$、直接标签 $\{L_i\}$、层级 $V$、每 scope 独立图（如 B4 的 HNSW）。

**输出**：共享 level-0 CSR + 每 scope 上层。

**步骤**：
1. 计算每对象的祖先闭包 $A_i$（位图，$n\times \lceil|V|/64\rceil$ 字）；
2. DFS 先序遍历层级，得 $\mathrm{tin}/\mathrm{tout}$；
3. 对每 scope 的 level-0 边 $(i,j)$，计算最小激活节点 $\min(A_i\cap A_j)$（Subtree）或共同直接标签极小元（Exact）；
4. 按最小激活节点去重，写入对应对象的两段桶；
5. 按 DFS 序排序，写 CSR；量化距离到 `dist16`；
6. 每 scope 上层结构独立保留。

**复杂度**：边去重 $O(E\cdot \log n)$（$E$ 为观测边数），DFS $O(|V|)$，闭包计算 $O(n\cdot |V|/64)$。85 节点、100k 对象下构建约 4.5 s（单线程）。

### 4.5 查询算法

**Exact$(u)$**：
1. 定位 $u$ 的直属段（二分 `act` 找 $=u$ 的区间）；
2. 在该段上运行标准 best-first 搜索（HNSW level-0 搜索），用 scope 自身上层结构下降到 level-0。

**Subtree$(u)$**：
1. 定位 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 区间；
2. 在该区间上运行 best-first 搜索，用 scope 自身上层结构下降到 level-0。

**antichain$(U)$**：对每个 $u\in U$ 分别执行 Subtree$(u)$，合并去重（generation 计数实现 O(1) visited）。

**三角剪枝**（可选）：`dist16` 量化距离与查询距离比较，`dist16−dq > τ` 时剪枝。实验显示剪枝率 0%（§5.7），因 HNSW 邻居贴近 source。

### 4.6 正确性

**引理 1**（直属段切片 = Exact 作用域边集）：$v$ 的 $B_{\text{exact}}$ 恰好是 Exact$(v)$ 所需的 level-0 边。  
*证明*：$B_{\text{exact}}(v)$ 包含最小激活节点 $=v$ 的边，即共同直接标签极小元为 $v$ 的边；这些边的端点都在 Exact$(v)$ 的可行集内，且任何 Exact$(v)$ 的边其共同直接标签极小元必为 $v$。∎

**引理 2**（子树区间切片 = Subtree 作用域边集）：$[\mathrm{tin}(v),\mathrm{tout}(v)]$ 恰好是 Subtree$(v)$ 所需的 level-0 边。  
*证明*：DFS 先序下，$v$ 的后代集合是连续区间 $[\mathrm{tin}(v),\mathrm{tout}(v)]$；Subtree$(v)$ 的边其最小激活节点是 $v$ 的某个后代，因此落在该区间内。∎

**命题 1**（切片包含 B4 的全部 level-0 边）：任何作用域的 level-0 边都归属到其最小激活节点，因此切片不遗漏。  
*证明*：由最小激活节点定义，每条边唯一归属；切片按归属取回，不遗漏。∎

### 4.7 空间与度数分析

85 节点合成层级、100k 对象下：

- 逐作用域物化 level-0 有 7,602,554 条观测有向边；LES 去重后 3,658,543 条唯一边（重复 51.9%）；
- 两段桶条目 3,822,238（1,966,214 直属 + 1,856,024 跨子树），多重度 1.045；
- 仅 level-0 条目与共享向量估算约 33 MB；
- 代价：根作用域平均切片出度从 19.4 升到 36.6（并图更密）。

**定理 1**（空间上界）：LES level-0 空间 $O(E_{\text{unique}}\cdot (|\mathrm{id}|+|\mathrm{act}|+|\mathrm{dist}|))$，其中 $E_{\text{unique}}\le E_{\text{observed}}$，等号当且仅当无边重复。

**定理 2**（查询时间）：Subtree$(u)$ 的搜索时间 $O(\mathrm{ef}\cdot \log \mathrm{ef}\cdot d)$，与 B4 同阶，但常数因并图度数升高而增大。

### 4.8 与最接近前序的逐项机制对比

| 机制 | LES | iRangeGraph [8] | LSSG [3] | UNG | JAG |
|---|---|---|---|---|---|
| 目标查询 | 层级 Exact/Subtree 双模式 | 单调二维标签范围 | 标签集包含 | 标签过滤 | 标签过滤 |
| 边去重 | 按最小激活节点 | 不去重 | 不去重 | 不去重 | 部分共享 |
| 桶结构 | 两段（直属/跨子树） | 无 | 无 | 无 | 无 |
| 区间切片 | DFS 先序 | 范围区间 | 标签集层级 | 无 | 无 |
| 上层结构 | 每 scope 独立 | 即时拼装 | 层级图 | 完整图 | 完整图 |
| 层级语义 | 树/森林 | 二维单调 | 标签集格 | 无 | 无 |

---

## 5. Experiments

### 5.1 Setup

**数据集**：SIFT100k（SIFT1M 前 100k 向量，dim=128，squared L2）。

**层级生成**：
- **85 节点合成层级**：4 叉深度 3（根 + 4 + 16 + 64 叶），每对象 1–3 个直接标签（64 个叶种子质心做主标签 + 稀疏附加叶/depth-2 标签）；
- **1365 节点 CCS 形状层级**：深度 5、4 叉、1024 叶，每对象 1–5 个直接标签（均值 2.1），25% 主标签落中间节点（假设 H1），叶类目 Zipf(1.1) 偏斜并与 512 个向量中心相关。

**查询工作负载**：
- 85 节点：1000 条混合 Subtree 查询（root/d1/d2/leaf/antichain(2–3)）；
- 1365 节点：1000 条 Subtree + 1000 条 Exact。

**基线**：
- **B0**：精确扫描（Subtree 后代标签并 + 距离；Exact 直接标签倒排 + 距离）；
- **B1/B2**：全局 HNSW + 过滤/小范围扫描；
- **B4**：逐作用域物化（每 scope 一个 HNSW，uint8 精确距离，vector-first 紧凑块，prefetch，M=24，efC=200，ef=21）；
- **Filtered-HNSW**：FAISS IndexHNSWFlat + bitmap selector（M32/efc200）；
- **ACORN**：FAISS IndexACORNFlat（占位 metadata 与主标签单标签 metadata 两种编码）。

**评估指标**：qps、Recall@10、p95 latency、索引大小、构建时间。

**硬件**：Intel Core i5-12600KF，单线程（除 FAISS 构建多线程，已对齐实验注明）。

### 5.2 主基准：85 节点 Subtree progression

B4 逐阶段优化（交错测量，原始表见 `primary-progression.csv`）：

| 阶段 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|
| hybrid_global_filter_scan | 3,104 | 0.9987 | 0.83 ms | 64 MB | 16 s |
| per_subtree_float | 17,683 | 0.9948 | – | 275 MB | – |
| scope_grouped_uint8 | 63,223 | 0.9732 | – | 115 MB | – |
| pc1_layout | 61,529 | 0.9685 | – | 115 MB | – |
| m24_efc200 | 64,624 | 0.9643 | – | 142 MB | – |
| prefetch/vec_first/pop_prefetch | 77,833–85,001 | 0.9609 | – | 108 MB | – |
| **cap_nearest_m_ef21（最终 B4）** | **94,698** | **0.9570** | **20.9 µs** | **83 MB** | **30.8 s** |

负结果：共享向量、变长邻接、小范围扫描、透明大页、两查询交错、多 seed、按作用域分配 ef、循环展开。

### 5.3 CCS 形状双语义结果（1365 节点）

同机、单线程、u8 精确距离、ef=21：

| 模式 | 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---|---:|---:|---:|---:|---:|
| Subtree | B4 逐作用域物化 | **74,706** | 0.9641 | 28.4 µs | 194 MB | 57 s |
| Exact | B0 直接标签倒排 + 精确扫描 | **191,914** | 1.0000 | 11.3 µs | 51 MB | 0 |

**Exact 作用域很小**（p50/p95/max = 126/396/6,973），**为 Exact 物化图在此 workload 上没有收益**；LES 的直属桶必须先与扫描比较。

### 5.4 Filtered-HNSW / ACORN 对照（同 1000 条 Subtree 查询）

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|---:|
| B0 Subtree 精确扫描 | – | 3,008 | 1.0000 | 1.16 ms | 51 MB | 0 |
| B4 逐作用域物化 | 21 | **74,706** | 0.9641 | 28 µs | 194 MB | 57 s（单线程） |
| Filtered-HNSW（bitmap selector） | 384 | **1,282** | **0.9524** | 1.07 ms | 75 MB | 4.8 s（多线程） |
| Filtered-HNSW | 512 | 991 | 0.9642 | 1.53 ms | 75 MB | 5.0 s |
| ACORN（占位 metadata） | 1024 | 394 | 0.8479 | 3.85 ms | 70 MB | 0.8 s |
| ACORN（主标签单标签） | 512 | 620 | 0.8395 | 2.48 ms | 70 MB | 0.9 s |

同召回下 B4 约为 Filtered-HNSW 的 75×、精确扫描的 25×，代价是 2.6× 索引字节；构建线程与内存预算尚未完全对齐，ACORN 变体未达到目标召回，故为**当前配置下的基线结果**。

**FAISS 单线程对齐**：Filtered-HNSW ef384 在 OMP=1 下 qps=1,619 / Recall=0.9512 / 构建 28.31 s（`ccs-les.csv`），与 B4 构建 57 s 可比。

### 5.5 LES 原型结果（仅 Subtree）

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|---:|
| LES 原型 | 32 | 15,694 | 0.9967 | 0.17 ms | 104 MB | 4.5 s |
| LES 原型 | 48 | 11,908 | 0.9992 | 0.23 ms | 104 MB | 4.5 s |
| LES 原型 | 64 | 9,940 | 0.9999 | 0.23 ms | 104 MB | 4.5 s |
| LES 原型 | 96 | 7,210 | 1.0000 | – | 104 MB | 4.5 s |
| LES 原型 | 128 | 5,764 | 1.0000 | – | 104 MB | 4.5 s |

同召回下 LES 约为 B4 的 0.21× 吞吐、Filtered-HNSW 的 8–12×；并图度数更高、召回更高，吞吐/召回曲线需按各自 ef 分别对齐后再比较。LES 索引 104 MB = B4 的 0.54×。

### 5.6 静态分析（85 节点）

LES level-0 边去重静态分析（`les-analysis.txt`）：

| 指标 | 数值 |
|---|---:|
| 观测有向边 | 7,602,554 |
| 唯一有向边 | 3,658,543（重复 51.9%） |
| 两段桶条目 | 3,822,238（1,966,214 直属 + 1,856,024 跨子树） |
| 多重度 | 1.045 |
| 直属成员数 | 115,847（80 节点非空，max 5,894） |
| 根作用域平均切片出度 | 36.59（B4 为 19.4） |
| 深度 1 节点切片出度 | 25.50–31.75 |
| 深叶切片出度 | 17.27–26.94 |

仅 level-0 条目与共享向量估算约 33 MB；不包含上界层与 Exact 专属图，因此不能与双语义 B4 的完整空间直接比较。

### 5.7 负结果

**三角不等式剪枝**：精确（Recall 不变），但 `PRUNED 0/5.7M = 0.00%`；HNSW 图的邻居贴近 source，界几乎不触发。

**Exact 用共享 direct-direct 边**：中高层（深度 1–2）direct 对象只有 **73–90%** 能被 direct-direct 边连到；beam 漏掉孤立 direct 对象。**Exact 是节点集合问题，共享边不能替代 direct 标签倒排**。

**Directory-Aware DSQ/DSM 复现**：在 85 节点与 1365 节点层级上复现了 PE-Online/PE-Offline/TrieHI 三种目录表示的 scope 解析延迟与 MOVE/MERGE 维护成本（`directory-aware-reproduction.md`）。PE-Online 的递归候选与 oracle 逐位一致（query 0 校验通过），MOVE 200 次随机内部节点操作：PE 路径键重写 mean 7.35 ms vs TrieHI 重链+祖先聚合更新 mean 10.79 µs，约 600–700× 差距。该复现验证目录层机制，但 bitmap 是 64-bit 简化版而非 Roaring，压缩率不同，且未复现 Viking PG/IVF 的 ANN 行为。

### 5.8 消融与公平性

- **构建线程对齐**：FAISS 多线程构建 4.8–5.0 s vs OMP=1 下 28.31 s；B4 单线程 57 s；
- **ef 对齐**：LES ef32/48/64/96/128 与 B4 ef21、Filtered-HNSW ef384/512 分别对齐，吞吐/召回曲线需按各自 ef 扫描后比较；
- **ACORN 召回不达标**：占位 metadata 与主标签单标签两种编码在 ef≤1024 下 Recall@10 ≤0.8479，未达到 0.95 门槛，可能因 metadata 编码与层级语义不匹配。

---

## 6. Discussion

**分层语义未完全解决**：LES v1 只共享 level-0 边；共享上界层需要 S1（全局层高）、S2（作用域层级资格）或 S3（重新定义共享图）之一并重新证明不变量。当前每 scope 保留独立上层结构，避免了跨图合并层的非法问题，但上层空间未共享。

**Exact 是节点集合问题**：实验显示中高层 direct 对象只有 73–90% 能被 direct-direct 边连到，beam 漏掉孤立对象。共享边不能替代 direct 标签倒排；LES 的直属桶必须先与扫描比较，而 CCS 形状 workload 上 Exact 扫描已足够快（191,914 qps）。

**新颖性未确认**：LES 与 iRangeGraph、UNG、LSSG、JAG 在目标上相近；需逐机制对照与外部检索后才能判断"两段桶 + 区间切片 + level-0 共享"是否有独立贡献。当前只能表述为候选增量。

**规模与更新限制**：仅 100k、静态、单线程；1M 与目录调整的重建成本未测；距离限平方 L2；动态更新（插入/删除/标签变更）未处理。

**假设 H1 未验证**：25% 主标签落中间节点是假设，非分类事实；真实 CCS 数据的标签位置分布待测。

## 7. Conclusion

我们提出 Laminar Edge Sharing（LES），用一套按最小激活节点去重的 level-0 邻接结构同时服务层级属性向量检索的 Exact 与 Subtree 双模式。LES 的两段桶与 DFS 先序区间切片把 Subtree 查询变为区间操作，热路径无需谓词判定；正确性由两条引理与一条命题支撑。静态分析显示 85 节点下 level-0 边重复 51.9%，空间估算约 33 MB。实验上，LES 原型在 Subtree 工作负载上达到 15,694 qps（ef32）/ Recall 0.9967，索引为 B4 的 0.54×，但吞吐为 B4 的 0.21×；Exact 工作负载上共享边不能替代倒排扫描。分层语义共享、双语义完整评测、真实 CCS 数据验证与动态更新是未来工作。

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

[FIGURE_NEEDED: Fig 1 — 层级示例 + Exact/Subtree 查询示意；purpose: 展示 4 叉深度 3 层级中一个 Subtree(u) 与 Exact(u) 的可行集差异；placement: §2 Problem Definition]  
[FIGURE_NEEDED: Fig 2 — LES 数据结构示意；purpose: 展示两段桶（B_exact/B_cross）+ DFS 先序区间 [tin,tout] 的映射；placement: §4.3 数据结构]  
[FIGURE_NEEDED: Fig 3 — 吞吐/召回曲线；purpose: LES/B4/Filtered-HNSW 在不同 ef 下的 qps-Recall 散点；placement: §5.5 LES 原型结果]

## 附录 B：符号表

| 符号 | 含义 |
|---|---|
| $\mathcal D$ | 对象集，$n=|\mathcal D|$ |
| $x_i\in\mathbb R^d$ | 对象 $i$ 的向量 |
| $L_i\subseteq V$ | 对象 $i$ 的直接标签集 |
| $V$ | 层级节点集 |
| $\mathrm{Anc}^{+}(v)$ | $v$ 的祖先集合（含自身） |
| $\mathrm{Desc}^{+}(v)$ | $v$ 的后代集合（含自身） |
| $A_i$ | 对象 $i$ 的可行祖先集 |
| $\mathcal D_u$ | Subtree$(u)$ 可行集 |
| $\mathcal B_u$ | Exact$(u)$ 可行集 |
| $B_{\text{exact}}(v)$ | 直属桶 |
| $B_{\text{cross}}(v)$ | 跨子树桶 |
| $\mathrm{tin}/\mathrm{tout}$ | DFS 先序入/出时间 |
| $k$ | top-$k$ 结果数 |
| $\mathrm{ef}$ | HNSW 搜索候选池大小 |
