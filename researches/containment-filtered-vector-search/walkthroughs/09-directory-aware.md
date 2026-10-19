# 第九篇 Directory-Aware／TrieHI 与当前研究的直接重合

论文：[Directory-Aware Query and Maintenance in Vector Databases](https://arxiv.org/pdf/2606.16903v1)，Mengzhao Wang、Zheng Gong、Jingpei Hu、Jiajie Fu、Maojia Sheng、Junwen Chen、Yifan Zhu。arXiv:2606.16903v1，2026-06-15；正式 venue/出版 DOI 未核实。核查日期 2026-10-19。本轮取得 15 页原始 PDF 并核读 §II–V 与结论；B 类 filtered ANN，可迁移借鉴，非 vector join。

## 先给定位 核心过滤语义已有直接前序

该工作并非仅可借鉴的平坦标签方法。其 recursive Directory-Semantic Query（DSQ）就在指定目录及全部后代中检索向量。固定一个命名空间、每个对象绑定一个逻辑目录时，记其目录路径为 $s_i$，则合格集合为

$$
\operatorname{Scope}(p)=\{o_i:p\preceq_{\mathrm{pref}}s_i\}=\mathcal D_p.
$$

这与我们当前单路径前缀过滤的核心集合语义一致。匹配按路径 segment，而非裸字符串字符；/a/ 不应误匹配 /ab/。原文允许对象直接绑定内部目录，recursive DSQ 同时包含该目录本地对象和后代对象，不能以“内部节点也有对象”作为差异。[§II-B–C、§IV-A，PDF pp.3、6–7](https://arxiv.org/pdf/2606.16903v1)。

原文还研究 non-recursive DSQ、分支排除的派生表达，以及 MOVE/MERGE 等 Directory-Semantic Maintenance（DSM）。所以“增加动态目录维护”也不能直接构成新意。本文更丰富的系统接口不表示我们的边界定义已被逐项验证：唯一 ID tie、空前缀/空结果/不足 k 和所有查询保证仍应分别核实。

## 原动机 路径展开把层级操作变成大量元数据操作

论文比较三种路径范围表示，ANN 执行器保持独立。

| 方案 | 成员表示 | 递归查询 | 主要代价 |
|---|---|---|---|
| PE-Online | 每个对象只属于其直接目录 posting | 枚举目标目录的后代路径，取 posting 并集 | 浅目录/大子树带来枚举与并集成本 |
| PE-Offline | 对象成员写入各祖先路径 posting | 查目标路径聚合 posting | 写入和空间随祖先层数增长，目录结构变化需维护路径键与祖先集合 |
| TrieHI | 原生 segment Trie，每节点存 inclusive IDs | 定位目标节点，读取其聚合集合 | 仍有祖先集合物化，但避免递归查询展开和部分子树路径重写 |

PE-Offline 已对应我们反复使用的完整前缀成员展开基线；TrieHI 则把目录表示为有稳定节点身份的拓扑对象。将 Trie 名称换成 prefix index 不构成机制差异。[§III–IV，PDF pp.4–8](https://arxiv.org/pdf/2606.16903v1)。

## TrieHI 的关键不变量与真实存储

对目录节点 v，原文存储 local segment、children 哈希表、parent 指针和 inclusive IDs；另有对象到节点的 catalog。设 $\operatorname{Local}(v)$ 是直接绑定 v 的对象，则

$$
\operatorname{Inc}(v)=\operatorname{Local}(v)
\cup\bigcup_{w\in\operatorname{Child}(v)}\operatorname{Inc}(w).
$$

recursive DSQ 定位 p 后取得 $\operatorname{Inc}(p)$。non-recursive DSQ 通过 $\operatorname{Inc}(p)$ 减去直接子节点 inclusive 集合的并集，恢复本地对象。实现使用 Roaring bitmaps。[§IV-A、§V-A，PDF pp.6–7、9](https://arxiv.org/pdf/2606.16903v1)。

因此，TrieHI **并没有消除全部祖先成员复制**。插入一个深度 t 的对象，需要沿祖先链更新 t 量级的聚合集合。其相较 PE-Offline 的关键差别还包括稳定目录身份与拓扑维护，不能把它概括成“只存树指针、完全不存祖先 posting”。

## 维护收益与复杂度边界

MOVE 可重连子树根，而不重新生成其全部后代目录路径键；但仍需从旧独有祖先的聚合中减去移动对象集合，并向新独有祖先加入该集合。MERGE 对同名冲突节点递归合并，无冲突子树可直接重连。

Table II 的 MOVE $O(t)$、MERGE $O(t+r)$ 描述节点访问/祖先聚合更新次数；r 是冲突协调中访问的源节点数。它们不是不计集合大小的总 CPU/字节复杂度。bitmap 运算、对象 catalog 处理、锁或并发开销仍需计算。极端冲突时 r 可接近整个源目录子树规模。[§IV-A–B，PDF pp.7–8](https://arxiv.org/pdf/2606.16903v1)。

递归查询的“路径遍历＋一次聚合集合访问”同样不意味着读出/转换整个集合免费；若复用不可变 bitmap 可避免立即复制，但该实际接口需查实现。原文描述重叠路径的变更串行化与一致性维护，不据此推导未给出的全系统事务/读快照保证。

## 关键区别 它是 scope 层 不改 ANN 布局

原文 §II-A、§II-D、§IV-A 明确：目录模块先确定合格 entry-ID 集合，再将其交给既有 PG/IVF 排序；不改变基础 ANN 算法或向量索引布局。

因此，仅提出“Trie 快速找合格集合，再调用 ANN”已与前序高度重合。可进一步研究的是：前缀层级是否能直接改善 ANN 中的候选生成、入口/边选择、局部视图或导航，而不只是更快提供一个完整 bitmap。

不过，本文不改 ANN 不表示后端只会先全局 top-k 再后过滤。它可将候选 ID 集合传给过滤执行器；后端的实际 in-filtering 策略仍待代码核查。不能把一个较弱的 search-then-filter 实现冒充 TrieHI 基线。

### 为什么没有逐一使用先进过滤ANN方案

用户追问后的原文补核：§V-A明确让PE-Online/PE-Offline/TrieHI使用相同Viking PG与IVF，目的为区分目录层开销与ANN行为；§II-A和§VI明确目标是目录scope层而不是替换ANN。参考文献确实列出ACORN、Filtered-DiskANN和UNG，所以不能把实验未纳入解释为作者不知道这些研究。

原文没有逐一解释排除ACORN/Curator/PathSeer等执行器的原因，也没有证明与它们组合无效；PG这一名称本身不足以认定具体搜索策略。合理的论文范围解释是固定后端研究目录表示与维护，但不能把该解释当成作者对每个基线的明确排除理由。其结果支持三种目录方案在已测后端上的比较，不支持领先所有filtered ANN方法的结论。

如果评价完整前缀检索系统，仍应把scope与执行器交叉组合：固定执行器比较范围表示，再固定相同合法集合比较过滤导航；无法直接组合的标签耦合方法另报告编码、索引重建和更新成本。LSSG v1在2026-09公开、晚于本文2026-06 v1，不能要求其初稿比较尚未公开的版本；其他方法的时间/版本需分别核查，不以时间作笼统理由。

## 实验告诉了什么 没告诉什么

§V 使用 Viking 内的 C++ 实现、PG 和 IVF 两类后端、Roaring 成员集合，报告查询/维护/存储，以及 OpenViking 系统级 QA 结果。

- WIKI-Dir：1.94M 对象、363,467 目录、平均深度 11.95、456 查询、1024 维 bge-m3。Wikipedia category DAG 被选择为每对象一个规范路径的树，不能当成原生多路径检索评测。
- ARXIV-Dir：2.76M 摘要、1024 维 mxb-ai-embed-large-v1、1,000 查询；subject 和 temporal 是两个独立命名空间，不能据此认定已评测任意多路径 OR/去重问题。
- 指标包括 nDCG@k/Recall@k、端到端与 directory-only 延迟、MOVE/MERGE、建索引时间和大小。目录层性能与 ANN 召回应分开，QA 系统收益不能归因成一个新近邻保证。

Table IV 表明递归 scope 访问中 PE-Offline 与 TrieHI 都很有竞争力，并非 TrieHI 全面更快。Table V 中 TrieHI 在两数据集上总空间最大；WIKI-Dir/PG 的 baseline、PE-Offline、TrieHI 分别为 7,925、8,213、8,326 MB。其增量为 288 和 401 MB，说明比较总索引可能掩盖目录模块本身的差异。论文资源/DSM口径排除了各方案共同 catalog，复现应明确这项，再报告实际系统总账。[§V-A–E、Tables IV–V，PDF pp.8–11](https://arxiv.org/pdf/2606.16903v1)。

本轮未独立运行原实现。向量距离、归一化、PG 的确切参数与全部计时路径仍需实现核查；不得用不同模型向量的跨数据指标直接比较算法效率。

## 对前八篇假设的收缩

| 已有候选 | 本轮之后需要保留的具体差异 |
|---|---|
| F1/F2/F3/F7：LCP 合法边、预算、桥接/分层 | 必须改变或简化 ANN 层的真实操作；仅 Trie 过滤不是差异。表达等价优化与图结构改变分别验证 |
| F4：选择性物化 | TrieHI 已物化所有节点聚合；候选可研究限预算下的选择与服务方式，但需排查已有视图/索引选择，不能直接宣称新颖 |
| F5：语义/几何局部交集 | 要与 TrieHI 完整 bitmap + 同一 Curator/后端的延迟过滤接口比较，而不只击败整库枚举 |
| F6：前缀扩散紧凑表示 | 差异在图上采样信号表示，不能把普通子树计数当作贡献；需计共享全局元数据 |
| F8：前缀入口组织 | 原生语义节点已存在；差异需落实到真实向量入口及同预算的导航收益 |

这些是尚未验证的机制假设，不是八项已确立贡献。首先选一个 ANN 层机制验证，不继续叠加多个谓词、SSD、多路径和事务要求来人为制造差异。

## 候选问题 F9 前缀约束参与 ANN 导航

本项目综合推导，新颖性尚未充分排查。可收敛为：

> 在静态单路径数据、精确前缀合法性和固定总内存下，利用对象路径的公共前缀关系组织 ANN 候选/边/入口，能否在相同最终 Recall@k 下优于原生 Trie scope 加既有过滤 ANN？

这保持用户原始查询语义，区别候选在物理执行层。原论文支持 MOVE/MERGE，但第一阶段静态实验即可验证导航机制，无需擅自把用户任务改成更新问题。

**假设：** 当不同查询深度下的合法邻居分布差异较大时，前缀合法深度相关的边/入口组织能减少无效边扫描与访问；独立 scope 层带来的收益不足以解释全部变化。该假设可被“scope 已足够便宜、普通过滤图已表现相同”否定。

最小实验先取 WIKI-Dir 的固定子集（下载/许可核查后），另用明确标为合成的等长/变长树控制深度与向量相关性；底层通用图比较统一 hnswlib。实现原文所述 TrieHI scope 并标为“依据论文机制的适配实现”，未使用 Viking 原执行器前不能声称 paper-faithful 复现。固定图/入口先比较 PE-Offline 与 TrieHI，再独立加入一个 F1/F2/F3/F7 组件；至少纳入相同 scope 下的普通过滤 HNSW、完整前缀编码的适用强基线、静态 DFS-range 归约与精确扫描。Curator 或 LSSG 原生后端若另测，必须单列后端，不能把换库当作前缀收益。

统一距离、k、查询、种子、线程、总字节和调参预算。报告 scope/入口、图导航、验证/排序时间；图边/bitmap/Trie/辅助元数据字节；边扫描、距离数、平均/P95、逐查询 Recall@k、结果填满率、过滤违规、零/不足 k。先证明增量来自导航，再讨论更大规模/动态维护；静态 DFS 顺序的 MOVE 重排代价若未测应显式不适用。

失败条件：原始图过滤已有足够合法边；宽前缀等价普通 ANN；浅树几乎无共享收益；深前缀候选极少时扫描占优；额外边和路由抵消省下的访问；效果只来自不公平地剥夺基线 bitmap 或入口。若出现这些结果，应放弃或收缩机制，而不是仅以已有问题名称为创新。

作者明确 future work 是更丰富的 path predicates 与混合语义/层级条件的 cost-aware planning（§VII）；本项目 F9 为文献综合推导，不冒充作者 future work。

## 常见论证纠偏：数据库是否必须知道层级目录

2026-10-19按用户提供的三点论证重核§III–V。结论是：原生目录层是一个设计选择，不是前缀搜索语义的必要条件。静态递归查询可通过正确编码的包含过滤执行；原始片段数组的无序contains不等于前缀，必须保留位置或完整路径标识。

1. **写放大有表示前提。** 若每对象显式保存可变完整路径及祖先成员，MOVE可能更新大量对象；但原文PE-Online主要重映射目录键，成本关联受影响目录数而非必然逐一改所有向量实体。还可设计稳定directory ID、对象绑定目录ID和独立目录服务，避免每次移动重写全部对象；该备选为本项目设计推导，不是原文已实现方案。原生TrieHI的MOVE也需维护旧/新children映射、parent、祖先inclusive集合及catalog一致性，不是只赋一个parent指针。bitmap成本依移动集合和压缩表示而变，不能无条件宣称由几十秒降至微秒。
2. **非递归查询不是数组索引的表达能力缺陷。** 直接parent_path或parent_directory_id倒排即可精确查询当前目录；PE-Online原文已有直接posting查询。仅维护祖先聚合时才需要子集合并差。TrieHI原文虽然用Local(v)记号描述不变量，却未在列出的四个节点字段中独立物化Local；其non-recursive DSQ同样计算Inc(p)减去直接子节点Inc的并集，不能把直接Local读取写成该文现有机制。
3. **路径字符串共享不等于消除祖先成员。** TrieHI逐节点维护inclusive IDs，每对象仍进入祖先聚合，逻辑成员数依路径深度增长。两种方案都可用Roaring等压缩，物理字节不能只据逻辑项数推断。§V-D/TableV明确TrieHI总索引空间在两数据集上最大，不能宣称它天然更省全部索引存储。

准确动机是：通过稳定目录节点和原生拓扑，提高递归scope复用及结构变更的一致性与维护效率；这是空间、查询和维护之间的折中。性能大小、动态操作频率与集中维护是否必要，需由实际工作负载与实现证明，不能称所有属性索引都有“致命缺陷”。

## 本地材料与待补查

[PDF](../../../papers/prefix-vector-search/2606.16903-directory-aware/paper.pdf)、[全文](../../../papers/prefix-vector-search/2606.16903-directory-aware/paper.md)、[阅读报告](../../../papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md)。新增全文索引，保留早先仅摘要候选快照并追加本轮状态快照。数据和 OpenViking 入口来自原文，当前实现与论文版本一致性尚未核查；没有运行 ANN 性能实验。
