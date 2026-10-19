> 后续语义澄清（2026-10-19）：用户现明确查询标签全部包含于每个返回对象的标签，当前入口改为[包含过滤向量搜索](../containment-filtered-vector-search/index.md)。本文保留此前 overlap 研究设计，不能作为 AND 语义的直接替代。

# 结构化重叠属性过滤的文献比较与问题细化

核查日期：2026-10-18（Asia/Shanghai）。本文为研究设计笔记，原问题是 filtered top-k search，归入 B 类可迁移借鉴；不是 vector join。以下问题细化为本项目综合推导，非三篇论文共同提出的 future work，新颖性尚未充分排查。

## 结论

原问题是 overlap-filtered ANN 的结构化子类，不宜直接改名为一般 overlap 问题并作为新问题。建议保留外部分类树，将单节点查询扩展为少量节点子树的并集，以紧凑谓词、最终不同对象 top-k 和端到端资源约束定义研究范围。关键比较是：在相同预算和质量下，保留层级结构是否优于展开查询标签、展开对象祖先标签或将标签转为范围记录的强归约。

## 三篇论文的身份与证据

| 工作 | 完整作者顺序 | 年份与版本 | 本轮核查程度 |
|---|---|---|---|
| [UniFilter](https://doi.org/10.1145/3769765)，Beyond Vector Search: Querying With and Without Predicates | Jiadong Xie; Jeffrey Xu Yu; Siyi Teng; Yingfan Liu | PACMMOD 3(6), Article 300, 1–26，2025-12；会议 SIGMOD 2026，作者主页列出 | Crossref/作者机构记录核身份与摘要；原文 PDF/HTML 返回 403，作者网站未发现全文。仅取作者综述相关页，不充当 UniFilter 原文精读 |
| [Curator](https://doi.org/10.1145/3786635)，Curator: Efficient Vector Search with Low-Selectivity Filters | Yicheng Jin; Yongji Wu; Wenjun Hu; Bruce M. Maggs; Jun Yang; Xiao Zhang; Danyang Zhuo | PACMMOD 4(1), 2026／SIGMOD 2026；所读 2601.01291v3 | 原始 PDF、§4.2 pp.6–7、§6.2 Fig.9 pp.10–11 |
| [ELI](https://doi.org/10.14778/3785297.3785304)，Elastic Index Selection for Label-Hybrid AKNN Search | Mingyu Yang; Wenxuan Xia; Wentao Li; Raymond Chi-Wing Wong; Wei Wang | PVLDB 19(4):617–630，**2025**；Crossref print=2025-12-01、online=2026-03-18；会议 VLDB 2026。预印本 v1=2025-05-06，所读 v2=2025-12-11 | 正式 14 页与 v2 18 页分别保存；overlap 证据在 v2 A.2/A.3 p.16；不将 online 年与刊年混同 |

### UniFilter 的能力与尚未确认的细节

[作者机构原始摘要](https://research.cuhk.edu.hk/en/publications/beyond-vector-search-querying-with-and-without-predicates/)支持它统一处理无谓词、类别和数值谓词。LSSG §2.2 Table 2/相关工作把它归入 label-partition 并列出 containment/overlap；这是 LSSG 的分类，不是本轮已核到的 UniFilter 原文执行证据。

[Jiadong Xie 等作者综述](https://xiejiadong.github.io/files/paper/vector_survey.pdf) PDF p.20 的 UniFilter 段落另明确描述：辅助谓词 AND/OR 图与原向量 proximity graph 联合；类别用 AND graph、数值用 OR graph，查询通过覆盖谓词节点与 cross-edges 导航。综述是作者来源，但仍不等同于原文；不能仅依据 LSSG 的粗分类推断 UniFilter 为每个标签组合构建独立 ANN 索引，也不能把“组合爆炸”写成已证明的 UniFilter 特定空间下界。其离散多标签 OR 执行、coverage 复杂度、实验及理论假设均待原文确认。

该工作暂作有核实书目的全文待补候选；没有为通过本地索引检查而制造 paper.md，也没有加入要求完整全文的 papers.jsonl。

### Curator 支持未知谓词，但需对齐输入成本

Curator v3 §4.2 把任意谓词满足的对象子集视为 virtual label，借助共享 hierarchical k-means tree 和子树连续的向量 ID，在有序合格 ID 列表上递归划分临时索引（Algorithm 2），之后沿用单标签搜索。不需要为每个组合预建独立索引，也不是每次重新聚类全部向量。该节还提出按预计访问子树延迟执行过滤和临时构造的优化，不能假设其唯一执行方式为全量 eager 构造。

§6.2 Fig.9 实验确实包含两标签 OR 和 AND。方法支持更一般谓词与论文实际测试过多少标签，是两个不同结论。图中 Curator (Idx) 为预先将该谓词作为 virtual label 建索引、在线免构造的版本，Curator 为查询时构造临时索引的版本；二者并非同一执行轨迹简单减去构造时间。但 §4.2 预期外部关系数据库提供有序合格 ID，不能据此确认计入了完整 bitmap/posting/filter 生成成本。

low selectivity 在 Curator 中是合格对象比例低，本研究统一使用 pass ratio，不混用不同文献对 selectivity 的相反术语。不能用“大查询标签数”直接推断 Curator 更差：OR 增加分支可能扩大可行集，必须控制比例、标签相关性和向量分布后评测。

### ELI 的 overlap 扩展与原作者明确 future work

ELI v2 A.2 p.16 将 overlap 拆为每标签一个 singleton containment 子查询，独立搜索再合并。A.3 Exp-7/Fig.14 p.16 已评测该扩展。原文明确未来拟将 overlap 纳入 greedy index-selection 的优化工作负载，而非仅在查询时扩展。不能因此声称其他工作均未直接优化 overlap，LSSG 正是必须加入的直接对照。

ELI §3.1 Lemma 3.2/A.1 的 O(C+k/c) 为带假设的期望代价：标签与向量位置独立、匹配点分布及后续邻居摊销常数枚举。全局 elastic factor 不能在任意相关分布上证明访问序列的局部命中率。§3.2 用成员数近似索引成本，并将已有 top index 视为零增量成本；本研究总预算须把它单独列出后计入。

拆成多路意味着有多次启动/搜索/合并的潜在开销，但不能由此推出延迟严格随查询标签数线性增长，也不能断言一定慢于 Curator；空标签、共享索引、缓存、并行和可行集密度都会改变结果。

## 递进的问题细化

### 第一步 明确一般 overlap 语义

令 $\Sigma$ 为标签全集，数据为 $\mathcal D=\{(\mathrm{id}_i,\mathbf x_i,L_i)\}_{i=1}^{n}$，$L_i\subseteq\Sigma$。一般 overlap 查询为 $Q=(\mathbf q,F,k)$，可行集合为

$$
\mathcal D_F=\{o_i\in\mathcal D\mid L_i\cap F\ne\varnothing\}.
$$

答案为该集合内按距离与 ID 排序的不同对象 top-k。这里 overlap 是“至少一个标签命中”，不表示“至少 t 个标签命中”或 Jaccard 阈值。后两者会改变问题语义，不能作为未经讨论的附加条件。原有层级查询对应 $\Sigma=V$、$F=\operatorname{Desc}^{+}(u)$。

### 第二步 保留分类树约束并支持紧凑子树并集

查询输入改为 $Q=(\mathbf q,U,k)$，其中 $U\subseteq V$ 是有限非空节点集。若 $U$ 同时包含祖先与后代，删去后代节点不改变覆盖范围，因此规范化为 antichain，即任意两节点互非祖先。定义

$$
F_T(U)=\bigcup_{u\in U}\operatorname{Desc}^{+}(u),
\qquad
\mathcal D_U=\{o_i:L_i\cap F_T(U)\ne\varnothing\}.
$$

$|U|=1$ 恢复原问题。可将 $|U|\le b$ 作为受控工作负载条件，b 是实验参数；同时分别扫描 $|F_T(U)|$，检验紧凑输入与展开标签数量之间的差异。输入接收 $U$ 与静态树，不要求用户预先发送完整 $F_T(U)$。索引可以选择展开，两种成本必须真实计入；不通过禁止强基线制造优势。

将对象祖先闭包定义为 $A_i=\bigcup_{v\in L_i}\operatorname{Anc}^{+}(v)$，则存在精确等价

$$
L_i\cap F_T(U)\ne\varnothing
\quad\Longleftrightarrow\quad
A_i\cap U\ne\varnothing.
$$

因此祖先展开把大查询标签集缩成小 U 的 overlap，尤其 singleton U 可转单标签过滤。这是必须保留的强对照，不能只比较 ELI-flat 在 $F_T(U)$ 上的巨大拆分。DFS 将 U 表达为至多 $|U|$ 个子树区间；多标签需要多值记录与 distinct-object 搜索，不可直接套单数值范围结论。

### 第三步 规定对象输出与整体资源约束

固定对象级唯一 ID、相同距离和并列规则；精确结果数为 $k_Q=\min(k,|\mathcal D_U|)$，近似结果无重复且严格满足过滤。Recall@k 以最终不同对象计算，不能用桶平均召回或填满率替代；空可行集单列，不进入非空查询平均召回分母。

固定常驻内存预算 $B$，包括原始向量、直接/展开标签、posting/bitmap、全局索引、子索引及路由元数据；共享向量只计一次。允许不同表示和缓存布局，但均须纳入实际字节。另给并发数 p 下的总峰值内存上限 $B_{\mathrm{peak}}$，临时索引、候选和 visited/去重集合均计入。不能只比较成员数量或把某方法的已有全局索引视为免费。

查询计时从接收紧凑谓词开始，到输出最终对象结果结束。按不重叠的阶段或依赖图记实际 wall time；顺序执行时可记为

$$
T(Q)=T_{\mathrm{decode}}+T_{\mathrm{qualify}}+
T_{\mathrm{prepare}}+T_{\mathrm{search}}+T_{\mathrm{merge}}.
$$

分别对应结构解析、合格 ID 生成、临时索引/计划准备、向量搜索、最终去重排序。异步/并行执行采用端到端 wall time，不能简单相加重叠计时。持久数据结构计入 build/常驻内存；跨查询缓存按同一规则列冷/热两种状态。历史工作负载训练与测试分离，新组合查询不要求离线全物化，但允许各基线在预算内自行选择物化。

### 第四步 选择一个主研究问题

推荐主问题：**在紧凑子树并集 overlap 查询下，如何利用外部层级与对象交叠信息共享索引和搜索工作，在固定总内存与最终对象召回约束下减少端到端延迟？** 首版静态树、静态数据、固定 encoder/距离、单机内存；不同时叠加更新、SSD、分布式与任意 Boolean 公式。

该问题是已有 overlap 检索的工作负载与资源细化，不是新谓词。必须证明具体共享机制带来收益，而不能以新增预算、层级或去重三个条件本身作为创新。若主要收益由 pass ratio 或简单缓存解释，应收缩或放弃该论点。

## 候选假设 最小实验与反例

### H4 紧凑谓词与索引布局联合利用层级

来源：本项目综合推导。与 ELI 一般索引选择、Curator 共享聚类树、UniFilter 谓词图和 LSSG 标签 tier 区分，候选机制需明确怎样使用给定 taxonomy，而非仅把节点编号作为普通标签。

假设：当 $|U|$ 小而展开 $|F_T(U)|$ 大、查询跨层级时，保留 taxonomy 的索引共享能在相同实际内存和 Recall@k 下减少展开、predicate preparation 与搜索开销；收益应同时超过 ancestor-expanded 基线，而不仅超过 flat query expansion。

最小实验：固定一份向量数据，用真实论文 taxonomy（版本/许可待核）或独立标为合成的树。首先单标签验证包含与范围归约，再加入同分支/跨分支多标签。固定 k 与距离，扫描树深、$|U|$、展开标签数、标签频率和向量—标签相关性；用完整过滤扫描生成小规模真值。工作负载至少包含未在物化选择阶段出现的节点组合，测试分支与训练分离。

反例：浅树、查询仅叶子、压缩 bitmap 已很便宜或祖先展开很小，则层级机制可能没有净收益。预期运行时间收益必须超过新增路由开销。

### H5 面向不同对象的跨分支搜索共享

来源：本项目综合推导。定义可行集合非空时的成员交叠度

$$
\mu(U)=\frac{\sum_{u\in U}|\mathcal D_u|}{|\mathcal D_U|}.
$$

规范化 antichain 的节点子树互不交，但跨分支多标签对象使 $1\le\mu(U)\le|U|$。单标签时必为 1。该统计需在基准生成或离线分析中计算，不把测试真值统计免费提供给在线算法。

假设：联合 visited/距离缓存与对象级候选预算，相比已启用简单缓存的多路搜索仍能减少候选重复访问与搜索启动，并在相同最终 recall 下减少时延。只证明距离缓存有效不足以构成独立新意。

最小实验：尽量固定可行集规模、$|U|$、每对象标签数、各分支边缘频率和向量分布，改变跨分支交叠；同时报告无法严格同时固定的因素，避免把扩大 OR 集合造成的简单难度变化归因于交叠。对比独立搜索、加 ID/距离缓存、共享候选前沿三个消融；记录 unique/total visited、distance computations、候选重复率、局部搜索调用数、最终 Recall@k 与 P95。

反例：精确局部 top-k 合并已保证正确性，标签重叠不带来新的结果语义；纯缓存足够时不存在独立共享机制收益。ANN 未返回的跨路最近邻也不能仅靠去重恢复。

### 必须对齐的基线与指标

核心：ELI-flat-overlap、ELI-ancestor-overlap、Curator 虚拟标签/临时索引、Curator ancestor-singleton/OR，以及 LSSG 原生 overlap。UniFilter 待拿到原文/可执行实现后作为基线，未实现前列方法比较，不能编造结果。

背景：ACORN+精确 bitmap、过滤扫描、逐直接标签索引合并、DFS 多值区间索引+distinct、预算允许的部分子树物化。祖先展开不一定复制向量，需按真实实现计费。不能只比较朴素四方案。

统一 CPU/GPU/线程、原始向量精度、归一化/距离、k、过滤与并列规则。分别报告 end-to-end latency/QPS、P95、最终对象 Recall@k、逐查询 recall 分位数/违约率、零/不足 k、过滤违规、填满率、总常驻/峰值字节、build time、qualifying ID/temp build 时间、距离数、重复访问与 cold/warm 状态。平均召回目标是实验可行性约束，不是理论逐查询证书。

## 额外约束的取舍

首选新增约束是紧凑的结构化查询输入、不同对象语义、总/临时内存及端到端成本。暂不加入至少 t 个标签命中、每类别配额、逐查询形式化概率保证或强制动态 taxonomy：它们分别改变谓词、输出目标或保证等级，需要单独问题与证据。若后续确认仅做普通 OR 与现有方法几乎无差异，再考虑有业务动机的变体，而非为绕开前序随意堆约束。

## 本地证据与待办

- [ELI 阅读报告](../../papers/filtered-ann/2505.03212-eli/paper.report.md)、[v2 全文](../../papers/filtered-ann/2505.03212-eli/paper.md)、[正式稿](../../papers/filtered-ann/2505.03212-eli/published-2025.pdf)、[alphaXiv 页证据](evidence/eli-overlap-alphaxiv-2026-10-18.xml)。
- [Curator 阅读报告补核](../../papers/filtered-ann/2601.01291-curator/paper.report.md)、[v3 全文](../../papers/filtered-ann/2601.01291-curator/paper.md)。
- [UniFilter Crossref](evidence/crossref-unifilter-2026-10-18.json)、[作者综述相关页](evidence/unifilter-author-survey-2026-10-18.md)；原文全文与 overlap 具体执行未核实。
- 新颖性检索仅为有限同义词/引用线索，尚缺 UniFilter 原文、结构化 OR/区间并集/子集索引/多值 distinct top-k 的系统引用闭包；未运行算法实验，未验证性能改善。
