# DAG 层级属性约束下的分区向量近邻检索

## 摘要

研究对象是具有 DAG 层级属性的向量集合。直接标注细粒度概念的对象满足全部祖先条件；对象可有多个直接标签，多条继承路径与多个查询条件均按对象 ID 去重。方案由**概念邻接与闭包、直接成员倒排表、可选的 scope 物化位图、对象到固定物理分区的映射，以及每分区一张 HNSW**构成。查询精确解析过滤集合，小集合精确扫描，其余搜索全部含合格对象的分区并合并结果。

设计约束是：标签变化更新元数据，不强制移动向量或重建几何图。初始分区利用概念层级聚集成员；标签更新后保留对象的物理分区。由此换取维护局部性，但查询 fanout 可能随更新增长。该结构使用常规倒排、位图和 HNSW；是否具有新的算法贡献，需要独立于实现优化检验。

## 1. 数据模型与查询

属性层级为有根 DAG $G=(V,E)$，边由一般概念指向细化概念。$u\preccurlyeq v$ 表示 $u=v$ 或存在从 $u$ 到 $v$ 的路径。自反可达闭包为

\[
\operatorname{Anc}^{+}(v)=\{u:u\preccurlyeq v\},\qquad
\operatorname{Desc}^{+}(v)=\{w:v\preccurlyeq w\}.
\]

对象 $o_i=(id_i,x_i,L_i,a_i)$ 具有向量 $x_i\in\mathbb R^d$、直接标签集 $L_i\subseteq V$ 与其他属性 $a_i$。继承标签集为

\[
A_i=\bigcup_{c\in L_i}\operatorname{Anc}^{+}(c).
\]

概念条件 $u$ 的 scope 为 $S(u)=\{i:live_i\land u\in A_i\}$。查询 $Q=(q,U,\varphi,k)$ 采用概念条件的并集：

\[
F_Q=\{i:live_i\land A_i\cap U\ne\varnothing\land\varphi(a_i)\}.
\]

$U$ 可删去被祖先条件覆盖的冗余概念，但不能因为已取得足够候选而忽略尚未处理的条件。输出 $F_Q$ 内按原始 FP32 squared-L2、对象 ID 次序取前 $\min(k,|F_Q|)$ 个对象；过滤与去重精确，近似性仅来自 ANN 的近邻选择。

非空查询 Recall 的分母固定为 $\min(k,|F_Q|)$，返回不足不缩小分母。macro Recall 对非空查询等权；pair Recall 为全部命中数除以全部精确答案数。空查询单独计数，不计作 Recall=1。完整查询计时包括工作区建立、范围解析、基数统计、分区定位、搜索与归并；GT、结果核验、日志及构建单列。

### 1.1 DAG 结构的作用

- $u\preccurlyeq v\Rightarrow S(v)\subseteq S(u)$，因此粗粒度条件覆盖细粒度条件。
- 互非祖先的概念也可能共享后代，因而 scope 可以相交。
- 直接标注内部概念，只满足它及其祖先，不能推出属于某个特定后代。
- 概念闭包按可达集合去重；成员并集再按对象 ID 去重。

下文用同一组七概念、八对象贯穿构建、查询与更新；图 1 展示保存的索引，图 2 展示查询工作区和执行过程，图 3 展示标签更新触及的状态。图中的对象 ID、分区名和计算结果与正文一致。

## 2. 索引结构与构建样例

### 2.1 贯穿全文的八对象例子

概念 ID 固定为 R=0、A=1、B=2、C=3、D=4、E=5、F=6。DAG 的边为 R→A、R→B、R→F、A→C、A→D、B→C、B→E。**C 同时有父概念 A、B**。对象按下表编号；本例每对象只有一个直接标签，全部存活，其他属性谓词为 true。

| 对象 ID | 向量 $x_i$ | 直接标签 | 继承概念集 | 初始物理分区 |
|---|---|---|---|---|
| o0 | (0,0) | A | {R,A} | b_A |
| o1 | (1,0) | C | {R,A,B,C} | b_A |
| o2 | (2,0) | D | {R,A,D} | b_A |
| o3 | (0,2) | C | {R,A,B,C} | b_A |
| o4 | (3,0) | B | {R,B} | b_B |
| o5 | (0,3) | E | {R,B,E} | b_B |
| o6 | (4,4) | F | {R,F} | b_F |
| o7 | (5,5) | R | {R} | b_R |

取分组深度 h=1；同时具有 A、B 祖先的 C 以较小稳定 ID 的 A 为初始分区键。因此 o1、o3 满足 B 条件，却存放在 b_A。这正是图 2 中查询 B 需要跨分区的原因。

### 2.2 图 1：保存了什么，记录之间怎么连接

![图1：概念元数据、直接倒排、物化成员与四张分区HNSW的实际组织](figures/dag-index-layout-outlined.svg)

**图 1 阅读顺序：**左侧概念 DAG 决定匹配语义；倒排和可选位图把条件变成对象 ID；右侧的对象位置表把 ID 映射到物理分区；分区表指向各自的 HNSW。概念边不承担向量图导航。

图中 `b_R0`、`b_A1`、`b_B2`、`b_F3` 的末尾数字是物理分区编号 0、1、2、3；正文分别简称 b_R、b_A、b_B、b_F。F 的概念 ID 为 6，分区号却为 3，二者不能混用。

记 $D=|V|$、$N$ 为对象 ID 容量、$W=\lceil N/64\rceil$。持久记录和查询临时对象的边界如下。

| 数据结构 | 键 → 保存的值 | 本例中的读取 | 源码与状态 |
|---|---|---|---|
| 概念邻接/闭包 | 概念 ID → parents、children、Anc⁺、Desc⁺ 的去重概念 ID 列表 | `Desc[B]=[B,C,E]`；`Anc[C]=[R,A,B,C]` | `Graph`；概念 ID 不是对象 ID |
| 直接倒排 `P` | 概念 ID → 排序的全局对象 ID 列表 | `P[B]=[o4]`，`P[C]=[o1,o3]`，`P[E]=[o5]` | `Workload.postings`，当前为 `vector<uint32_t>` |
| 精确基数 `C` | 概念 ID → scope 内不同对象数 | `C(B)=4`，不是直接成员数 1 | 更新探针维护 `Strategy.cardinality`；ANN 查询探针使用 `scope.count()`，未借助持久计数规划 |
| 可选物化 `M` | 已选概念 ID → 全局对象 ID 位图 | `M(B)` 的位 1、3、4、5 为 1 | `mat_all` / `mat_select`；只存成员位，不存向量或图边 |
| 对象位置表 `b` | 全局对象 ID → 唯一物理分区编号 | `b(o1)=b_A` | ANN 探针的 `part_of`；查询时可直接按 ID 查位置 |
| 分区目录 | 分区编号 → 该分区的 HNSW 实例 | `part_graphs[b_A]` 含 o0、o1、o2、o3 | 每个非空分区一个 `HierarchicalNSW` 实例 |
| 查询工作区 | 本次条件 → `scope`、`eligible`、每分区合格数、候选结果 | B 查询得到 {o1,o3,o4,o5} 和 A:2、B:2 | 查询时创建/填充；不是新的持久 ANN 结构 |

为展示物化读取分支，本例显式保留 M(B)，不表示八对象时启发式一定会选择 B。位图在图中从左到右按 **o0,o1,…,o7** 排列；`01011100` 是这个展示顺序，不按二进制整数的高低位解释。直接倒排 P(B) 只有 o4；持久 M(B) 包含 B、C、E 的全部成员；临时 eligible 则还需要结合本次其他属性与 live 状态。

#### 一张分区 HNSW 内部保存什么

本例一共七个概念、四个非空物理分区、四张 HNSW。C、D、E 有直接倒排，但不各自再建立一张图。

在 b_A 内，按全局 ID 顺序插入的四条节点记录为局部 ID 0→o0、1→o1、2→o2、3→o3。每条 HNSW 节点记录包含：

- **局部 ID**：本图内部编号，邻接表中的邻居用这一编号寻址；不同分区的局部 ID 可以相同。
- **外部 label**：全局对象 ID，取到节点后用它检查 eligible 位并识别返回对象。
- **向量 payload**：用于计算距离的 FP32 数组。
- **节点层数及各层邻接表**：HNSW 随机层上的局部邻居 ID；每张图另有入口节点等元数据。

因此，图搜索的寻址链是“分区表 → 图入口/局部邻居 → 节点向量与外部对象 ID”。HNSW 的随机导航层与概念深度分别生成、互不等同。图 1 下方直接展开本例实际构建的 b_A：入口为 local 0/o0，max_level=0，四个节点在 L0 两两连接；local 1/o1 的邻居局部 ID 为 [0,2,3]、payload 为 (1,0)。这是八对象实例的实际邻接，不假定一般 HNSW 为完全图，也不把绘图位置当作向量坐标。

小集合扫描从 `base.data[id × dim]` 读取向量；HNSW 路径从本图节点 payload 读取。当前探针保留 base 数组供扫描/GT，hnswlib 还在节点中复制向量 payload。**“一个对象只加入一张分区图”不等于整个进程只保留一份向量字节**。全局 HNSW 仅是对照臂，不是分区方案的必需组成。

### 2.3 构建算法：先建成员元数据，再建四张图

从根计算最短路径深度，定义分组键

\[
g_h(c)=\begin{cases}
c,&depth(c)<h,\\
\min\{u\in Anc^+(c):depth(u)=h\},&depth(c)\ge h.
\end{cases}
\]

```text
Build(G, labels, vectors, h, selected_materializations):
    compute unique Anc⁺ and Desc⁺ concept lists
    for object i:
        append i to P(c) for every direct label c of i
        b(i) ← minimum g_h(c) over its direct labels
    create one HNSW for each nonempty physical partition
    for object i in global-ID order:
        addPoint(HNSW[b(i)], vector=x_i, external_label=i)
    for selected concept u:
        M(u) ← union of P(c) for unique c in Desc⁺(u)
    C(u) ← number of distinct live members in S(u), if counters are maintained
```

本例在 h=1 处产生 b_R、b_A、b_B、b_F 四个非空分区。o7 直接标注根，因此留在 b_R；它不会被推断成 A、B 或 F 的后代成员。多直接标签的分区键取最小值属于方案定义，本例和当前查询探针验证单直接标签。

DAG 的最短深度层不一定是 antichain，也不是分隔全部路径的 frontier。分区键只决定初始唯一物理位置；完整匹配始终使用全部可达祖先。h 增大通常产生更多小图，但查询还需支付 fanout、图搜索和归并成本，不保证时间单调改善。

### 2.4 选择性物化的角色

当前探针以

\[
\widehat E(u)=|Desc^+(u)|+\sum_{c\in Desc^+(u)}|P(c)|
\]

作为展开工作量的启发式代理，与 W 比较，在预算内按代理值选点。这里数的是概念访问与对象 ID 访问，不是稀疏位图 word；两种操作常数不同，该规则没有最优性保证。

无 M(B) 时查询合并 P(B)、P(C)、P(E)，有 M(B) 时读取同一成员位图；**两条路径进入同一分区目录、搜索相同的图**。物化优化范围解析，不改变几何图，也不提供额外近邻保证。

## 3. 查询算法与逐步样例

### 3.1 通用查询

```text
Search(q, U, predicate, k):
    scope ← empty object bitmap
    for u in U:
        if M(u) exists:
            scope ← scope OR M(u)
        else:
            for c in unique Desc⁺(u):
                add every ID in P(c) to scope
    eligible ← live AND scope AND predicate
    e ← popcount(eligible)
    if e = 0: return empty
    if e ≤ τ: return exact top-k over all eligible IDs
    count[b] ← 0 for every physical partition
    for i in eligible:
        count[b(i)] += 1
    candidates ← empty
    for b with count[b] > 0:
        kb ← min(k, count[b])
        efb ← min(count[b], max(efSearch, kb))
        candidates += filtered HNSW(b, q, eligible, kb, efb)
    return top-k of candidates by (squared-L2, object ID)
```

候选位图告诉图搜索“哪些对象可以返回”；位置表告诉调度“哪些物理图必须搜索”。二者不同，且分区定位当前需要枚举合格 ID，代价为 O(|F_Q|)。候选已知并不意味着需要对其全部向量计算距离：大集合通过图搜索减少距离计算，小集合直接扫描。

### 3.2 图 2：查询 B，为何要搜索 b_A

设 q=(1,1)、U={B}、k=2、τ=2、efSearch=20。以下均使用 squared-L2。

![图2：从B条件到跨分区局部近邻与全局归并，并展示小集合扫描分支](figures/dag-query-walkthrough-outlined.svg)

按图中编号读：

1. **范围解析。** B、C、E 的直接成员合并为 {o1,o3,o4,o5}；物化 M(B) 得到相同集合。该步骤仅处理 ID，不计算向量距离。
2. **计划分流。** 合格数量为 4，大于示例阈值 2，走分区图路径。其他属性为 true，因此 scope 与 eligible 相同；有额外属性时必须在这一步前完成其筛选。
3. **按真实位置分发。** o1、o3 在 b_A，o4、o5 在 b_B；b_R、b_F 的合格计数为 0，跳过。只搜名称为 B 的分区会漏掉共享后代 C 的对象。
4. **局部图检索。** b_A 返回 (o1,1)、(o3,2)，b_B 返回 (o4,5)、(o5,5)，括号内为对象和距离。b_A 中的 o0、o2 仍是图节点，但不能进入答案；callback 不保证它们不参与导航或距离计算。图中展示局部输出，不虚构 HNSW 的逐边访问顺序。
5. **全局归并。** 对局部候选按 (距离,对象 ID) 排序，得到 o1、o3。两个局部 top-2 不等于各取一个结果，最终答案可以全部来自同一分区。

每个分区只需返回至多 k 个候选：若局部结果精确，全局 top-k 对象在所在分区的局部名次不超过 k。实际 HNSW 可能漏近邻，该覆盖论证不构成一般 ANN Recall 保证。

### 3.3 同一索引的另两种查询

**小集合：U={D}。** 只有 P(D)={o2}，合格数 1≤τ；直接算 d²(q,x₂)=2，返回一个对象 o2，虽然 k=2 也不补入不合格对象。此路径不需要调用任何 HNSW。当前计时探针为了收集结构 fanout 仍统计合格分区，该诊断开销不属于扫描算法的必需步骤。

**交叠并集：U={A,B}。** A、B 各包含 o1、o3，最终并集是 {o0,o1,o2,o3,o4,o5}，不是八个成员。构造 scope 时按对象位或，o1、o3 各置位一次。精确单概念计数只给界

\[
\max_{u\in U}C(u)\le |S(U)|\le\min(N_{live},\sum_{u\in U}C(u)).
\]

本例上下界为 4 和 8，实际并集为 6。计数不能替代候选枚举，也不能在候选“够多”时忽略尚未处理的查询概念。

## 4. 标签更新算法与逐步样例

### 4.1 通用维护规则

直接标签由 L 改为 L′，旧、新继承集合为 A、A′。先计算去重后的成员变化：

\[
\Delta^- = A\setminus A',\qquad\Delta^+=A'\setminus A.
\]

```text
Relabel(i, Lnew):
    Aold ← union of unique Anc⁺(c) for c in Lold
    Anew ← union of unique Anc⁺(c) for c in Lnew
    remove/add i only in direct postings whose labels changed
    for u in Aold minus Anew:
        C(u) -= 1; clear bit i in M(u) if materialized
    for u in Anew minus Aold:
        C(u) += 1; set bit i in M(u) if materialized
    write new direct labels
    keep b(i), vector payload and HNSW adjacency unchanged
```

共有祖先 A∩A′ 保持不变。多条路径与多标签不能重复增减同一成员；例如删除一个直接标签，若另一标签仍支持祖先条件，该祖先成员必须保留。

### 4.2 图 3：o6 从 F 改标为 B，物理位置不动

![图3：标签更新只改变成员元数据，固定物理位置使查询B新增一个被访问分区](figures/dag-label-update-outlined.svg)

图 3 延续同一八对象实例，仅将 o6 的直接标签由 F 改为 B：

1. **继承差分。** {R,F}→{R,B}，因此只删除 F 的匹配、增加 B 的匹配，根 R 的基数仍为 8。
2. **直接成员。** 从 P(F) 删除 o6，向 P(B) 插入 o6。概念 DAG、Anc⁺、Desc⁺ 不变。
3. **派生状态。** C(F) 从 1 变为 0，C(B) 从 4 变为 5；已物化 M(B) 置位 o6，展示串 `01011100`→`01011110`。未物化的范围由更新后的直接 posting 在线求得。
4. **几何状态。** b(o6) 仍为 b_F，向量仍为 (4,4)，原 b_F 图及其他图不重建。**逻辑 S(F) 可以为空，而物理 b_F 仍有一个图节点**；分区名表示初始分组键，不是实时标签断言。
5. **后续查询。** B 的 eligible 多了 o6，位置统计使查询访问 b_A、b_B、b_F 三张图；o6 的距离为 18，本例全局 top-2 仍为 o1、o3。fanout 从 2 增至 3，这就是固定物理位置的维护收益与查询代价之间的取舍。

图中的变化数只描述触及的成员状态，不能当作完整更新时间。排序 vector posting 插删可能搬移 O(|P(c)|) 个 ID。

### 4.3 更新成本与其他事件

单直接标签 c→c′ 需要触及的继承状态数为 O(|Anc⁺(c)∪Anc⁺(c′)|)。已排序闭包可线性归并差分；当前维护探针逐项二分检查对方闭包，还存在对数比较成本。多标签显式重算集合时，闭包枚举为 O(Σ_{c∈L∪L′}|Anc⁺(c)|)，另计去重。若要把多标签对象的单标签变更收紧到该标签祖先数，需要额外每对象祖先支持计数，当前探针不提供。

| 事件 | 触及的状态与限制 |
|---|---|
| 普通属性更新 | 写属性记录；若有独立属性索引，其维护成本另计 |
| 向量修改 | 仅所在分区的向量/图维护，不按元数据更新成本计 |
| 对象插入 | posting、继承成员、一个分区图；ID 容量及位图扩容成本另计 |
| 对象删除 | live/直接与派生成员、图删除标记；压实或图修复不是免费操作 |
| 概念显示名修改 | 稳定 ID 不变时只改名称 |
| 分类边增删 | 检查无环性、维护受影响闭包/基数/位图，保留其他路径的支持 |
| 重分区或 full repack | 改变物理位置并重建涉及的几何图，单独计量，不作为查询实验前置条件 |

即使在线求概念闭包，分类边变化仍可能使持久基数与物化成员失效。并发快照、崩溃恢复、任意容量扩张未在探针中验证。样例输入、实际执行结果与复现命令见[样例核验](experiments/dag-worked-example.md)；该例验证集合与执行过程，不是性能实验。

## 5. 成本模型

令 $w$ 为在线展开实际访问的 posting ID 数，$d_c$ 为访问的概念数，$e=|F_Q|$，$f$ 为含合格对象的分区数，$C_b\le k$ 为每图返回数。忽略常数时：

\[
T_Q=O(W+d_c+w+T_\varphi+e+P)
+\sum_{b\in touched}T_{ANN}(b,Q)
+O\left((\sum_b C_b)\log(\sum_b C_b)\right).
\]

$P$ 是分区计数数组的初始化/遍历成本。物化可用每张 $W$ 个 word 的读取替代相应后代展开。小集合精确扫描的向量工作为 $O(e\cdot d_{vec})$，仍需承担范围解析成本。HNSW 未提供本设计可用的对数最坏时间或 Recall 保证。

以当前列表闭包、ID posting 与稠密物化位图计，持久空间包括

\[
O(D+|E|+\sum_c|Anc^+(c)|+\sum_c|Desc^+(c)|)
+4\sum_c|P(c)|+8D+8W|\mathcal M|+O(N)+M_{HNSW}.
\]

这些项不含容器常数与查询工作区。多图实现的锁表、入口及访问池开销须实测，不能仅按所有分区总对象数相同推断内存相同。

## 6. 文献定位与数据来源

| 工作 | 可迁移的具体结构或证据 | 与本方案的关系 |
|---|---|---|
| [Directory-Aware / TrieHI](https://arxiv.org/pdf/2606.16903v1)，§III–IV | 直接成员在线展开、祖先范围物化、稳定概念解析 | 范围解析/物化谱系的对照；本方案需独立检验 DAG 交叠与分区搜索成本 |
| [Curator](https://arxiv.org/pdf/2601.01291v3)，§3–4 | 共享几何聚类树与按标签分布的自适应粒度 | 分区来自向量几何；当前初始分组来自概念层级 |
| [ACORN](https://arxiv.org/pdf/2403.04871v1)，§5 | 谓词相关的图遍历与一/二跳扩展 | 当前普通 HNSW callback 不提供这些邻接/遍历机制 |
| [iRangeGraph](https://arxiv.org/pdf/2409.02571v1)，§3 | 数值范围的分层 elemental graphs 与按需选边 | 当前不组合跨层图边 |
| [Filtered-DiskANN](https://harsha-simhadri.org/pubs/Filtered-DiskANN23.pdf)，§3 | 标签参与剪边、标签图的边合并 | 当前每分区一张独立普通图 |
| [SciHTC](https://aclanthology.org/2022.emnlp-main.610/)，§2–3，pp.8924–8926 | 学术论文的层级分类数据、最高相关度标签路径 | C 类应用/数据背景：原问题是分类，不是 ANN 或 vector join |

SciHTC 的完整作者为 Mobashir Sadat、Cornelia Caragea，发表于 EMNLP 2022，pp.8923–8937，DOI [10.18653/v1/2022.emnlp-main.610](https://doi.org/10.18653/v1/2022.emnlp-main.610)，预印本 arXiv:2211.02810。原文将层级称为 tree，并在多个作者标签分支中只保留最高相关度的一条；§3 把使用全部分支列为 future work。项目以公开路径终点作为单直接标签，再用 CCS 稳定概念 ID 合并同一概念、计算全部可达祖先，从而构造 DAG 继承负载。这是本项目的负载语义，不能写成原论文已提供多父 ANN 基准。

SciHTC 原文 PDF 给出 1,233 个观测 topic；ACL 网页摘要写 1,234；本地完整 CCS 本体有 1,928 个真实概念。三者不是同一统计口径。全文数据含标题、摘要与关键词，作者 [README](https://github.com/msadat3/SciHTC#dataset-access) 要求申请，或使用获授权的 ACM proceedings 重建；当前本地只有公开 ID/标签，不具备自然文本向量。

## 7. 已测事实与证据范围

结构数据：[scihtc-dag-structure.json](data/scihtc-dag-structure.json)、[逐概念 CSV](data/scihtc-dag-concepts.csv)；脚本 [scihtc_dag_structure.py](experiments/scihtc_dag_structure.py)。

| 量 | 实测 |
|---|---|
| 概念 / 边 | 1,929（含虚拟根）/ 2,040 |
| 多个不同父的概念 | 103，约 5.34%；重复出现的 ID 共 159 个 |
| 标签行 / 可用行 | 186,160 / 168,937；含 `~` 排除 17,223；无法解析 0 |
| 每篇论文继承概念数 | 均值 4.213，最大 11 |
| 每概念祖先数 | 均值 5.035，p95 7，最大 16 |
| 每概念 scope | 均值 368.954，p95 1,072，最大 168,937 |
| 非空多父概念父 scope 的交并比 | 101 个概念；p50 0.0126，p95 0.528，最大 0.9011 |
| 启发式物化集合 | 39 概念，823,680 字节位图载荷 |

概念数与对象数是不同单位；两者的比值不能证明查询加速。$\sum|Anc^+|=\sum|Desc^+|$ 由可达对计数恒等。上述统计只说明小祖先闭包和大对象 scope 可以同时出现，端到端代价必须另测。

查询工作负载为 834 个非空单概念条件和 400 个随机兄弟概念组合，共 1,234 行。DAG 兄弟集合不保证为 antichain，合并仍按完整并集语义。实验中的对象 ID 固定种子打散，向量为前 168,937 行 SIFT128 FP32，并按打散后的对象 ID 绑定标签；标签与向量没有语义相关性。测得 Recall 仅描述该组合负载的几何近邻质量，不能外推真实学术语义检索。

当前有效查询与维护检查、命令和逐查询数据见[当日报告](report.md)。完整查询计时以外层计时器为准，不用各阶段计时之和替代。

## 8. 实现范围与可检验问题

- [dag_scope_ann.cpp](../../../codes/directory-aware-vector-attr-search/src/dag_scope_ann.cpp)：多父闭包、单直接标签、全局过滤图对照、可选分区图、小集合扫描与逐查询质量检查。
- [dag_scope_probe.cpp](../../../codes/directory-aware-vector-attr-search/src/dag_scope_probe.cpp)：单直接标签元数据解析与重标注检查。位图/计数维护内核与 posting 维护、独立核验分开计量。
- 一般多直接标签、任意属性谓词、向量动态插删、并发和持久恢复不在该查询探针的验证范围。§4 的多标签集合差分是方案定义，不能用单标签重放计时替代其实测。

待检验的问题是：在相同实际 Recall 层上，哪种分区粒度的完整查询更快；这种收益能否在自然向量—标签相关性上保持；固定物理归属经历标签变化后 fanout 如何漂移；选择性物化能否在独立负载上抵偿其维护与空间。当前尚未充分排查新颖性，也不把常规组件的组合直接称为新索引。
