# Laminar Edge Sharing: 层级属性向量检索的共享索引

**目标 venue：SIGMOD/VLDB/ICDE。语言：中文正文，保留英文术语。状态：审稿修正稿（2026-10-20）。**

**审稿修正说明**：本稿已根据审稿报告 `paper_review.md` 修正数学错误、删除无法支持的术语与主张，所有实验结果标为"已测""估算"或"待测"。

---

## 1. Introduction

层级属性向量检索要求同时满足结构化层级约束与高维向量近邻排序。以 ACM Computing Classification System（CCS）为例，论文被组织在分类树中，用户查询既需要在某个学科大类下做全景检索（Subtree），也需要在某个细分类别下做精确限定（Exact）。现有方案在处理这两种模式时面临执行效率与存储开销的权衡：全局过滤需要在线展开与合并，逐作用域物化带来存储冗余，过滤图索引未利用层级结构。

本文提出 Laminar Edge Sharing（LES），一种面向层级属性向量检索的共享索引。LES 的核心机制是：将 level-0 导航边按其在层级中的最小激活节点去重存储，利用 DFS 先序将子树映射为连续区间，使 Subtree 查询退化为区间切片；同时引入调度阈值 $\tau$，在 Exact 查询的超小可行集上启用精确扫描，在大集合上启用图搜索。

**贡献**：
1. 形式化层级属性向量检索的两种查询语义（Exact/Subtree）与成本模型；
2. 提出 level-0 边去重机制与两段式桶结构，证明 Subtree 查询的切片等价性（Theorem 1）；
3. 设计自适应调度阈值 $\tau$，在 Exact 语义下结合精确扫描与图搜索；
4. 在 SIFT100k 与 CCS 形状工作负载上实测 LES 的吞吐、召回与存储开销。

---

## 2. Problem Definition

**定义 1（属性树）**：属性树 $T=(V,E_T,r)$，$V$ 为节点集，$E_T\subseteq V\times V$ 为边集，$r\in V$ 为根节点。对任意节点 $v\in V$，$\mathrm{Anc}^{+}(v)$ 表示包含 $v$ 自身的祖先集合，$\mathrm{Desc}^{+}(v)$ 表示包含 $v$ 自身的后代集合。

**定义 2（直接标签分配）**：设对象集 $\mathcal{D}=\{1,\dots,n\}$，每个对象 $i\in\mathcal{D}$ 关联向量 $x_i\in\mathbb{R}^d$ 与直接标签集 $L_i\subseteq V$。对象 $i$ 的祖先闭包 $A_i=\bigcup_{v\in L_i}\mathrm{Anc}^{+}(v)$。

**定义 3（查询语义）**：
- **Subtree$(u)$**：可行集 $\{i\in\mathcal{D}\mid L_i\cap\mathrm{Desc}^{+}(u)\neq\varnothing\}$；
- **Exact$(u)$**：可行集 $\{i\in\mathcal{D}\mid u\in L_i\}$。

**定义 4（反链）**：节点集 $U\subseteq V$ 称为反链，当且仅当对任意 $u,v\in U$，$u\notin\mathrm{Anc}^{+}(v)$ 且 $v\notin\mathrm{Anc}^{+}(u)$。复合作用域 $\mathcal{S}(U)=\bigcup_{u\in U}\mathcal{S}_{\text{rec}}(u)$，基数 $|\mathcal{S}(U)|\le\sum_{u\in U}|\mathcal{S}_{\text{rec}}(u)|$，等号当且仅当无对象同时属于多个子树。

**成本模型**：设层级有 $|V|$ 个节点，对象总数 $n$，平均每对象直接标签数 $\ell$，层级深度 $h$。逐作用域物化的总图边数上界为 $\Omega(\sum_{u\in V}|\mathcal{S}_{\text{rec}}(u)|)$；单标签叶节点情形下为 $\Omega(n\cdot h\cdot\ell)$。全局过滤的查询期谓词判定开销为 $O(|\mathrm{Desc}^{+}(u)|)$ 每候选。

---

## 3. Related Work

| 方法 | 查询语义 | 边构建/共享方式 | 查询时过滤 | 支持的层级组合 | 与 LES 的可检验增量 |
|---|---|---|---|---|---|
| ACORN [1] | 任意谓词 | 全局图，无共享 | 谓词遍历 | 无层级 | LES 按层级去重边，ACORN 无 |
| Filtered-DiskANN [2] | 标签过滤 | 按标签分区 | bitmap selector | 无层级 | LES 用 DFS 区间切片，Filtered-DiskANN 无 |
| LSSG [3] | 标签集包含 | 标签集分层图 | 分层遍历 | 标签集格 | LES 区分 Exact/Subtree 双模式，LSSG 无 |
| iRangeGraph [8] | 单调二维范围 | 即时拼装小图 | 范围区间 | 二维单调 | LES 处理树状层级，iRangeGraph 无 |
| Directory-Aware [9] | 前缀语义 | Trie 组织 | 前缀匹配 | 树状前缀 | LES 区分 Exact/Subtree，Directory-Aware 无 |
| B4（baseline） | 单模式物化 | 每 scope 独立图 | 无 | 树状 | LES 共享 level-0 边，B4 无 |
| B0（baseline） | 精确扫描 | 倒排索引 | 无 | 树状 | LES 引入图导航，B0 无 |

**可检验增量**：
1. LES 的 level-0 边按最小激活节点去重，B4/LSSG/iRangeGraph 无此机制；
2. LES 的 Subtree 查询用 DFS 区间切片替代多索引合并，ACORN/Filtered-DiskANN 无；
3. LES 的 Exact 查询引入调度阈值 $\tau$（超小集扫描/大集合图搜索），B0/B4 无。

---

## 4. LES 方法

### 4.1 最小激活节点集

**定义 5（最小激活节点集）**：对无向边 $e=(i,j)$：
- Exact 语义下：$\mathrm{MA}_{\text{exact}}(e)=\min(L_i\cap L_j)$，即共同直接标签中的极小元集合；
- Subtree 语义下：$\mathrm{MA}_{\text{subtree}}(e)=\min(A_i\cap A_j)$，即共同可行祖先的极小元集合。

边 $e$ 归属到 $\mathrm{MA}(e)$ 中的每个节点。若需唯一归属，可按 DFS 序选最小元，但需证明不遗漏。

### 4.2 两段式桶结构

对象 $i$ 的邻接表按 $\mathrm{MA}(e)$ 的 preorder 排序，划分为：
- **$B_{\text{exact}}$**：$\mathrm{MA}(e)=\{v\}$ 的边；
- **$B_{\text{cross}}$**：$\mathrm{MA}(e)\subseteq\mathrm{Desc}^{+}(v)\setminus\{v\}$ 的边。

DFS 先序将子树 $\mathrm{Desc}^{+}(u)$ 映射为连续区间 $[\mathrm{tin}(u),\mathrm{tout}(u)]$。

**Theorem 1（Subtree 切片等价性，待完整证明）**：对任意 $u\in V$，区间 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 恰好包含 Subtree$(u)$ 所需的全部 level-0 边。

*证明思路*：需证明 $\mathrm{MA}(e)\subseteq\mathrm{Desc}^{+}(u)\iff i,j\in\mathcal{S}_{\text{rec}}(u)$。当前证明不完整，需补充多标签对象与反链情形的分析。

**Theorem 2（存储上界，已测）**：LES 存储 $|E_{\text{LES}}|=E_{\text{unique}}$，其中 $E_{\text{unique}}$ 为去重后边数；85 节点下 $E_{\text{unique}}=3,658,543$ vs $E_{\text{observed}}=7,602,554$（重复率 51.9%）。

### 4.3 调度阈值 $\tau$

Exact$(u)$ 查询时，若 $|\mathcal{S}_{\text{exact}}(u)|<\tau$，启用精确扫描；否则启用图搜索。$\tau$ 为系统参数，需实验标定。

### 4.4 算法

**Algorithm 1：Build**
```
Input: 对象向量, 直接标签, 属性树, 每 scope 独立图
Output: 共享 level-0 CSR + 每 scope 上层
1: 计算祖先闭包 A_i
2: DFS 先序 tin/tout
3: 对每 scope 的 level-0 边 (i,j), 计算 MA(e)
4: 按 MA 去重, 写入两段桶
5: 按 preorder 排序, 写 CSR
6: 保留每 scope 独立上层
```

**Algorithm 2：Subtree Query**
```
Input: q, u, k, ef
1: 定位 [tin(u), tout(u)]
2: 从 scope u 的入口下降到 level-0
3: 在共享并图上按区间切片 best-first 搜索
4: 若结果数 < k, 返回全部已找到结果
```

**Algorithm 3：Exact Query**
```
Input: q, u, k, ef, τ
1: 若 |S_exact(u)| < τ: 精确扫描 S_exact(u), 返回 top-k
2: 否则: 定位 u 的直属段, 图搜索, 返回 top-k
3: 若结果数 < k, 返回全部已找到结果
```

---

## 5. Experimental Evaluation

### 5.1 实验设置

**数据集**：SIFT100k（dim=128，squared L2）。

**层级生成**：
- 85 节点合成层级：4 叉深度 3，每对象 1–3 个直接标签；
- 1365 节点 CCS 形状层级：深度 5、4 叉、1024 叶，每对象 1–5 个直接标签（均值 2.1），25% 主标签落中间节点，叶类目 Zipf(1.1) 偏斜。

**查询工作负载**：
- 85 节点：1000 条混合 Subtree 查询；
- 1365 节点：1000 条 Subtree + 1000 条 Exact。

**基线**：
- B0：倒排 + 精确扫描；
- B4：逐作用域物化 HNSW（uint8 精确距离，M=24，efC=200）；
- Filtered-HNSW：FAISS IndexHNSWFlat + bitmap selector（M32/efc200）；
- ACORN：FAISS IndexACORNFlat（占位 metadata 与主标签单标签两种编码）。

**硬件**：Intel Core i5-12600KF，单线程（FAISS 构建多线程，已对齐注明）。

### 5.2 Subtree 工作负载（1365 节点，已测）

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建时间 | 状态 |
|---|---:|---:|---:|---:|---:|---:|---|
| B0 精确扫描 | – | 3,008 | 1.0000 | 1.16 ms | 51 MB | 0 | 已测 |
| B4 逐作用域物化 | 21 | 74,706 | 0.9641 | 28.4 µs | 194 MB | 57 s | 已测 |
| Filtered-HNSW | 384 | 1,282 | 0.9524 | 1.07 ms | 75 MB | 4.8 s | 已测 |
| Filtered-HNSW | 512 | 991 | 0.9642 | 1.53 ms | 75 MB | 5.0 s | 已测 |
| **LES** | 32 | 15,694 | 0.9967 | 0.17 ms | 104 MB | 4.5 s（dedup） | 已测 |
| **LES** | 48 | 11,908 | 0.9992 | 0.23 ms | 104 MB | 4.5 s | 已测 |
| **LES** | 64 | 9,940 | 0.9999 | 0.23 ms | 104 MB | 4.5 s | 已测 |
| **LES** | 96 | 7,210 | 1.0000 | – | 104 MB | 4.5 s | 已测 |
| **LES** | 128 | 5,764 | 1.0000 | – | 104 MB | 4.5 s | 已测 |

**分析**：LES 在 ef=32 时实测 15,694 qps / Recall 0.9967，索引 104 MB。B4 在 ef=21 时实测 74,706 qps / Recall 0.9570，索引 194 MB。两者未按相同 Recall@k 门槛比较，不构成"同召回胜出"。

### 5.3 Exact 工作负载（1365 节点，已测）

| 方法 | qps | Recall@10 | p95 | 索引 | 状态 |
|---|---:|---:|---:|---:|---|
| B0 直接标签倒排 + 精确扫描 | 191,914 | 1.0000 | 11.3 µs | 51 MB | 已测 |
| LES 图搜索 | – | – | – | – | 待测 |

**注意**：Exact 的 191,914 qps 是 B0 扫描的成绩，不是 LES 的成绩；LES 的 Exact 图搜索未实现/测试。

### 5.4 静态分析（85 节点，已测）

| 指标 | 数值 | 状态 |
|---|---:|---|
| 观测有向边 | 7,602,554 | 已测 |
| 唯一有向边（LES） | 3,658,543 | 已测 |
| 重复率 | 51.9% | 已测 |
| 两段桶条目 | 3,822,238 | 已测 |
| 多重度 | 1.045 | 已测 |
| 根作用域平均切片出度 | 36.59 | 已测 |

### 5.5 估算数据

| 指标 | 数值 | 状态 |
|---|---:|---|
| LES level-0 条目与共享向量空间 | ~33 MB | 估算 |
| B4 双语义完整空间 | ~166 MB | 估算 |
| LES 压缩比（vs B4 双语义） | ~5.0× | 估算 |

### 5.6 构建时间

| 方法 | base graph 构建 | edge dedup | 总构建 | 状态 |
|---|---:|---:|---:|---|
| B4（单线程） | 57 s | – | 57 s | 已测 |
| Filtered-HNSW（多线程） | 4.8 s | – | 4.8 s | 已测 |
| Filtered-HNSW（OMP=1） | 28.31 s | – | 28.31 s | 已测 |
| LES | 复用 B4 base graph（未单独计时） | 4.5 s | 待测 | 待测 |

### 5.7 ACORN 适配性（已测）

占位 metadata 与主标签单标签两种编码在 ef≤1024 下 Recall@10 ≤0.8479，未达到 0.95 门槛。

### 5.8 稀疏图连通性（已测）

中高层（深度 1–2）direct 对象仅 73–90% 能被 direct-direct 边连到。这是稀疏图采样下的连通性边界现象，需通过调度阈值 $\tau$ 处理。

---

## 6. Discussion & Future Work

**动态更新**：LES 的 CSR 布局在静态场景下有效；动态插入/删除需处理祖先链位图更新与先序桶重排，是开放问题。

**多属性森林**：当对象同时属于多个独立层级时，最小激活节点集的定义需扩展为跨森林联合极小元；DFS 先序可推广为多根遍历，但区间切片的连续性需额外映射表。

**真实 CCS 数据验证**：当前工作负载基于合成 CCS 形状生成；真实 ACM CCS 论文的标签分布与查询日志待测。

**Theorem 1 完整证明**：需补充多标签对象与反链情形的严格证明。

---

## 7. Conclusion

本文提出 Laminar Edge Sharing（LES），一种面向层级属性向量检索的共享索引。LES 通过最小激活节点集去重 level-0 边，结合 DFS 先序区间切片与调度阈值 $\tau$，在 Subtree 语义下实测 15,694 qps / Recall 0.9967 / 104 MB（ef=32），在 Exact 语义下引入精确扫描与图搜索的混合调度。静态分析显示 85 节点下 level-0 边重复率 51.9%。Theorem 1 的完整证明、LES Exact 图搜索实测、真实 CCS 数据验证与动态更新是未来工作。

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

---

## 附录 A：作图数据

**QPS–Recall 散点（Subtree，1365 节点，已测）**：

```
方法,ef,qps,recall_at_10
B4,21,74706,0.9641
Filtered-HNSW,384,1282,0.9524
Filtered-HNSW,512,991,0.9642
LES,32,15694,0.9967
LES,48,11908,0.9992
LES,64,9940,0.9999
LES,96,7210,1.0000
LES,128,5764,1.0000
```

**QPS–空间散点（Subtree，1365 节点，已测）**：

```
方法,qps,index_mb
B4,74706,194
Filtered-HNSW,1282,75
Filtered-HNSW,991,75
LES,15694,104
LES,11908,104
LES,9940,104
LES,7210,104
LES,5764,104
```

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
| $\mathrm{MA}(e)$ | 边 $e$ 的最小激活节点集 |
| $B_{\text{exact}}$ | 直属桶 |
| $B_{\text{cross}}$ | 跨子树桶 |
| $\mathrm{tin}/\mathrm{tout}$ | DFS 先序入/出时间 |
| $\tau$ | 调度阈值 |
| $k$ | top-$k$ 结果数 |
| $\mathrm{ef}$ | HNSW 搜索候选池大小 |

## 附录 C：尚缺实验清单

1. LES Exact 图搜索吞吐/召落实测；
2. 构建时间拆解（base graph + dedup）；
3. 真实 CCS 数据标签分布；
4. 动态更新（插入/删除）；
5. 1M 规模扩展性；
6. Theorem 1 完整证明；
7. Exact 切片等价性证明；
8. beam search 结果不足 $k$ 的接口行为。
