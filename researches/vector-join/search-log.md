# 文献检索与覆盖日志

## 2026-10-11 初始结构化索引审计

范围：核对项目已有 8 篇论文的书目与相关全文章节，补充核心术语检索和 ANN 迁移候选。本轮不是全领域系统综述，不把搜索结果排名视作完整覆盖。

### 检索顺序与查询

1. 核心查询：`"vector similarity join" high dimensional paper`、`"vector join" SimJoin DiskJoin`、`"similarity self-join" "inner product" join`。
2. 元数据/版本查询：Xling 正式 publication/ICDE、DiskJoin DOI、GPU self-join DaMoN/JPDC、IPS PODS、SJPC TKDE、LSH-SS PVLDB。
3. 相邻向量数据库查询：`"vector database" "filtered" ANN SIGMOD ACORN VBASE`、ACORN arXiv ID、VBASE Relaxed Monotonicity。
4. 新颖性粗筛：`"vector join" "cardinality" optimizer NN-Map`、`"high-dimensional similarity join" "cost" optimizer`、`"inner-product similarity join" "estimation"`。

来源：arXiv abstract/history、原始全文/作者稿、PVLDB、作者发表列表、大学发表记录、USENIX、ACM/Crossref 注册元数据。搜索引擎用于发现候选；结论以原始来源与本地全文核对。

### 已核实入口与限制

| 工作 | 原始入口 | 核实事项/限制 |
|---|---|---|
| SimJoin | [作者稿](https://xiejiadong.github.io/files/paper/%5Bsigmod25%5Dvector_join.pdf)、[DOI](https://doi.org/10.1145/3725403) | 标题、作者、PACMMOD 3(3) 与正文参考格式；ACM 页面 403，出版注册元数据可访问 |
| Fast AVJ | [arXiv](https://arxiv.org/abs/2603.16360) | v2 标题、作者顺序与版本；本地 §6 NN-Map 已核；正式 venue 未核实 |
| DiskJoin | [arXiv](https://arxiv.org/abs/2508.18494)、[DOI](https://doi.org/10.1145/3769780)、[注册元数据](https://api.crossref.org/works/10.1145/3769780) | 正式刊年 2025，会议 SIGMOD 2026；本地与正式页数不同，不自动等同实验 |
| Xling | [arXiv](https://arxiv.org/abs/2402.13397) | 本轮核实预印本，正式 venue 未找到已核实证据；§VI-A 抽样/归一化/划分已核 |
| GPU-Join | [作者列表](https://jan.ucc.nau.edu/mg2745/publications/)、[DaMoN 原文](https://jan.ucc.nau.edu/mg2745/publications/GowanlockKarsin_DaMoN2019.pdf) | 同题 DaMoN 2019 与本地 2018 长稿一条工作记录，差异待核；同作者 JPDC 2019 低维工作另列候选，不强行合并 |
| IPS | [arXiv](https://arxiv.org/abs/1510.02824)、[机构记录](https://researcher.itu.dk/en/publications/on-the-complexity-of-inner-product-similarity-join/) | PODS 2016 DOI/作者/页码；原文 witness 与 all-pair enumeration 区别 |
| LSH-SS | [PVLDB 全文](https://www.vldb.org/pvldb/vol4/p338-lee.pdf)、[arXiv](https://arxiv.org/abs/1104.3212) | Definition 1、cosine/TF-IDF、§5.2/§6；Crossref 请求遇到 429，改用正式全文，不循环重试 |
| SJPC | [arXiv](https://arxiv.org/abs/1806.03313) | journal reference=TKDE 32(4):768–781 (2020)；字段 equality/Hamming 模型 |

### 候选队列（尚未纳入 papers.jsonl）

以下仅为待补查入口，候选属性不等于全文核实后的最终分类；标题/venue 不确定处保留状态。没有给这些候选填造实验或复杂度。

| 候选/入口 | 预期分类与迁移点 | 待办 |
|---|---|---|
| [Implementing Distributed Approximate Similarity Joins using Locality Sensitive Hashing](https://openproceedings.org/2022/conf/edbt/danny.pdf) | A 候选；distributed LSH join、倾斜与重复候选 | 核全部作者/正式 ID；精读输出语义、负载与召回保证；追相关理论前序 |
| [High-Dimensional Similarity Joins](https://research.google/pubs/high-dimensional-similarity-joins-2/) | A 候选；epsilon-tree 与 cost model；页面列 TKDE 2002 | 核原文/早期版本关系与 DOI；精读成本假设、距离与现代 dense 适用性 |
| [Similarity Join over Array Data](https://faculty.ucmerced.edu/frusu/Papers/Conference/2016-sigmod-array-sim-join.pdf) | A/C 待定义核对；partitioning 与 cost model | 核作者与正式信息、原始数据模型/输出定义；不能只凭标题归 A |
| [The Space Complexity of Inner Product Filters](https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ICDT.2020.22) | A 理论组件候选；确定性压缩过滤/gap guarantee | 核作者、读定理条件；区分 filter 与 join 枚举 |
| [VBASE](https://www.usenix.org/conference/osdi23/presentation/zhang-qianxi) | B 候选；直接支持复杂 vector-relational 执行/可迭代 ANN，SimJoin 也用作 baseline | 精读 threshold/analytic query 的真实语义、完整作者、接口与停止准则；不把全部 ANN 功能称为 join |
| [ACORN](https://arxiv.org/abs/2403.04871) | B 候选；可迁移 filtered graph connectivity | 核正式 venue/DOI、精读过滤定义/构图与内存；明确由 filtered top-k 到批量 threshold join 的改造 |
| GPU 作者列表中的 HEGJoin、Hybrid KNN-Join、load imbalance 工作 | A 候选；heterogeneous execution 与 per-query kNN join | 逐项建记录，不同 title/work 不并入 GPU-Join；核正文/版本 |

### 未覆盖与下一轮顺序

- 先补早期 exact/LSH/out-of-core/distributed 的引用链，避免综述仅围绕四篇现代图/SSD/learned 工作。
- 尚未系统检索所有 SIGMOD/PVLDB/ICDE/SIGIR/KDD/WWW/TODS/TKDE/TOIS 卷期，也未做 citation forward/backward 完整闭包。
- B 类尚未正式入索引；量化、更新/并发、SSD search、多向量检索与 filtered threshold join 需补查。
- semantic/LLM、集合/字符串 join 尚未形成单独背景索引；仅在明确算法/系统/评价迁移点后收录。
- 官方实现/下载链接、formal/preprint 差异、recall 分母/zero-result queries、pair conventions 与 materialization 开销仍需逐篇补证。
- 粗筛已发现更早 cost models 与 Fast AVJ §6，不能宣称向量 join 基数/优化器无人研究；研究假设新颖性仍“尚未充分排查”。
