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

![DAG 属性的继承、交叠与多标签删除](figures/dag-attribute-scope-outlined.svg)

图是五概念示意模型。$C$ 的两个父为 $A,B$；对象 $p4$ 同时标注 $A,C$，删除 $C$ 标签后仍匹配 $A$。有限集合核对见[数据](data/dag-attribute-scope-check.json)。SVG 标签使用字形路径；[文字源图](figures/dag-attribute-scope.svg)保留编辑能力，背景透明。

## 2. 索引存储

记 $D=|V|$、$N$ 为对象 ID 容量、$W=\lceil N/64\rceil$。

| 组件 | 持久状态 | 用途与空间口径 |
|---|---|---|
| 概念邻接与闭包 | parents/children；去重后的 Anc⁺、Desc⁺ 概念 ID 列表 | 将逻辑匹配转换为可达性；闭包 ID 空间与 $\sum_c|Anc^+(c)|$ 成比例，最坏二次 |
| 直接成员倒排 | $P(c)=\{i:c\in L_i\}$，当前探针为排序 `vector<uint32_t>` | 枚举直接成员；ID 载荷 $4\sum_c|P(c)|$ 字节，另计容器开销 |
| 精确基数 | $C(u)=|S(u)|$ | 单概念范围判断；一般并集只能取得界，不能免费得到精确交叠量 |
| 可选 scope 位图 | $M(u)=S(u)$，仅部分概念持久物化 | 每张 $8W$ 字节；替代重复的后代 posting 展开 |
| 物理分区映射 | $b(i)$，对象到唯一分区 | 标签变化后保持不动；使几何图维护与属性变化解耦 |
| 分区几何图 | 每个非空分区一张普通 HNSW | 每对象只加入一个分区图；查询使用精确成员位图过滤 |

候选位图、每分区合格数量与查询结果堆为查询工作区，不是另一类 ANN 索引。全局过滤 HNSW 是实验对照；单个分区方案不要求同时保存全局图。

### 2.1 初始分区

从根计算每概念的最短路径深度 $depth(c)$，选择分组深度 $h$。定义概念的分区键

\[
g_h(c)=\begin{cases}
c,&depth(c)<h,\\
\min\{u\in Anc^+(c):depth(u)=h\},&depth(c)\ge h.
\end{cases}
\]

最小值按固定概念 ID 取，消除多父归属歧义。单直接标签对象以 $g_h(c)$ 为初始分区；多标签设计可取各标签分区键中的最小值。该选择只是唯一物理归属规则，逻辑继承仍使用全部标签和全部路径。

DAG 的最短深度层不必是 antichain，也不是分隔所有路径的 frontier。查询不能只按一条父链定位图；必须覆盖所有实际含合格对象的物理分区。分区总槽数包含空槽，应同时报告非空图数及成员尺寸分布。

$h$ 增大通常形成更多小图，但不保证每个 DAG、每次更新后都单调改善时间。细分的收益须与 fanout、局部图搜索、归并、构建成本和多图常数开销共同计量。

### 2.2 选择性物化

当前探针以

\[
\widehat E(u)=|Desc^+(u)|+\sum_{c\in Desc^+(u)}|P(c)|
\]

作为展开工作量的**启发式代理**，与 $W$ 比较，按代理值由大到小在预算 $B_m$ 内选取物化点。这里数的是概念访问与对象 ID 访问，不是稀疏位图 word 数；两类操作的 CPU 常数不同，$\widehat E(u)>W$ 不构成最优性定理或必然加速条件。

窄 scope 直接遍历短 posting 可能比读整张 $N$ 位位图便宜。实际选择应由独立训练负载校准，不使用测试查询 GT。物化仅改变合格集合的取得方式，不改善 HNSW 本身的近邻保证。

## 3. 查询算法

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

必须处理全部查询条件。当前分区定位会枚举合格对象，代价为 $O(|F_Q|)$；不能把这部分放在完整查询计时之外。若存在一般属性谓词，其逐对象计算或独立属性索引成本同样计入。

精确基数只能给出

\[
\max_{u\in U}C(u)\le |S(U)|\le\min\left(N_{live},\sum_{u\in U}C(u)\right).
\]

上界小于扫描阈值只允许预先决定走扫描，仍需枚举、去重并检查属性；计数不能替代候选集合。

**覆盖性质。** 每个合格对象位于恰好一个被访问的分区，不因多父结构漏分区。若各分区提供精确局部 top-k，则它们的并集包含全局精确 top-k：全局答案在其所在分区的局部名次不超过 k。使用近似局部搜索时，该结论只适用于候选覆盖结构，不能转化为 ANN 的 Recall 保证。

**过滤性质。** hnswlib 的 callback 限制结果候选，不代表不合格点不参与导航或不计算距离。该搜索不是 ACORN 的二跳谓词子图遍历。

## 4. 更新约束

### 4.1 标签增删

设对象的直接标签由 $L$ 改为 $L'$。分别计算旧、新继承集合 $A,A'$，只在成员资格真正变化时修改派生状态：

\[
\Delta^- = A\setminus A',\qquad\Delta^+=A'\setminus A.
\]

先更新发生变化的直接 posting；对 $\Delta^-$ 中概念的精确基数减 1 并清除已物化位图中的对象位，对 $\Delta^+$ 做对称操作。$A\cap A'$ 保持不变。多条路径与多个直接标签均不能导致重复增减。

单直接标签重标注 $c\to c'$ 需要触及的继承状态数为 $O(|Anc^+(c)\cup Anc^+(c')|)$；已排序闭包可用线性归并计算差分，当前维护探针逐项二分检查对方闭包，额外有对数比较成本。多标签使用显式旧/新集合时，闭包枚举为 $O(\sum_{c\in L\cup L'}|Anc^+(c)|)$，另计集合去重成本。若要把多标签对象的单标签变化收紧到该标签祖先数，需要额外的每对象祖先支持计数状态，当前探针不提供该状态。

排序 vector posting 的插删可能移动 $O(|P(c)|)$ 个 ID，因此上述祖先界仅约束计数/位图内核，不是完整更新时间界。物理分区 $b(i)$ 与几何图邻接不因标签变化而变。

### 4.2 其他事件

| 事件 | 触及的状态与限制 |
|---|---|
| 普通属性更新 | 写属性记录；若另有属性索引，维护成本另计 |
| 向量修改 | 仅所在分区的向量/图维护；不能按元数据更新成本计量 |
| 对象插入 | posting、继承成员、一个分区图；ID 容量与位图扩容成本另计 |
| 对象删除 | 清除 live/直接与派生成员，图标记删除；压实或图修复不是免费操作 |
| 概念显示名修改 | 稳定 ID 不变时，只改显示映射 |
| 分类边增删 | 检查无环性，更新受影响可达闭包、精确基数与物化位图；其他路径的支持必须保留 |
| 重分区或 full repack | 重新决定对象位置并重建涉及的几何图；不作为查询实验前置条件 |

固定物理归属避免标签变化触发图重建，但更新后的类目成员可能分散在更多分区。该 fanout 漂移需要长期工作负载验证；不能同时宣称零移动和永久保持初始类目共置。分类边变化即使采用在线闭包，也不能忽略已存基数与物化成员的失效。并发快照、崩溃恢复及任意容量扩张未在探针中验证。

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
