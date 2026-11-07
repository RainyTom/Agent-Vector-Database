# TrieHI（arXiv:2606.16903）技术解读：问题定义、方案与实验

核查基础：`paper.md` 全文提取（2026-10-19，PyMuPDF）。章节定位均指原文 PDF 页码。
一句话定位：把**层级目录语义**做成向量数据库的原语，提出 DSQ/DSM 两个操作形式化，比较路径展开（PE-Online/Offline）与原生前缀树（TrieHI）三种 scope-resolution 设计；是 filtered ANN 的 scope 层，不改 ANN 布局。

## 1. 问题定义

### 1.1 动机（§I，pp.1–2）

向量数据库把元数据当 flat scalar attributes，表达不了代码库、企业文档、agent 记忆常用的层级目录语义（如 `/src/core/payment.py` 中 `/src/`、`/src/core/` 前缀编码的结构上下文）。后果：目录范围检索和结构更新只能在应用层 workaround——递归 scope 解析昂贵（读路径），MOVE/MERGE 退化为大量文档/路径级更新（写放大 + 一致性风险）。根因：目录结构放在向量库之外。

### 1.2 执行模型（§II-A，p.2）

向量库执行模型：查询向量经 ANN 索引（PG 或 IVF）排序取 top-k；元数据过滤把标量谓词解析成合法 entry 集合，执行器只排候选。目录条件可视为一种 **scope predicate**，关键区别是谓词是拓扑的而非 flat：解析一条 path 可能要遍历祖先、后代或兄弟分支。**目标不是替代 ANN 索引，而是提供 directory-semantic scope-resolution 层**，把候选集喂给现有执行器。论文实现集成在字节 Viking 引擎（提供 PG/IVF）。

### 1.3 DSQ 原语（§II-C，pp.2–3）

DSQ（Directory-Semantic Query）= 读路径，把目录约束转成向量搜索的候选 scope。两个基元：

- **Recursive**：搜索目标目录下所有条目，含后代目录。锚在 `/HR/` → `{doc_1, doc_2}`；锚在 `/HR/Policies/` → 只含 doc_2，排除 `/Archive/HR/` 下的 doc_7。
- **Non-recursive**：只搜直接绑定到目标目录的条目。`/HR/` non-recursive → 只含 doc_1，排除子目录中的 doc_2。

派生操作可规约到这两个基元：exclusion = 减去某分支的 recursive scope；sibling/ancestor 查询 = 目录遍历 + 基元 scope 解析的组合。论文设计与评估聚焦两个基元。

### 1.4 DSM 原语（§II-C，p.3）

DSM（Directory-Semantic Maintenance）= 写路径，变更目录拓扑且保持后续 DSQ 的命名空间语义。两个代表操作：

- **MOVE**：整棵子树迁到新父节点，子树内部结构不变，但全部后代命名空间改变。
- **MERGE**：源子树并入已存在的目标子树，名字冲突递归调和（如 `/Dept_A/OKR/` 并入 `/Dept_B/OKR/`，doc_8/doc_9 汇合）。

创建、删除、重命名等生命周期操作是这两个原语的更简单变体。核心挑战：**一个逻辑命名空间操作可能影响大量后代**。

### 1.5 四条设计需求（§II-D，p.3）

1. **Scope correctness**：DSQ 在向量排序前解析出正确的 recursive/non-recursive scope。
2. **Query efficiency**：解析不能每次查询都扫描/展开大子树。
3. **Maintenance efficiency**：结构变更应尽量避免重写子树内每条 entry 或物化路径。
4. **ANN-index independence**：目录语义是 scope-resolution 模块，可搭配不同向量索引，不要求改 ANN 算法。

## 2. 方案

### 2.1 总览

层级用 path string 编码时，目录约束被改写成一或多个标量 path 谓词，ANN 索引不动，目录逻辑全在元数据层。中心设计选择是**何时展开目录 scope**：查询时展开 = PE-Online；摄入时展开 = PE-Offline；不做路径展开、原生保留拓扑 = TrieHI。三设计都是 Viking 上的 pluggable 层，只算候选集，不碰向量排序。

### 2.2 PE-Online（§III-A，pp.3–4）：查询时展开，时间换空间

- **存储**：三个结构——entry→目录映射；parent-path 倒排（每目录 path key → 直接存于其下的 entry 集）；辅助目录索引（全部 path key，支持前缀枚举与直接子查找）。每条 entry 只贡献 1 条 posting。
- **Recursive DSQ**：用辅助索引枚举锚点子树内全部目录 key（m_q 个），逐 key 取 posting 并集。元数据开销随子树目录基数线性增长——对宽浅 scope 敏感。
- **Non-recursive DSQ**：直接查锚点 path key 的 posting，1 次查找 + 读 posting。
- **MOVE**：枚举源子树 m_u 个 key，前缀替换算目标 key，逐对重映射 posting、增删辅助索引 key。开销 O(m_u)，与 entry 数无关但随子树目录数增长。
- **MERGE**：O(m_u) key 合并 + 目标 key 冲突的集合并。不比 MOVE 便宜，冲突多时更贵。

### 2.3 PE-Offline（§III-B，pp.4–5）：摄入时展开，空间换时间

- **存储**：在 PE-Online 结构上增加 path expander（父 path → 祖先序列）和 ancestor-materialized 倒排（每目录 key → 位于其下*任一深度*的 entry 集）。深度 t 的 entry 贡献 t 条 ancestor posting；高层 key（如 `/`）物化巨大后代集。
- **Recursive DSQ**：查锚点 key 的 posting 一次命中。
- **Non-recursive DSQ**：取锚点 posting 全集，枚举直接子目录（c 个）、取子 posting 并集成 SetChildren，算差集 SetTotal ∖ SetChildren。开销 ∝ 直接扇出。
- **MOVE/MERGE**：除 O(m_u) 的 key 重映射/合并外，还要更新子树外 proper ancestor 的 posting（旧独有祖先删、新独有祖先加、公共祖先不动），多 O(t) 祖先更新。MERGE 同样 ≥ MOVE。

### 2.4 展开式设计总结（§III-C，Table I，p.5）

兼容性是主要优点（复用标量过滤接口，不改 ANN）；共同瓶颈是**维护放大**：结构变更改变子树的标量命名空间，受影响 path-key 范围必须重映射/合并，成本与变异子树大小绑定。层级只编码为 path string，元数据层没有可跨 DSQ/DSM 复用的原生子树对象。PE-Online 查询敏感（子树目录基数），PE-Offline 摄入/存储/非递归查询/维护全面放大。

### 2.5 TrieHI（§IV-A/B，pp.6–8）：原生前缀树

**结构**：每目录一个 TrieNode（segment、children hash map、parent 指针、inclusive ids）；另有 entry→node catalog。核心不变量（Eq. 1）：

> Inc(v) = Local(v) ∪ ⋃_{w ∈ Child(v)} Inc(w)

即节点的 inclusive 集 = 直接绑定条目 + 各子子树聚合，使**目录节点成为可复用的物化 scope**。成员集合用 Roaring bitmaps 实现。

| 操作 | TrieHI 开销（Table II） |
|---|---|
| Ingestion | O(t) 节点访问 + O(t) 聚合集更新（t = 路径深度） |
| Recursive DSQ | O(t) 遍历 + 1 次聚合集访问 |
| Non-recursive DSQ | O(t+c) 元数据访问 + 并/差集（c = 直接子目录数） |
| MOVE | O(t)：源/目的路径访问 + 受影响祖先聚合更新；与子树目录数**无关** |
| MERGE | O(t+r)：r = 递归调和访问的源节点数；非冲突子树整体重链，冲突分支逐节点调和 |

与展开式的本质区别：子树有**稳定节点身份**，MOVE 重链子树根即可，不必为每个后代目录重新生成标量 path key；MERGE 的冲突处理在节点级递归进行。更新一致性：变异在其受影响 trie 区域内按固定序执行（收集受影响 entry 集 → 改 parent/child 链 → 刷新 catalog 与祖先聚合 → 清除旧绑定），重叠路径变异串行化。

**代价**：摄入沿祖先链更新聚合（形式同 PE-Offline 的祖先物化）、存储含 trie 拓扑 + 每节点 inclusive 集（三设计中最大）、non-recursive 查询要算局部差集。Table I/II 的 O(·) 是节点访问/集合更新次数，bitmap 与 catalog 的实际成本不含在内。

### 2.6 OpenViking 集成（§IV-C，p.8）

OpenViking（字节开源 agent context database）用虚拟文件系统组织 memories/resources/skills（viking:// URI）。TrieHI 支撑三个能力（Table III）：filesystem 式上下文组织（URI segment = 目录节点）；L0/L1/L2 分级上下文加载（同一 scope 挂不同粒度条目，先轻量后详细）；directory-recursive retrieval（先解析目录 scope，再在 scope 内向量排序）。

## 3. 实验（§V，pp.8–12）

### 3.1 设置

- **WIKI-Dir**：Wikipedia 语料 + DBpedia 查询与相关性标签；爬 Wikipedia 类别 DAG，每对象取一条规范路径构成 canonical tree。363,467 目录、均深 11.95、1.94M 条目、1024 维 bge-m3、456 测试查询（锚深度递增）、1000 MOVE + 1000 MERGE。
- **ARXIV-Dir**：2.76M arXiv 摘要，双独立命名空间——subject 层级（如 `/cs/AI/`，168 目录、均深 2.19）+ temporal 层级（如 `/2024/05/`，432 目录、均深 1.92）；1024 维 mxb-ai-embed-large-v1；1000 查询，约束自动生成，GT = 约束满足集暴力搜索。
- **执行器**：Viking 的 PG 与 IVF 两种 ANN；C++ 实现；48 核 Xeon Silver 4310 @2.10GHz、125GB RAM。
- **指标**：DSQ 质量 nDCG@k / Recall@k + 端到端延迟 + **directory-only 延迟**（谓词解析成候选 ID 集的时间，隔离目录开销）；DSM 墙钟；构建时间 + 索引大小（含基线向量索引与目录模块）。
- **口径注意**：所有设计共享的 entry→目录 catalog 在 DSM 延迟与目录模块开销比较中**排除**（报告差异只反映设计特有结构）。

### 3.2 RQ1：DSQ 性能

**Recursive（Fig 7，Table IV）**：PE-Offline 与 TrieHI 是更优的准确率-延迟权衡。WIKI-Dir directory-only 延迟：PE-Online mean 365,000.80 µs（P99.9 达 2.65 s，浅层锚点展开巨大子树）vs PE-Offline 235.23 / TrieHI 273.10 µs（同一量级）；ARXIV-Dir：1,089.20 vs 0.12 / 0.43 µs。查询时路径展开是 recursive DSQ 的主要扩展性瓶颈。

**Non-recursive（Fig 8，Table IV）**：局部权衡反转。WIKI-Dir：PE-Online 29.07 µs（直接父路径查找）vs PE-Offline 1,037.07 / TrieHI 945.17 µs（差集 ∝ 直接扇出）；ARXIV-Dir：4.35 vs 163.91 / 112.49 µs。但 Fig 8 显示该元数据开销相对端到端向量搜索仍小。

**深度敏感性（Fig 10/11/12）**：WIKI-Dir 浅层路径子树巨大（depth 1 锚点平均展开 236,903 个后代 key，depth 9 仍有 86,501），而直接扇出有界——正是查询时展开的脆弱结构。质量随路径变深一般变好（目录 scope 携带检索上下文）。延迟分解（Fig 12）：recursive 的 PE-Online 开销在 "Sub-Path Obtain"+"Bitmap Fetch"；non-recursive 的 TrieHI/PE-Offline 开销在 "Bitmap Compute"。PG 下深约束更 selective、候选 mask 削弱图连通性，执行器侧延迟反而升；IVF 的 partition probing 主导，深度曲线更平。

### 3.3 RQ2：DSM 性能（Fig 9）

TrieHI 的 MOVE 重链子树 + 更新受影响祖先聚合，开销由源/目的路径与移动 entry 集决定，与子树目录数解耦；MERGE 限于冲突分支调和。展开式设计必须把受影响后代 path key 全部重映射/合并（PE-Offline 还要更新物化祖先 posting），子树越大越贵且方差高。这是原生树设计的主要证据。

### 3.4 RQ3：构建与存储（Table V）

构建时间增量均 < 1.7%（WIKI-PG 基线 1491.27 s → 三设计 1502.21–1515.25 s）。存储：PE-Online 最小（只存直接成员）；PE-Offline 祖先物化，深路径的 WIKI-Dir 上更明显；TrieHI 两数据集都最大（trie + 每节点 inclusive 集；WIKI-PG 8326 MB vs 基线 7925 MB；ARXIV-PG 11,519 vs 11,227 MB）。结论：无 universally dominant 表示；TrieHI 用元数据空间换 recursive DSQ 与 DSM。

### 3.5 RQ4：OpenViking 端到端（Table VI/VII）

- **LOCOMO 长对话用户记忆 QA**：接入 OpenViking 后 OpenClaw 24.20%→82.08%（平均查询时间 95.14→38.8 s，输入 token 392.6M→37.4M）；Hermes 33.38%→82.86%；Claude Code auto-memory 57.21%→80.32%。TrieHI 提供 scope 解析 + L0/L1/L2 分级避免重复注入全量历史。
- **HotpotQA 多跳 KB QA**：OpenViking top-20 = 91.00% 准确率 / 0.23 s / 12,533 tokens，高于 LightRAG 的 89.00% / 75 s / 28,443 tokens，且 token 与延迟都更低；top-5 = 72.75% / 0.22 s。机制：先解析连贯目录 scope 再向量排序，增大 top-k 主要增加 scope 内相关证据。
- 注意：这两表是**系统级结果**（含 LLM 编排与提示工程收益），不是 TrieHI 作为 ANN 组件的独立保证。

### 3.6 作者结论

目录语义应作为向量数据库一等能力；展开式能表达目录语义但继承维护放大；TrieHI 以节点身份替代子树范围的标量物化，recursive DSQ 与 MOVE/MERGE 由子树规模解耦，代价是最大存储与 non-recursive 差集开销。Future work：更丰富的 path predicates（前缀约束、通配 segment）形式化与评估；混合语义/层级条件的 cost-aware planning。

## 4. 局限与未解决问题

1. **理论**：近邻质量定理未提供；Eq. 1 是精确 scope 不变量，非 ANN 召回保证。Table I/II 的 O(·) 不计 bitmap/catalog 实际成本。
2. **TrieHI 存储最大**；non-recursive 查询非最优（Table IV 不支持"TrieHI 所有查询均最快"）。
3. **共同 catalog 排除**使 DSM/目录开销比较偏小，真实系统总账需另算。
4. **后端覆盖**：固定 Viking PG/IVF 隔离目录开销，不代表相对 ACORN、Filtered-DiskANN、UNG 等属性感知 ANN 的端到端领先（§VI 引用但不比较；作者未说明理由，也未证明不可组合）。
5. **数据集**：WIKI 类别 DAG 取规范路径丢多父信息；ARXIV 双命名空间独立、浅（均深 ~2）；距离/归一化/执行器调参细节原文报告有限。
6. **DSM 操作目标**的分布（移动子树大小、冲突率）原文按生成协议描述，通用性取决于负载分布。

## 5. 本地复现与延伸

- 合成 100k 全协议复现（Table IV/Table V/Fig 8/9/10 对应实验 + 6 项偏离声明）：`2026-10-26-triehi-reproduction/2026-10-26-triehi-repro-results.md`。
- 真实数据 400k 子采样验证（WIKI/ARXIV-Dir 发布数据）：`2026-10-26-real-dataset-validation.md`。方向性结论一致；全量（需 ≥24GB RAM）与 Fig 7 逐点 trade-off 曲线为遗留任务。
- 选择率主线（含 IVF × 相关谓词 §6.3）：`2026-10-26-selectivity-sweep/2026-10-26-selectivity-experiments.md`。

相关记录：索引摘要 [`paper.report.md`](../../../papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md)；全文 [`paper.md`](../../../papers/prefix-vector-search/2606.16903-directory-aware/paper.md)。
