# 属性集等价与包含：同时支持的filtered ANN工作

日期：2026-10-20。状态：文献与代码接口核查，未运行新性能实验。用户已明确确认：equality为完整集合相等 `Lx=Q`；containment方向为 `Q⊆Lx`。

## 结论与定义

**最直接的两篇是UNG和LSSG；ACORN、PathSeer能通过通用精确谓词表达二者；ELI正文明确声明等价扩展，但本轮未核其完整实现路径。** “支持两种任务”“一份索引同时服务”“在两种任务都达到优秀性能”是三个不同结论。

对象为向量与去重标签集合 `(x,Lx)`，查询为 `(q,Q,k,mode)`，先按mode定义可行集，再以原始向量距离排序。标签顺序不构成语义；空集时containment为全库，equality只匹配空标签对象，二者必须分别测试。过滤条件精确，ANN允许遗漏真近邻；不是vector join。集合重复项应先规范化。

例如Q={A,B}，equality只接纳{A,B}，containment还接纳{A,B,C}；两者都拒绝{A}。有限集合下 `Lx=Q ⇔ Q⊆Lx 且 |Lx|=|Q|`；这是本项目数学归约，不表示任一包含索引已有高效的等价导航。

## 核心文献与证据

| 论文 | 两种语义的支持层次 | 索引与执行证据 | 实验/限制 |
|---|---|---|---|
| **UNG — Navigating Labels and Vectors: A Unified Approach to Filtered Approximate Nearest Neighbor Search**；Yuzheng Cai、Jiayang Shi、Yizhuo Chen、Weiguo Zheng；PACMMOD 2(6), Article246, **2024** / SIGMOD2025 | 作者官方实现明确支持equality、containment | 相同完整标签集分组；equality入口`find_exact_match`；containment入口`get_min_super_sets`。README构建区分`general`与`equality`；equality构建不设LNG和跨组边，不能宣称现成general图无改动支持equality | 本轮未取得论文全文，依据官方README、作者海报及核心代码；未运行。作者海报有两类工作负载，但不据此补造正文定理/性能数字。[DOI](https://doi.org/10.1145/3698822)、[官方实现](https://github.com/YZ-Cai/Unified-Navigating-Graph) |
| **LSSG — Fast Label-Filtering Approximate Nearest Neighbor Search via Progressive Label Set Stratification**；Ziqi Wang、Jingzhe Zhang、Shuo Shen、Wei Hu；**2026预印本** | 原文明确，且为同一索引 | §2.1明确`Lq=Li`、`Lq⊆Li`；§3.1最严格标签层只连接相同标签集，其他层用于较宽关系；§3.4查询执行；官方README明确三类谓词one index | §6.3分别评测equality/containment/overlap。标签资格精确不等于ANN recall=1；理论依赖标签模型，未复现。作者标SIGMOD2027，正式出版DOI未核。[原文§2.1](https://arxiv.org/html/2609.15058v1#S2.SS1)、[代码](https://github.com/nju-websoft/LSSG) |
| **ACORN — Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data**；Liana Patel、Peter Kraft、Carlos Guestrin、Matei Zaharia；PACMMOD 2(3), Article120, **2024** | 原文通用谓词；两种集合实例化是研究者归约 | §3.1任意`p_q`；§5在同一图上执行predicate subgraph traversal。用完整集合相等/包含callback或精确allow-list即可表达 | 不宣称原论文单独实验了这两种集合任务；谓词、bitmap准备和额外元数据成本应计入。原生ACORN与产品变体分开。[原文](https://arxiv.org/html/2403.04871v1#S3.SS1) |
| **PathSeer — Adaptive Neighbor Handling for Efficient Filtered ANN Search**；Zhiyue Li、Guangyan Zhang、Ruochun Jin、Bojun Li、Xiaoguang Ren、Wenjing Yang；PACMMOD 4(3), Article221 / SIGMOD, **2026** | 原文predicate-agnostic；两种集合实例化是研究者归约 | 原文PDF p.2 Introduction说明属性与索引独立、支持任意查询谓词；据此以精确set callback或bitmap表达二者 | 不把自适应邻居处理改称简单beam缩放；不宣称原文逐一实验两种集合语义。论文原文镜像本地已存，网页重取失败；官方仓库当前可读，未运行。[DOI](https://doi.org/10.1145/3802098)、[代码](https://github.com/lizhiyuell/PathSeer) |
| **ELI — Elastic Index Selection for Label-Hybrid AKNN Search**；Mingyu Yang、Wenxuan Xia、Wentao Li、Raymond Chi-Wing Wong、Wei Wang；PVLDB19(4),617–630, **2025刊年** / VLDB2026 | containment为主问题；原文Remark明确声明set-equality扩展 | §2.1 `S(Lq)={x:Lq⊆Lx}`，Remark写`Lq=Li`可用扩展解决并指向附录 | 本轮所读v2 HTML不含所需附录细节，现成equality代码及同索引部署未核。不能把声明升级为完整原生支持或相同性能保证。[原文Remark](https://arxiv.org/html/2505.03212v2#S2.SS1) |

以上均为B类：自身是filtered ANN、相对vector join为可迁移借鉴，不称直接vector join。

## 易误收的背景工作

- **RWalks**原文§2.1 Definition2把“Equality”和“AND”均写为 `∀j, AQ[j]=1 ⇒ ACi[j]=1`，只约束查询中出现的标签；按当前定义是containment，不排除额外标签。原文还有OR/NOT，理论上可另做完整谓词，但本轮未核该组合的实现，不列为原文已验证完整集合等价。[原始稿](https://raw.githubusercontent.com/AnasAito/rwalks-reproduce-v2/master/RWalks_paper_public.pdf)。
- **Filtered-DiskANN / Packing**不能仅凭“多标签/equality”字样列为两种任务均原生支持。原始单标签/OR、AND、完整集合相等必须分开核验；旧LSSG比较表不能代替前序论文原定义。
- **UniFilter / Curator**可研究谓词扩展，但本轮没有原始正文证据证明完整`Lx=Q`与`Q⊆Lx`两模式共享执行，暂标待核。单值类别属性相等与完整标签集相等不同，固定长度多属性相等也不自动覆盖任意标签集。

## 对当前LES与帕累托实验的影响

前述node-Exact定义为`u∈Lx`，Subtree为`Lx∩Desc+(u)≠∅`。二者并不直接等于本轮确认的完整集合equality/containment；只在明确的路径/标签编码及特殊约束下可归约。旧结果保留原任务标签，不能重新命名为本轮两种集合查询。

后续先采用原始去重标签集，分别构造equality与containment精确GT。主基线优先级：LSSG-IVF/MinHash、UNG（分mode构建记录）、ACORN原生变体；PathSeer为通用执行对照，ELI待核equality实现后进入双语义主表。PSG与LES继续保留作自建对照，但要先实现/核对目标谓词。

分别绘制每语义的预算–QPS–recall前沿；“一份索引同时服务”再加mixed-workload实验，UNG若部署两份索引则两份成本都计入，向量确实共享才只计一次。还需同构建预算和参数调优机会，不能只扫LES/PSG后宣称覆盖本任务SOTA。

## 本轮新增/修订、验证与待办

保存六方法能力证据于[data/equality-containment-evidence.json](data/equality-containment-evidence.json)，修订已有论文索引的集合语义备注；UNG保持有证据候选，不伪造缺失的全文记录。未新增论文performance数值、未运行实验、未补造理论保证。搜索覆盖是定向能力核查，不穷尽全部filtered ANN文献。

待办：UNG原文全文与混合部署接口；ELI的equality附录及实现；LSSG正式发表状态；各方法真实新查询上两种谓词的公平Pareto扫描。既有`tools/research_index.py`在当前checkout不存在，本轮仅做JSON、重复id、本地证据路径及链接检查，不能声称运行该校验器。
