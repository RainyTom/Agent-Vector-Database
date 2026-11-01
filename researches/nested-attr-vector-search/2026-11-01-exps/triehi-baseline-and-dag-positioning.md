# TrieHI 的基线选择，与把 NodeANN 降为执行器的方案定位

## 1. TrieHI 选了哪些基线

原文 §V-A "Compared Designs"（`paper.md:1038–1048`）：

> We compare the three designs analyzed in Sections III and IV. PE-ONLINE stores only the immediate path key and expands descendant paths at query time. PE-OFFLINE materializes ancestor memberships during ingestion so that recursive scopes can be resolved by a materialized path key. TRIEHI stores the hierarchy as a prefix tree and uses directory nodes as reusable scope objects. All three designs are integrated with two ANN executors in the Viking engine: a graph-based proximity graph (PG) index and a partition-based inverted file (IVF) index. **This lets us distinguish directory-semantic overhead from ANN-index behavior.**

结构是：**ANN 执行器固定（PG 与 IVF 两套），只替换目录元数据/scope 解析层**。三种表示分别对应展开式在线、物化式、原生前缀树。

另外两类对照与指标拆分（`paper.md:1049–1054`）：

- **无目录的纯向量索引**作 `Baseline (Vec.)`，只用于构建时间与索引大小（Table V，`paper.md:1480–1500`）。
- 指标为 nDCG@k / Recall@k、端到端延迟，以及独立的 **directory-only latency**——"the time required to resolve a directory predicate into a candidate entry-ID set before ANN ranking"（`paper.md:1051–1053`），Table IV 单独报告该表（`paper.md:1237`）。

因此它的实验逻辑是**分层归因**：先单独测 scope 解析，再测端到端，再测构建/空间，再测 MOVE/MERGE 维护。这与"把整个系统与某个先进过滤索引比一次总延迟"是两种不同的论证方式。

## 2. 为什么没把多属性过滤索引当基线

分三类证据，必须区分"原文说的"和"我们推断的"。

### 2.1 原文确实知道这些工作

§VI Related Work 的 Filtered Vector Search 段落（`paper.md:1806–1820`）列了 decoupled systems/optimizers、按谓词选择率选择执行策略、以及针对 tags、ranges 的耦合索引。参考文献含 ACORN（[80]）、SIEVE（[81]）、属性过滤实验研究（[82]，arXiv:2508.16263）、Filtered-DiskANN（[87]）。不能说作者不了解。

### 2.2 原文给的理由是问题定位，不是"跑不动"

- 引言（`paper.md:85–87`）：现有 metadata filtering 机制"are effective for flat attributes, but they do not model the hierarchical topology that defines many real retrieval scopes"。
- 引言（约 `paper.md:96`）："Flattening paths into scalar metadata discards this topology."
- §II 定位（`paper.md:201–203`）："Our goal is therefore not to replace ANN indexes, but to provide a directory-semantic scope-resolution layer that can feed valid candidate sets to existing vector-search executors."
- §VI（`paper.md:1812–1817`）："Directory constraints can be approximated with scalar path filters, but existing vector databases typically treat paths as flat attributes rather than hierarchy-aware constraints. Exact-parent predicates can be evaluated by equality filtering, while recursive scopes and namespace mutations must be encoded through additional metadata expansion or application-side coordination."
- 贡献 1（`paper.md:183` 附近）明确说目标是 "clarifying the requirements that cannot be represented by flat metadata filters alone"。

串起来的论证是：**递归目录范围不是一个平坦标量谓词**；即使后端是过滤 ANN，候选集仍由目录层产出。所以真正待比较的是"目录表示"，而不是"哪家过滤 ANN 更强"。PE-ONLINE/PE-OFFLINE 正是"把路径当平坦属性、需要展开或额外物化"这一路线的代表。

### 2.3 原文没有写的部分（不能替它补）

- 全文没有"我们不与 ACORN/Filtered-DiskANN 比较，因为……"这类排除说明；"Compared Designs" 只有一句"compare the three designs"。
- 没有证明这些系统难以或无需与目录层组合，也没有实验说明它们在递归 scope 上会输。
- 两个数据集的查询只有目录约束，没有"目录 + 独立标量属性"的混合负载（`paper.md:986–1037`）。**推断**：在这种纯目录 scope 下，多属性过滤索引的强项（谓词选择率与混合谓词规划）没有可发挥的场景；但作者未陈述这一点。
- 作者自己在结论把 "cost-aware planning for mixed semantic and hierarchical constraints" 列为 future work（`paper.md:1865–1867`），等于承认混合条件尚未处理。

### 2.4 最关键的一条：它把 DAG 压成了树

WIKI-Dir 的构造（`paper.md:990–993`）：

> Since Wikipedia categories form a directed acyclic graph, we construct a canonical tree by selecting one category path per entry and discarding invalid links.

ARXIV-Dir 是 subject 与 temporal 两个**独立**命名空间（`paper.md:1004–1008`），不是多父 DAG。

所以 TrieHI 的问题模型里**没有多父**：每个 entry 一条规范路径，`Inc(v)=Local(v)∪⋃Inc(child)` 在树上成立。它的结构、数据集与真值都是树一致的。**多父 scope 交叠不在其表示能力之内，也就不会进入基线讨论。**

## 3. 这对我们意味着什么

用户的判断成立：现在的"合并组"只是 NodeANN 上的分组规则，本身不构成贡献。TrieHI 的方法论提供了正确的参照结构，但**不能照抄它的结论**，因为语义不同。

### 3.1 采纳的部分：固定执行器，只变元数据层

- **ANN 执行器：NodeANN**（每个非空概念一张局部图）→ 作为方案**组件**，不再是必须打败的对手。
- **贡献层放回 DAG 侧**：多父 scope 的解析、以及"给定合格集合后，需要启动哪些局部图、每图给多少预算、如何保证不漏"的调度。
- 这样"合并组"降级为一个可选的分组/规划配置，可由实验淘汰。

### 3.2 不采纳的部分：树化

把 DAG 压成 canonical tree 会**改变答案集合**，不只是性能差异：多父概念下的对象只保留一条路径，就会从其他父 scope 中漏掉。本地实测 12,913 篇论文（可用行 7.6%）的继承集合含至少一个多父概念。因此在 DAG 负载上，TrieHI 式树不是"更快的同类方案"，而是**不同语义**；比较必须先把真值口径写清楚，不能直接判输赢。

### 3.3 因此可陈述的差异

| 维度 | TrieHI | 本项目候选 |
|---|---|---|
| 层级语义 | canonical tree，每 entry 一条路径 | 保留 DAG 多父，scope 可交叠 |
| 元数据层 | 前缀树节点 + inclusive 集合 | 待定：DAG 闭包 + 直接 posting + 可选物化 |
| ANN 层 | 单执行器（PG/IVF），全局候选集 | **NodeANN 多局部图**，需要跨图调度与归并 |
| 主要代价 | scope 解析、空间 | 额外增加 fanout、跨图归并、图成员漂移 |
| 维护 | DSM MOVE/MERGE 树操作 | 多父成员关系变化 + 图成员是否迁移 |

多局部图是本项目**独有于 TrieHI 的复杂度来源**：TrieHI 解析出一个全局候选集就交给单个执行器；NodeANN 下候选集分散在多张图里，需要决定搜索哪些图、每图返回多少，并且要证明不漏。作者把混合条件的代价规划列为 future work，但没有多局部图这一层。

## 4. 可执行的对照骨架

固定：同一 DAG、同一向量、同一距离核、同一 k/efSearch、同一次构建预算、全驻内存。

| 臂 | scope 解析 | ANN 执行器 |
|---|---|---|
| B0 | 直接 posting + 在线展开（PE-ONLINE 类比） | NodeANN |
| B1 | 物化 inclusive 集合（PE-OFFLINE 类比） | NodeANN |
| B2 | DAG→canonical tree（TrieHI 类比，真值需按树语义单列） | NodeANN |
| B3 | DAG scope 解析 + 跨图调度（候补方案） | NodeANN |
| B4 | 全局过滤 HNSW（历史对照，仅作参照） | 单图 |

指标必须分开记：scope 解析、调度决策、各图搜索、归并、端到端；质量用非空 macro 与 pair Recall；机制用节点/边扩展数、距离计算数、启动图数；另记构建大小与维护操作数。

现有 `dag_scope_ann` 已具备 B0/B1 两臂与 B4 对照，B2 需要新增树化臂，B3 需要新增调度臂；工作负载沿用现有 SciHTC/CCS 数据，不需要新数据。

## 5. 尚未成立的部分

- "跨图调度"本身不是新颖点：引用[82]一类的属性过滤实验研究与选择率驱动执行策略研究已覆盖按选择率选执行计划的思路。可检验的增量必须落在 **DAG 结构特有的可利用信息**（交叠、闭包、多父影响）以及**不漏的覆盖条件**上。
- 本文没有证明 TrieHI 不可与过滤 ANN 组合，也没有证明 B3 会赢；这仍是假设。
- 新颖性尚未充分排查；不能据本节把方案称为新索引。

## 6. 证据位置

- 原文：`papers/prefix-vector-search/2606.16903-directory-aware/paper.md`（§I 85–96、§II 201–203、§V-A 986–1054、Table IV 1237、Table V 1480–1500、§VI 1785–1820、结论 1865–1867）。
- 阅读记录：`papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md`。
- 本项目 DAG 结构实测：`researches/nested-attr-vector-search/2026-11-01-exps/data/scihtc-dag-structure.json`。
- 现有 NodeANN 式与合并组对照：`data/nodeann-grouping-work.json`。
