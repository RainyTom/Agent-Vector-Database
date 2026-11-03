# 依托alphaXiv的当前DAG组合图创新性评估

核查日期：2026-11-03。评估对象为提交`64214c2`时的可运行DAG合并图，不是2026-10-19的六个历史方向，也不是尚未实现的分层/b>0/等字节剪枝设计。

## 1. 结论先行

**当前方案尚未通过独立算法创新性确认。基础机制有强前序，整体具体组合尚未被证明重复，也尚未被证明具有实质新贡献。**

不宜主张：首次每属性建图后合并、首次共享单图、首次多入口共用一次搜索、首次先资格后距离、首次把小图递归合并、首次支持递归/非递归层级过滤。

仍值得保留核查的具体差异是：**仅对直属owner建局部HNSW，沿外部多父属性DAG递归汇总真实item代表，并布置跨组桥，使Direct/Recursive OR都在同一item图上执行严格过滤的一次搜索。** 当前相关前序没有被本轮证明与其逐步骤同构；但“没有找到完整同构”不是新颖性通过。

这不是因为所有组件都必须全新。已有原语的组合也能产生论文贡献，但必须体现新的算法规则、结构性质、成本界或可靠实证，而不是只有场景、参数和命名差异。现阶段更准确的定位是**DAG-aware共享item图的研究候选/实验基线**，不是已确认的新图索引范式。

## 2. 实际如何使用alphaXiv

- 先查`papers/papers.jsonl`、`relations.jsonl`、已有全文及历史alphaXiv记录，再按vector similarity join/vector join/similarity self-join/high-dimensional similarity join/inner-product similarity join检索，随后切到filtered ANN、graph stitching、HNSW merge、hierarchical labels、DAG、UNG等。
- 本轮实际通过alphaXiv公开论文页、alternate Markdown或其公开PDF取得8篇相关全文并核相关章节：DLH、LSSG、HNSW Merge、iRangeGraph、Directory-Aware、ACORN、Curator、JAG；另从作者网站核读Filtered-DiskANN原稿。不是8篇全部逐页精读，更未运行作者实现。
- 公开网页阅读有效；交互浏览器启动失败，平台`llms.txt`为404。**未调用alphaXiv AI Assistant或discovery MCP；本报告是基于其论文内容的研究者判断，不是alphaXiv官方认证或AI评分。** 搜索引擎的site:限定检索也不冒充平台内部discovery。
- UNG作者仓库本轮可读，正式ACM PDF为403；已有作者海报和固定代码快照另标证据等级，不冒充取得UNG论文全文。UniFilter原文也仍缺。
- 当前问题属于B类可迁移filtered ANN，不是几何vector join；本轮没有新增join算法结论。

[原始来源、版本、短引文与SHA](data/alphaxiv-dag-novelty/sources.json)、[平台能力/访问边界](data/alphaxiv-dag-novelty/platform-access.json)。已有论文全文不复制；新增JAG和HNSW Merge各保存一份全文及日期化metadata。

## 3. 核心前序：逐机制比较

证据等级：**原文**是本轮或已有本地原文相关节核查；**作者材料**是官方代码/海报，不等于目标论文正文；**推导**是本项目分析，不归给论文。

| 前序 | 已有机制与定位 | 对当前方案的约束 | 不能强行判同的差异 |
|---|---|---|---|
| [Filtered-DiskANN / StitchedVamana](https://harsha-simhadri.org/pubs/Filtered-DiskANN23.pdf)，WWW2023，原文 | §4/Alg.5，pp.4–5：按标签建Vamana，按统一item身份叠加边，再FilteredRobustPrune；§3.1/Alg.1，pp.3–4：OR资格、多标签起点集合进入一个候选队列，先筛合格邻居再距离排序 | “局部图→共享图”“多入口一次OR搜索”“严格资格导航”均已有直接前序 | 标签匹配集允许重叠；当前初始图是互斥直属owner集合，须显式加桥。原文不按外部DAG逆拓扑汇总代表，也没有当前组间MST规则 |
| [LSSG](https://www.alphaxiv.org/abs/2609.15058)，v1，原文 | §2.1 p.3支持equality/containment/overlap；§3.1 p.4共享V的多tier；§3.4 p.6 Alg.3–4：合格多入口、共享beam、倒排bitmap并/交、先资格后距离 | 不能把“同一图多尺度”“倒排合法多入口＋单frontier”“bitmap资格”作为新增 | tier按标签相似度阈值，不是属性DAG深度或HNSW随机层；未证等价于递归代表＋MST桥。v1页眉自列SIGMOD27但DOI占位，正式出版未独立核实 |
| [UNG](https://doi.org/10.1145/3698822)，PACMMOD2024/SIGMOD2025，作者材料 | [官方实现](https://github.com/YZ-Cai/Unified-Navigating-Graph)：相同标签集分组、组内Vamana、标签包含DAG、对象级跨组近邻边，组合为统一对象图；本地快照见下 | **结构上最危险的前序之一**：组内ANN＋包含偏序＋跨组桥已存在，不能只把Vamana换成HNSW便称创新 | 目标论文全文未取得；代码的containment共享队列与overlap多轮搜索必须分开。未核证其与外部DAG代表池/MST/反向桥限额完整等价 |
| [iRangeGraph](https://www.alphaxiv.org/abs/2409.02571)，v1；PACMMOD2024/SIGMOD2025，原文 | §3.2.2 pp.5–6：自底向上复用子段图邻居，在另一子段搜候选再RNG剪枝；§3.3按需生成范围专用邻接后一次搜索 | “自底向上复用/组合子图以避免独立多图搜索”不是空白 | 数值区间/segment tree不是任意多父DAG；保留elemental graphs、查询时选边，不是当前一份平面邻接。不能直接搬用其空间/时间界 |
| [Three Algorithms for Merging HNSW Graphs](https://www.alphaxiv.org/abs/2505.16064)，2025预印本v1，原文 | §4 pp.7–10：NGM/IGTM/CGTM逐HNSW层重建融合邻域；§7 p.16已提出从小图递归合并构图作为future work | 不能主张首次合并HNSW或首次提出递归合并小图 | HNSW随机层次不是语义DAG；普通几何merge不含Direct/Recursive过滤。future-work提议不等于已经实现当前完整方案 |
| [JAG](https://www.alphaxiv.org/abs/2602.10258v1)，2026预印本v1，原文 | §3.2–3.3 pp.5–7：多阈值导航候选union、单图、JointRobustPrune总度预算；§4.2比较ACORN、Stitched、UNG、iRangeGraph | 多尺度边融合、总度预算本身已有强前序；后续分层/剪枝不能仅据此申报创新 | 按filter distance允许不合格节点导航，与严格b=0不同；阈值层次不是外部DAG。正式venue未核实 |

### 必须纠正的两处易错比较

1. Filtered-DiskANN §2确实提到OR可逐标签搜索后合并；**但§3.1与Alg.1同时明确支持多标签起点的一次共享OR搜索**。不能只引用前一句，把共享OR搜索留给我们当新意。算法支持与作者只实验singleton查询也不能混为一谈。
2. UNG官方代码的containment路径把各极小合格组入口放入一个`search_queue`；overlap路径另有逐入口组搜索与合并。**不能称UNG所有OR查询都已一次共享搜索，也不能因此反称共享OR是我们的首次。** LSSG和Filtered-DiskANN已有直接证据。

UNG固定本地证据：`researches/containment-filtered-vector-search/evidence/ung-uni-nav-graph-2026-10-19.cpp`，组内图171–208、包含DAG226–245、跨组边264–369、overlap407–420、containment入口456–480/共享搜索514–565；作者海报为同目录`ung-author-poster-2026-10-19.md`。本轮未重新编译或执行其代码。

## 4. 背景前序：问题语义与其他结构

| 工作 | 已有能力与证据 | 与当前方案的边界 |
|---|---|---|
| [Directory-Aware / TrieHI](https://www.alphaxiv.org/abs/2606.16903)，v1 | §II-C p.3已定义recursive/non-recursive；§IV-A明确目录层不改变ANN布局 | 是树式目录语义前序，不是当前item级桥接；论文将原类别DAG选路径归约成树，不证明保留全部父边的DAG导航已解决 |
| [ACORN](https://www.alphaxiv.org/abs/2403.04871)，v1 | §4–5 predicate-subgraph traversal，保留HNSW层次，扩展邻域改善过滤可达性 | 谓词无关构图，不是属性DAG合并；当前仅移植ACORN-1搜索策略，不能称复现完整作者索引 |
| [Curator](https://www.alphaxiv.org/abs/2601.01291)，v3 | §1及§4.2：共享聚类树上的per-label索引，复杂谓词临时索引、局部求值/缓存 | 共享物理结构并非新意，但它是partition-based补充，不是当前图桥接的同构实现 |
| [DLH](https://www.alphaxiv.org/abs/2607.00727)，v1 | §2.1 p.2明确无向filter graph、r-hop范围；§3–4标签/Bloom/memoization | 已有结构化图过滤问题，但**无向距离范围不等于有向DAG后代闭包**，不能据此判整个问题相同 |

NodeANN是本项目按直属分区分别搜索后归并的基线，不是已核实的TrieHI论文命名算法。只比NodeANN不足以排除UNG、LSSG、Stitched等更近的算法前序。

## 5. 关键推导：DAG语义可精确归约到现有标签过滤

以下为本项目推导，不是上述作者声称的新颖性结论。

设item的唯一直属属性是`owner(x)`，`Anc*(v)`包含v自身及其所有祖先。对查询属性集合Q：

- Direct OR：`owner(x) in Q`。
- Recursive OR：存在`q in Q`使`owner(x) in Desc*(q)`。

为每个item编码两个不交标签命名空间：

```
L(x) = { D:owner(x) } ∪ { R:a | a ∈ Anc*(owner(x)) }
Direct查询标签：{ D:q | q ∈ Q }
Recursive查询标签：{ R:q | q ∈ Q }
统一资格谓词：L(x) 与查询标签集合有非空交集（overlap）
```

因为`owner(x) in Desc*(q)`当且仅当`q in Anc*(owner(x))`，两种模式的合格集合完全相同，包括多父共享后代和没有直属item的内部属性。**这说明“以前方法不能表达我们的DAG过滤”不能作为创新论据。**

注意：多节点OR对应overlap，不是`Q ⊆ L(x)`；后者变为AND。祖先标签的equality也不等于Direct。

### 本轮实际验证

[独立验证程序](experiments/verify_dag_label_reduction.py)用真实1,929属性/2,040边：前向DFS独立构造后代集合，拓扑传播构造祖先标签；检查全部singleton两模式owner对 **7,442,082项**，以及原采样120个复合scope的两模式 **462,960项**，全部相等。所得G分布也与现有实验一致。

[输出与输入SHA](data/alphaxiv-dag-novelty/scope-reduction.json)另给出query81的OR/AND反例：Q={1230,1232,1233}、owner1230，overlap为真、containment为假。这只是资格归约验证，**没有运行ANN，不证明索引拓扑、内存、维护或召回等价**。

### 归约也不能抹掉成本差异

祖先闭包可能扩大标签与维护成本，但不能用“每item天真复制祖先列表”作唯一基线。相同owner的祖先集合可intern/去重共享；LSSG本就按标签集组织，UNG按标签集分组。本项目也预存`desc[a]`，不能把已有闭包空间算成零。

若要论证DAG专用表示更省，应对齐**共享去重后的标签闭包、元数据、入口、边、构建/更新成本与内存字节**，不是只对比标签数量。问题可表达性等价，不等于同成本高效执行；这正是潜在空间，但尚未被当前实验建立。

## 6. 剩余差异到底在哪里

当前代码可核对的具体组合包括：

1. 只对互斥直属owner建HNSW，不为每个祖先完整重建子图。
2. 沿外部多父DAG逆拓扑汇总真实item代表，最多8个，而不是单独几何中心路由层。
3. 输入代表组MST、近代表桥、直属item分布式连接、反向桥上限16和代表快捷边。
4. 最终一份平面item邻接，Direct/Recursive分别取合法入口，共享一次搜索；严格b=0。

本轮没有证明其他论文完整实现了以上组合。但这些规则目前主要是启发式；**MST连接输入组不等于任意过滤诱导子图可导航，更不等于近邻距离单调或高召回保证**。8代表/反向限额也没有与DAG重叠宽度、数据几何和导航成本联系起来的已证界。

HNSW代替Vamana、参数M64/efC200、portal叫法、保留FP32/SIMD、换SIFT数据都不是独立创新。b>0、新全局层次和等字节剪枝尚未实现，不能提前计入贡献。

## 7. 目前要达到的研究门槛

下列均是**基于文献综合推导的待验证研究假设**，不是作者future work，也不是已经提出/实现的新算法：

| 可辩护方向 | 必须比前序多证明什么 | 最小验证与反例风险 |
|---|---|---|
| 多父DAG共享桥的成本优势 | 在标签集已去重的UNG/Stitched/LSSG基线上，新的共享/选边规则仍减少实际字节或构建/更新工作，而非省掉天真复制 | 同字节、同Recall比较，拆开直属边/桥/入口/闭包；多父大量重叠可能让桥和反向连接膨胀 |
| Direct/Recursive兼容的导航结构 | 不只是局部资格见证，而是对规定scope族的入口覆盖、可达/路径或可导航性质；说明固定预算下何时成立、何时失败 | 小DAG穷举/反例＋真实数据召回；任意OR诱导图可能破坏桥，连通也可能贪心不可达 |
| 比独立局部搜索更好的效率前沿 | 在更近算法和完整局部n调节下，同质量、同服务预算仍有稳定收益 | 仍固定用户两格，增加独立query与真实属性相关性；当前G=2仅7/11条，高G低Recall点不能当成功 |

本轮不提出新机制来绕过查重，也不自动停止用户正在研究的DAG框架；结论是**先把差异落到以上可检验贡献，再谈论文创新性**。不重新启动历史编码树/量化路线。

## 8. 本轮交付、限制及下一步

新增JAG、HNSW Merge两条规范记录及唯一全文/审读，补3条JAG原文明示比较关系；论文库45篇、33关系。既有论文不重复保存全文，UNG/UniFilter未取得目标全文者仍不假装正式精读入库。

未进行新ANN性能实验；仅执行资格归约验证，未改变检索实现、SIMD或最新QPS图。当前已有实验的随机属性绑定、G样本不足和质量差异仍适用。

检索覆盖有限：定向同义词、alphaXiv平台全文、既有本地引用链和作者实现，未追完所有图拼接/约束导航/动态图文献，也未核全部正式版差异；未检索到完整同构不构成独创证明。UNG与UniFilter正文仍是最重要待补缺口。当前创新性判断为**未通过确认，而非已证明整方案重复**。

复现资格核验：

```bash
python3 researches/nested-attr-vector-search/2026-11-03-exps/experiments/verify_dag_label_reduction.py \
  --workload work/dag-scope/workload-run1 \
  --out work/alphaxiv-dag-scope-reduction.json
```

本地证据入口：[来源](data/alphaxiv-dag-novelty/sources.json)、[访问限制](data/alphaxiv-dag-novelty/platform-access.json)、[资格验证](data/alphaxiv-dag-novelty/scope-reduction.json)。
