# 索引结构与命名复核：取消机制包装

## 当日目标与结论

用户指出“资格索引、层级分块索引、三种布局”的命名有过度包装、索引形式不明显。先核对总论文/关系索引、本地全文、现实现，再读官方系统说明；本轮是定向结构与术语审查，不是新的算法或性能实验。

**结论：质疑成立。当前原型应描述为“直接目录成员倒排表＋scope内属性逐对象过滤＋DFS装箱的平坦分区HNSW；小候选集切换到精确扫描”。**

- 当前不是新型多层ANN，也不是每层目录有一张图的层级导航结构。
- `eligibility`可保留为源码模块名，但正文必须说清楚真实数据结构；它不是已建立新颖性的索引类别。
- `P-dfs-aware`与`P-dfs`共用同一分区HNSW，差别是查询计划，不能写成两个索引。
- `map → sorted vector`、删除bitplanes是有效工程实现优化，不能因为加速大就改称新剪枝机制。已有等价性结果恰好说明该改动没有产生新的候选排除能力。
- 常规组件重合不等于项目所有组合必然无创新；但**目前证据不足以把该原型包装成新的层级ANN结构**。

## 1. 真实存储结构

### 1.1 元数据侧：目录倒排＋属性表，不是完整多属性倒排索引

`codes/directory-aware-vector-attr-search/src/eligibility_index.h`：

- `parent_/children_`：目录邻接树。
- `owners_[dir]`：`vector<pair<uint32_t,uint64_t>>`，按word有序。记录**直接归属该目录**的对象ID，word为`id/64`，bits指明其中哪些ID归该目录。
- `items_[id]`：对象owner、category、numeric、tags、alive的记录。
- `live_`：存活位图。
- `Result.scope/eligible`：查询工作区，不是持久的每目录子树聚合集。

`prepare()`先遍历请求的目录/子树，将直接成员posting按位或成scope，再读取scope内对象记录判断属性，生成eligible位图和精确计数。当前**没有**category→ID、numeric范围树、tag→ID的三套独立索引；因此不应称“多属性位图索引”或暗示所有属性都可倒排直达。

精确计数不是免费估计：获取该计数的posting合并和属性判断都属于查询成本。

### 1.2 向量侧：平坦分区集合，每分区一张HNSW

`src/packed_index.h:172–208,285–318`：

- `blocks_[b]`：成员列表和普通hnswlib HNSW。
- `loc_[id]`：对象→物理分区。
- `home_[dir]`：新对象默认落入的分区；复活对象可能回原位置。
- 构建时按直接owner分组，按给定次序next-fit装箱；每分区独立HNSW，M16/efConstruction200。
- 查询时枚举eligible IDs，按loc统计每分区合格数，跳过合格数为0的分区；**搜索所有剩余分区**，最后合并top-k。

这里没有目录树上的图间路由，没有分区质心ANN，没有每层可组合邻接，没有距离下界或几何摘要。HNSW本身有多层导航图，但它的随机导航层与目录父子层级不是一回事。

### 1.3 三种布局只是分区构造配置

| 历史配置 | 真实操作 | 不应暗示的能力 |
|---|---|---|
| node | 每个非空直接目录组单独一个分区 | 不是新的ANN；与纯NodeANN仍有预选、局部ef等执行差异 |
| dfs | 目录DFS次序，容量目标驱动地合并相邻目录组 | 不保证分区对应某完整子树，不提供层级图路由 |
| shuffle | 打乱目录组次序后，用同一装箱规则 | 是分组顺序对照，不是新检索算法；块数不保证与dfs相等 |

同一种装箱算法换顺序一般可改变块数/尺寸；当前某些数据恰好相同，不能宣称总是完全相同。B=4096是装箱目标，超大目录允许独占超过B的分区，并非硬容量上限。

整目录不拆分是**初始layout/repack**规则；动态split按成员中点切分，可切开同一owner组。owner移动也可使一个目录分散在旧分区，不能把初始共置性质写成动态永久不变式。

## 2. 项目内论文：结构必须说得出“存了什么”

先检查`papers/papers.jsonl`、`papers/relations.jsonl`及已存全文。本轮沿用已有条目，不新增论文、不覆盖旧版本、不添加推测引用边。

### 直接相关

| 工作与版本 | 核实的存储/搜索结构 | 与当前结构的边界 |
|---|---|---|
| [Directory-Aware Query and Maintenance in Vector Databases](https://arxiv.org/pdf/2606.16903v1)，v1 §III–IV，PDF pp.3–7 | PE-ONLINE保存直接路径posting，在线展开后代；PE-OFFLINE物化祖先成员；TrieHI保存路径段Trie、稳定目录节点、每节点inclusive-ID集 | 三者是scope解析结构，不是三种ANN。当前与在线直接成员展开有相同取舍，但用目录ID/邻接树而非完整路径键。没有TrieHI的每节点inclusive-ID持久聚合集 |
| [Curator](https://arxiv.org/pdf/2601.01291v3)，v3 §3.1–3.2、§4.2，PDF pp.4–7 | 共享hierarchical k-means树、节点centroid、按标签分布自适应的buffer层次、Bloom存在性结构；一般谓词可由合格ID构造临时层级索引 | 真正存在跨层空间分区及标签相关路由。不是目录DFS排序后的单层分区，也不是内部节点都是布尔谓词的decision tree |
| [iRangeGraph](https://arxiv.org/pdf/2409.02571v1)，v1 §3.2–3.4，PDF pp.5–7 | 数值属性排序域的segment tree、多层elemental graphs；查询按需选边组成range-dedicated graph | 过滤范围实质改变可选邻接；不是搜索几张独立图后合并 |
| [ACORN](https://arxiv.org/pdf/2403.04871v1)，v1 §5.1–5.3，PDF pp.4–6 | HNSW衍生共享图；predicate-agnostic构建；ACORN-1扩展一/二跳邻域以支持predicate-subgraph traversal | 普通hnswlib callback不等于ACORN。论文构建变体也不能与Weaviate的定制搜索实现混为一谈 |
| [Filtered-DiskANN](https://harsha-simhadri.org/pubs/Filtered-DiskANN23.pdf)，§1.3、§3，PDF pp.2–4 | FilteredVamana让标签参与剪边；StitchedVamana叠加标签图的边后再剪枝，最终为共享图 | 真正增加/保留标签相关图边，不是每标签独立图逐一查询。原论文一般AND复杂表达式边界须保留 |

关键原句及本地定位：

- TrieHI："It does not change vector ranking or the layout of the underlying vector index." `papers/prefix-vector-search/2606.16903-directory-aware/paper.md:674–678`。`Inc(v)=Local(v) ∪ ⋃Inc(child)`及祖先更新见`:714–738`。因此可以有真实的**元数据索引**贡献，但必须说明额外状态及其更新成本，而不是只靠“层级”一词。
- Curator："The base index T is a standard hierarchical k-means clustering tree"，`papers/filtered-ann/2601.01291-curator/paper.md:409–428`；标签buffer/Bloom结构见`:451–493`，复杂谓词输入posting/bitmap见`:673–684`，临时节点范围见`:820–845`。
- iRangeGraph："Our index is based on segment tree"，`papers/filtered-ann/2409.02571-irangegraph/paper.md:503–539`；按需边见`:672–678`。
- ACORN："consider all one-hop and two-hop neighbors"，`papers/filtered-ann/2403.04871-acorn/paper.md:618–633`。
- Filtered-DiskANN：叠加标签图的边见`papers/filtered-ann/10.1145-3543507.3583552-filtered-diskann/paper.md:209–239`，标签约束剪边见`:376–401,479–493`。

注意：**NodeANN是本项目基线标签，不是Directory-Aware论文提出的第四种方法。**原论文PE-ONLINE/PE-OFFLINE/TrieHI接PG/IVF；本项目每目录局部HNSW不应借论文名暗示原文已验证。

### 背景：何为真正按属性谓词组织的布局树

[Qd-tree: Learning Data Layouts for Big Data Analytics](https://zongheng.me/pubs/qdtree-sigmod20.pdf)，SIGMOD2020作者稿§3/§3.3，PDF pp.4–5。内部节点的cut是属性谓词，左右分别是p/¬p；叶对应物理块，查询与块语义描述相交后产生`BID IN (...)`。实现扫描叶元数据判断相交，也提出可以沿树路由，不能只描述后者。

本地定位：`papers/data-indexing/2020-qd-tree/paper.md:114,148–150`。这是数据布局/块跳过工作，不是ANN，更不是当前目录DFS装箱的同名替代。

## 3. 官方博客/文档：常规组件已有明确名称

| 来源 | 官方组件/术语 | 对本项目的约束 |
|---|---|---|
| [Weaviate Filtering](https://docs.weaviate.io/weaviate/concepts/filtering)，Efficient Pre-Filtered Searches、Flat-Search Cutoff | inverted index产生allow-list，交给HNSW；小匹配集切flat/brute-force | 对应“元数据预选＋带结果过滤的ANN”和“扫描回退”，不是新索引名 |
| [Weaviate 2024-11-19 ACORN博客](https://weaviate.io/blog/speed-up-filtered-vector-search)，Pre-filtering、pre-ACORN、implementation | `flatSearchCutOff`；sweeping；定制ACORN查询按需二跳，可不改变vanilla HNSW构建 | 搜索策略也可有实质贡献，但应说清扩展规则；不能用aware词汇代替算法说明 |
| [Qdrant Indexing](https://qdrant.tech/documentation/manage-data/indexing/)，Payload Index、Vector Index、Filterable HNSW | payload index辅助基数估计与规划；`full_scan_threshold`；filterable HNSW增加payload相关边 | 元数据索引、查询计划、向量图结构是三件事。当前没有其额外payload边，也没有免费得到的基数估计 |

原始链接和短引保存在[data/official-filtering-terminology.json](data/official-filtering-terminology.json)。官方文档是本轮访问的无版本在线页，不推定所有历史版本都相同；不引用其性能数字作为项目实测。

术语兼容：部分论文把pre-filtering专指筛选后精确扫描，Weaviate把allow-list后接HNSW也称pre-filtering。正文采用**metadata preselection + filtered HNSW / exact-scan fallback**避免歧义。

## 4. 统一命名与创新边界

| 旧叫法 | 当前正文采用的描述 | 类型 |
|---|---|---|
| 资格索引 | 直接目录成员倒排表＋属性逐对象过滤 | 元数据结构/候选生成 |
| 层级分块索引 | DFS目录组装箱的平坦分区HNSW | 向量分区组织 |
| P-dfs | DFS装箱分区HNSW＋候选位图过滤 | 历史实验标签对应的执行配置 |
| P-dfs-aware | 同一分区HNSW＋小候选集精确扫描回退 | 查询计划，不是第二套索引 |
| 三种布局算法 | 三种分区构造配置：单目录/DFS装箱/随机目录装箱 | 组织消融 |
| 顺序postings新剪枝 | posting容器/内存访问优化 | 工程改进，无新增排除集合 |

历史标签、CLI、源码namespace和原始CSV不改，避免破坏SHA与复现；其含义在当前文档明确，不另起缩写或重复包装。后续若要提出实质新机制，需要精确列出：新增持久状态、查询由此少访问的对象/边/分区、为何安全或近似误差如何变化、更新触及哪些状态；并对齐同样的预选与扫描策略。**这不是以硬件布局或容器替换冒充论文主贡献。**

目前只证明相对指定纯NodeANN基线有测得收益，不等于优于Curator、ACORN或现代数据库同类规划；后者本轮未运行。

## 5. 对之前方案描述的具体纠正

1. 属性字段category/numeric/tags是平坦对象属性；目录前缀才有层级。当前没有“属性层级剪枝”。DFS装箱也不保证每块是一个完整子树。
2. **候选位图不等于仅在合格节点上计算距离。**`hnswalg.h:418–439`先算距离、可能进入导航队列，再用callback决定能否进入结果候选堆。不合格节点仍可导航和计算距离。不能把它描述为ACORN式predicate-subgraph traversal。
3. shuffle与dfs在旧100k样本均25块，只是该样本结果，不是块数/尺寸一般相同的理论性质。
4. B=4096不是硬上限；动态split触发条件是超过`2*target`的预期活成员数，不是“超过B即分裂”。初始整目录不拆分也不是动态不变量。
5. map改vector的旧等价性数据说明过滤集合、图搜索及返回结果没有新增变化；大加速不能自动证明新信息或新剪枝。没有采集cache-miss计数，缓存归因仍为解释而非直接测量。
6. 元数据无几何图重建不等于所有更新O(1)：新posting word可O(W)搬移；批量owner移动至少触及移动对象；目录重挂当前重算O(D)rank；向量ID空间固定，不能外推任意新向量扩容/并发/持久恢复。
7. 旧报告“0.99258/0.99297低于0.99”为数值文字错误，已纠正；原始数据不变。共同Recall目标达标也不等于两臂实际Recall完全相等。

## 6. 本轮实际执行、结果与复现

实际执行仅为已存结果的结构审计，未构建索引、未重跑ANN、未测维护、未运行作者实现。脚本[experiments/review_index_structure.py](experiments/review_index_structure.py)读取旧manifest和4个summary，记录源文件SHA与结构字段。

```bash
python3 researches/nested-attr-vector-search/2026-11-01-exps/experiments/review_index_structure.py \
  researches/nested-attr-vector-search/2026-11-01-exps/data/index-structure-audit.json
```

首次执行因新data目录未创建，输出写入失败；脚本补齐父目录创建后成功，未重跑旧性能实验。

| 旧100k l2048-random样本 | 物理分区数 | 非空图数 | 策略 |
|---|---:|---:|---|
| P-node | 2341 | 2341 | cap |
| P-shuffle | 25 | 25 | cap |
| P-dfs | 25 | 25 | cap |
| P-dfs-aware | 25 | 25 | hybrid |

相同引擎参数为M16/efConstruction200；构建源码统一在`packed_index.h:131–139`，不是三种ANN实现。审计输出见[data/index-structure-audit.json](data/index-structure-audit.json)。上述是历史单元结构字段，不是本轮性能数字。

## 局限与下一步

- 定向核读上述章节，不声称覆盖全部partitioned/filtered ANN前序；Window Filters等已有待归档项未在本轮宣称完成。
- 未新增paper/relations记录，已有论文版本和ID不变；官方系统短引进入本日证据，不作为论文条目。
- 下一轮先用标准组件描述候选，再判断是否确有新的存储不变量、可访问邻接、覆盖或剪枝能力；不要从方法缩写倒推机制。已停止的WQR/NPI/closure不因改名重新启动。

## 方案正文与图形

[层级属性约束下的向量近邻检索](hierarchical-dag-attribute-index.md)给出当前方案：DAG 属性层级、直接标签集与祖先继承、antichain 查询、向上维护的精确基数、按展开代价选择物化的派生位图、标签增删的祖先方向更新，以及对应的成本模型与可检验假设。图示见 [DAG 属性 scope](figures/dag-attribute-scope-outlined.svg)（文字源图见 `figures/dag-attribute-scope.svg`）。

本轮核对内容：五概念 DAG 的闭包、祖先包含与跨分支交叠（[检查数据](data/dag-attribute-scope-check.json)，6 组包含关系成立，ML 与 IR 互非祖先但交集为 p1）；SVG 以字形路径导出，22 组标签、591 个路径、0 个 `<text>` 元素（[字体与导出记录](data/dag-attribute-figure.json)），并在白底与深色前景下完成渲染检查。

ANN 运行 0，C++ 改动 0。方案中的多父闭包、标签集、精确基数与派生位图尚未在代码中实现，现有 SIFT/WIKI 结果只对应单标签树形负载。

## 显示兼容性

两幅示意图均提供 `*-outlined.svg` 显示版并作为 Markdown 图片引用：标签转换为自包含字形路径，不依赖客户端字体或 `<text>` 渲染，颜色仍随主题前景（`currentColor`），背景透明；文字版 SVG 保留为可编辑源图。字形来自 Noto Sans CJK SC Regular/Bold（SIL OFL 1.1），字体与输出 SHA 见 [data/dag-attribute-figure.json](data/dag-attribute-figure.json)，字体二进制不入库。重新导出命令见 `experiments/outline_svg_labels.py`。

## 真实 DAG 结构测量（CCS 2012 + SciHTC）

在真实多父本体系上测量 §3 方案所需的输入量，不涉及向量：概念 1,929、边 2,040、列表实例 2,113；159 个概念 ID 重复出现，其中 103 个（5.3%）有至少两个不同父。标注 186,160 行，可用 168,937 行，17,223 行含 `~` 被剔除，路径解析失败 0。

每篇论文的祖先集合均值 4.21、最大 11；每个概念的祖先集合均值 5.04、最大 16；scope 均值 369、p95 1,072、最大 168,937。最大 scope 与最大祖先数相差 10,559 倍，构成"向上更新便宜、向下查询昂贵"的结构基础。

多父造成非层状交叠：12,913 篇论文（可用行 7.6%）的继承集合含至少一个多父概念，12,446 篇的终点概念本身是多父。101 个多父概念的父 scope 交并比 p50 0.013、p95 0.53、最大 0.90（Personalization 82/91；CSCW 953/1,202）。

按物化规则 E(u)>n/64 在 n=168,937 上选中 39 个概念（2.0%），位图合计 824 KB，作为 H2 的预算基线。

数据：[scihtc-dag-structure.json](data/scihtc-dag-structure.json)、[scihtc-dag-concepts.csv](data/scihtc-dag-concepts.csv)；脚本 `experiments/scihtc_dag_structure.py`。有向量、无质量结论：SciHTC 缺向量，本轮未跑 ANN、未改 C++。

