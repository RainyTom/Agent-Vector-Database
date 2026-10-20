# LES 论文审稿报告：术语、正确性、新颖性、实验审计

**审稿标准**：SIGMOD/VLDB/ICDE 审稿人 + 论文合作者。  
**目标**：每个术语、定理、实验主张可核验。  
**审计日期**：2026-10-20。

---

## ① 术语与事实审计表

| 术语 | 出现次数 | 类型 | 审计结论 | 处理 |
|---|---:|---|---|---|
| **LES** | 46 | 本文自定义缩写 | 保留，首次出现需全称 Laminar Edge Sharing | 保留 |
| **MAF / Minimal Activation Frontier** | 29 | **本文自定义** | 非领域通用术语；定义 8 存在逻辑漏洞（见②）；建议改为"最小激活节点"或"边归属节点"，删除缩写 | **修正或删除** |
| **Scope Algebra** | 3 | **本文自定义** | 非既有理论；定义 4–6 是集合运算，不构成代数结构；建议改为"作用域语义" | **修正** |
| **Virtual Layer-0 Entry Binding** | 4 | **本文自定义** | 包装普通实现步骤（每 scope 保留独立入口）；无机制创新；建议改为"按作用域入口" | **删除或降级为描述** |
| **Hybrid Switch** | 9 | **本文自定义** | 是阈值调度的工程策略；非理论概念；建议改为"自适应调度阈值 $\tau$" | **修正** |
| **Zero-Predicate Overhead** | 1 | **无法支持** | 查询仍需区间二分与边界检查；非零开销；删除 | **删除** |
| **percolation threshold** | 1 | **误用物理概念** | 图连通性破裂是稀疏图采样问题，非渗流阈值；建议改为"稀疏图连通性边界" | **修正** |
| **Two-Phase Bucket Layout** | 3 | **本文自定义** | 是 CSR 分段的实现细节；非布局理论；建议改为"两段式桶结构" | **修正** |
| **Adjacency Slice Equivalence** | 2 | **本文自定义定理名** | Theorem 1 证明有漏洞（见②）；需修正后重新命名 | **修正** |
| **Minimal Storage Bound** | 2 | **本文自定义定理名** | Theorem 2 表述过强；需限定条件 | **修正** |
| **Adaptive Dual-Mode Engine** | 1 | **过度包装** | 是 if-else 调度；非引擎；删除 | **删除** |
| **Lossless Instantaneous Scan** | 1 | **过度包装** | 是精确扫描；非无损瞬时；删除 | **删除** |
| **Seamless Graph-Slice Navigation** | 1 | **过度包装** | 是区间切片搜索；非无缝；删除 | **删除** |
| **Pareto 前沿** | 3 | **实验误用** | 只有 5 个 ef 实测点，非完整曲线；应称"实测点" | **修正** |
| **同召回胜出** | 2 | **实验误用** | LES ef32 recall=0.9967 vs B4 ef21 recall=0.9570，未按相同 Recall@k 门槛比较；删除 | **删除** |

---

## ② 必须修正的数学/算法问题

### 问题 1：反链子树集合可能交叠（定义 6 与命题 1 错误）

**草稿主张**：定义 6 称"反链 $U$ 的复合作用域 $\mathcal{S}(U)=\bigcup\mathcal{S}_{\text{rec}}(u_j)$，由于 $U$ 是反链，各 $\mathcal{S}_{\text{rec}}(u_j)$ 两两不交"。

**反例**：对象 $i$ 有标签 $L_i=\{a,b\}$，其中 $a\in\mathrm{Desc}^{+}(u_1)$，$b\in\mathrm{Desc}^{+}(u_2)$，$u_1,u_2$ 是反链。则 $i\in\mathcal{S}_{\text{rec}}(u_1)\cap\mathcal{S}_{\text{rec}}(u_2)$，两集合交叠。

**修正**：删除"两两不交"与 $|\mathcal{S}(U)|=\sum|\mathcal{S}_{\text{rec}}(u_j)|$。改为：$\mathcal{S}(U)=\bigcup_{u\in U}\mathcal{S}_{\text{rec}}(u)$，基数 $|\mathcal{S}(U)|\le\sum|\mathcal{S}_{\text{rec}}(u)|$，等号当且仅当无对象同时属于多个子树。

### 问题 2：MAF 极小元不唯一（定义 8 与引理 1 错误）

**草稿主张**：引理 1 称"在树/森林层级中，$\mathrm{MAF}(e)$ 唯一确定"。

**反例**：对象 $i,j$ 有共同直接标签 $L_i\cap L_j=\{v_1,v_2\}$，其中 $v_1,v_2$ 无祖先-后代关系（如不同分支的叶节点）。则 $\min(L_i\cap L_j)=\{v_1,v_2\}$，极小元不唯一。

**修正**：定义 8 改为"最小激活节点集" $\mathrm{MA}(e)=\min(L_i\cap L_j)$（Exact）或 $\min(A_i\cap A_j)$（Subtree），允许多个极小元；边 $e$ 归属到所有极小元（或按 DFS 序选其一，但需证明不遗漏）。

### 问题 3：Exact 祖先-后代关系漏边（Theorem 1 证明错误）

**草稿主张**：Theorem 1 称"区间切片 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 恰好包含 Subtree$(u)$ 所需的全部 level-0 边"。

**反例（Exact 语义）**：对象 $i$ 有标签 $u$，对象 $j$ 有标签 $v$，其中 $v\in\mathrm{Desc}^{+}(u)$。Exact$(u)$ 的可行集 $\mathcal{S}_{\text{exact}}(u)$ 只含 $i$；但边 $(i,j)$ 的 Exact MAF 为 $u$（因 $u\in L_i$，$u\notin L_j$，共同直接标签极小元需重新分析）。若 $j\notin\mathcal{S}_{\text{exact}}(u)$，边 $(i,j)$ 不应被 Exact$(u)$ 使用；但按草稿的区间切片，$v\in\mathrm{Desc}^{+}(u)$ 的边会被包含，导致漏边或错边。

**修正**：Exact 与 Subtree 的边归属逻辑必须分开定义；Exact 的边归属仅基于共同直接标签，与 DFS 区间无关；Theorem 1 需分别陈述 Exact 与 Subtree 的切片等价性。

### 问题 4：跨作用域共享 level-0 边的 HNSW 搜索不变量（Virtual Layer-0 Entry Binding 未证明）

**草稿主张**：Virtual Layer-0 Entry Binding"在形式上保持 HNSW 的期望对数搜索复杂度"。

**问题**：HNSW 的不变量依赖于每层图的独立性与入口点的层分配；共享 level-0 边后，不同作用域的入口点在同一并图上搜索，visited 标记、候选池管理、邻居扩展的语义未重新定义。草稿未证明：共享 level-0 是否破坏期望对数复杂度？beam search 在并图上的终止条件是否仍有效？

**修正**：删除"期望对数搜索复杂度"主张；改为"每作用域保留独立入口点与上层结构，level-0 共享仅用于邻居扩展；搜索复杂度由 base graph 决定，共享 level-0 的度数变化影响常数因子"。

### 问题 5：beam search 结果不足 $k$ 的接口要求（未定义）

**问题**：草稿未定义当 $\mathrm{ef}<k$ 或可行集大小 $<k$ 时，算法返回什么。HNSW 的 beam search 可能返回不足 $k$ 个结果。

**修正**：Algorithm 2/3 需补充：若候选池耗尽且结果数 $<k$，返回全部已找到结果；若可行集大小 $<k$，返回全部可行对象。

---

## ③ 相关工作定位

| 方法 | 查询语义 | 边构建/共享方式 | 查询时过滤 | 支持的层级组合 | 与 LES 的可检验增量 |
|---|---|---|---|---|---|
| **ACORN** [1] | 任意谓词 | 全局图，无共享 | 谓词遍历 | 无层级 | LES 按层级去重边，ACORN 无 |
| **Filtered-DiskANN** [2] | 标签过滤 | 按标签分区 | bitmap selector | 无层级 | LES 用 DFS 区间切片，Filtered-DiskANN 无 |
| **LSSG** [3] | 标签集包含 | 标签集分层图 | 分层遍历 | 标签集格 | LES 区分 Exact/Subtree 双模式，LSSG 无 |
| **iRangeGraph** [8] | 单调二维范围 | 即时拼装小图 | 范围区间 | 二维单调 | LES 处理树状层级，iRangeGraph 无 |
| **Directory-Aware** [9] | 前缀语义 | Trie 组织 | 前缀匹配 | 树状前缀 | LES 区分 Exact/Subtree，Directory-Aware 无 |
| **B4（本文 baseline）** | 单模式物化 | 每 scope 独立图 | 无 | 树状 | LES 共享 level-0 边，B4 无 |
| **B0（本文 baseline）** | 精确扫描 | 倒排索引 | 无 | 树状 | LES 引入图导航，B0 无 |

**可检验增量**（非"Exact/Subtree 不同"）：
1. LES 的 level-0 边按"最小激活节点"去重，B4/LSSG/iRangeGraph 无此机制；
2. LES 的 Subtree 查询用 DFS 区间切片替代多索引合并，ACORN/Filtered-DiskANN 无；
3. LES 的 Exact 查询引入调度阈值 $\tau$（超小集扫描/大集合图搜索），B0/B4 无。

---

## ④ 重写后的 Introduction 与方法章节

### 1. Introduction

层级属性向量检索要求同时满足结构化层级约束与高维向量近邻排序。以 ACM CCS 为例，论文被组织在分类树中，用户查询既需要在某个学科大类下做全景检索（Subtree），也需要在某个细分类别下做精确限定（Exact）。现有方案在处理这两种模式时面临执行效率与存储开销的权衡：全局过滤需要在线展开与合并，逐作用域物化带来存储冗余，过滤图索引未利用层级结构。

本文提出 Laminar Edge Sharing（LES），一种面向层级属性向量检索的共享索引。LES 的核心机制是：将 level-0 导航边按其在层级中的最小激活节点去重存储，利用 DFS 先序将子树映射为连续区间，使 Subtree 查询退化为区间切片；同时引入调度阈值 $\tau$，在 Exact 查询的超小可行集上启用精确扫描，在大集合上启用图搜索。

**贡献**：
1. 形式化层级属性向量检索的两种查询语义（Exact/Subtree）与成本模型；
2. 提出 level-0 边去重机制与两段式桶结构，证明 Subtree 查询的切片等价性（Theorem 1）；
3. 设计自适应调度阈值 $\tau$，在 Exact 语义下结合精确扫描与图搜索；
4. 在 SIFT100k 与 CCS 形状工作负载上实测 LES 的吞吐、召回与存储开销。

### 2. Problem Definition

**定义 1（属性树）**：$T=(V,E_T,r)$，$V$ 为节点集，$E_T$ 为边集，$r$ 为根。$\mathrm{Anc}^{+}(v)$、$\mathrm{Desc}^{+}(v)$ 分别为祖先与后代集合（含自身）。

**定义 2（直接标签）**：对象 $i$ 的直接标签集 $L_i\subseteq V$，祖先闭包 $A_i=\bigcup_{v\in L_i}\mathrm{Anc}^{+}(v)$。

**定义 3（查询语义）**：
- Subtree$(u)$：可行集 $\{i\mid L_i\cap\mathrm{Desc}^{+}(u)\neq\varnothing\}$；
- Exact$(u)$：可行集 $\{i\mid u\in L_i\}$。

**定义 4（反链）**：$U\subseteq V$ 中任意两节点无祖先-后代关系。复合作用域 $\mathcal{S}(U)=\bigcup_{u\in U}\mathcal{S}_{\text{rec}}(u)$，基数 $|\mathcal{S}(U)|\le\sum|\mathcal{S}_{\text{rec}}(u)|$。

### 3. LES 方法

**定义 5（最小激活节点集）**：对边 $e=(i,j)$：
- Exact：$\mathrm{MA}_{\text{exact}}(e)=\min(L_i\cap L_j)$；
- Subtree：$\mathrm{MA}_{\text{subtree}}(e)=\min(A_i\cap A_j)$。

边 $e$ 归属到 $\mathrm{MA}(e)$ 中的每个节点（或按 DFS 序选其一，需证明不遗漏）。

**两段式桶结构**：对象 $i$ 的邻接表按 $\mathrm{MA}(e)$ 的 preorder 排序，划分为：
- $B_{\text{exact}}$：$\mathrm{MA}(e)=\{v\}$ 的边；
- $B_{\text{cross}}$：$\mathrm{MA}(e)\subseteq\mathrm{Desc}^{+}(v)\setminus\{v\}$ 的边。

**Theorem 1（Subtree 切片等价性，已测）**：对任意 $u$，区间 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 恰好包含 Subtree$(u)$ 所需的全部 level-0 边（需补充证明：$\mathrm{MA}(e)\subseteq\mathrm{Desc}^{+}(u)$ 当且仅当 $i,j\in\mathcal{S}_{\text{rec}}(u)$）。

**Theorem 2（存储上界，已测）**：LES 存储 $|E_{\text{LES}}|=E_{\text{unique}}$，其中 $E_{\text{unique}}$ 为去重后边数；85 节点下 $E_{\text{unique}}=3,658,543$ vs $E_{\text{observed}}=7,602,554$。

**调度阈值 $\tau$**：Exact$(u)$ 查询时，若 $|\mathcal{S}_{\text{exact}}(u)|<\tau$，启用精确扫描；否则启用图搜索。$\tau$ 为系统参数，需实验标定。

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

## ⑤ 数据结果表及作图数据

### 5.1 已测数据（Measured）

**Subtree 工作负载（1365 节点，已测）**：

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

**Exact 工作负载（1365 节点，已测）**：

| 方法 | qps | Recall@10 | p95 | 索引 | 状态 |
|---|---:|---:|---:|---:|---|
| B0 直接标签倒排 + 精确扫描 | 191,914 | 1.0000 | 11.3 µs | 51 MB | 已测 |
| LES 图搜索（未测） | – | – | – | – | 待测 |

**注意**：Exact 的 191,914 qps 是 **B0 扫描** 的成绩，不是 LES 的成绩；LES 的 Exact 图搜索尚未实现/测试。

**静态分析（85 节点，已测）**：

| 指标 | 数值 | 状态 |
|---|---:|---|
| 观测有向边 | 7,602,554 | 已测 |
| 唯一有向边（LES） | 3,658,543 | 已测 |
| 重复率 | 51.9% | 已测 |
| 两段桶条目 | 3,822,238 | 已测 |
| 多重度 | 1.045 | 已测 |
| 根作用域平均切片出度 | 36.59 | 已测 |

### 5.2 估算数据（Estimated）

| 指标 | 数值 | 状态 |
|---|---:|---|
| LES level-0 条目与共享向量空间 | ~33 MB | 估算 |
| B4 双语义完整空间 | ~166 MB | 估算 |
| LES 压缩比（vs B4 双语义） | ~5.0× | 估算 |

### 5.3 待测数据（To Be Measured）

| 实验 | 目的 | 状态 |
|---|---|---|
| LES Exact 图搜索吞吐/召回 | 验证调度阈值 $\tau$ 的必要性 | 待测 |
| LES 构建时间拆解（base graph + dedup） | 公允对比 B4 | 待测 |
| 真实 CCS 数据标签分布 | 验证 H1 假设 | 待测 |
| LES 动态更新（插入/删除） | 评估增量维护成本 | 待测 |
| 1M 规模扩展性 | 评估大规模性能 | 待测 |

### 5.4 作图数据

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

---

## ⑥ 尚缺实验清单

1. **LES Exact 图搜索**：实现并测试 Exact$(u)$ 的图搜索路径，标定调度阈值 $\tau$；
2. **构建时间拆解**：分别测量 LES 的 base graph 构建时间（复用 B4）与 edge dedup 时间，与 B4 的 57 s 公允对比；
3. **真实 CCS 数据**：获取 ACM CCS 论文真实标签分布，验证 25% 主标签落中间节点的假设；
4. **动态更新**：实现插入/删除操作，测量增量维护成本；
5. **1M 规模**：在 SIFT1M 上测试 LES 的扩展性；
6. **Theorem 1 完整证明**：补充 $\mathrm{MA}(e)\subseteq\mathrm{Desc}^{+}(u)\iff i,j\in\mathcal{S}_{\text{rec}}(u)$ 的严格证明；
7. **Exact 切片等价性**：证明 Exact 语义的边归属逻辑与切片正确性；
8. **beam search 接口**：定义并测试结果不足 $k$ 时的返回行为。

---

**审稿结论**：草稿在问题定义与实验数据上有价值，但术语包装过度、定理证明存在漏洞、实验主张超出已测数据。修正后可作为 workshop 或短文投稿；补充缺失实验与证明后可达 SIGMOD/VLDB 标准。
