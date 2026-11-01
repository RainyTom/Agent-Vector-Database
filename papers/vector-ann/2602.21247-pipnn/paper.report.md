# PiPNN：分区产生候选，HashPrune 在线稀疏化

## 1. 结论与身份

**PiPNN 优化的是图 ANN 索引构建，不是提出一种新的阈值 vector join。** 最值得迁移的组合是“重叠小分区 + GEMM 批量距离 + 有界在线邻接表”；HashPrune 保留方向多样性服务于导航，不能作为保留全部阈值结果的安全剪枝。

| 项目 | 核实结果 |
|---|---|
| 标题 | PiPNN: Ultra-Scalable Graph-Based Nearest Neighbor Indexing |
| 作者（原顺序） | Tobias Rubel; Richard Wen; Laxman Dhulipala; Lars Gottesbüren; Rajesh Jayaram; Jakub Łącki |
| 机构 | University of Maryland、Google Research；作者逐项机构见 PDF 首页 |
| 正式出版 | KDD 2026，Proceedings of the 32nd ACM SIGKDD Conference on Knowledge Discovery and Data Mining V.2，4092–4102 |
| DOI | [10.1145/3770855.3817891](https://doi.org/10.1145/3770855.3817891) |
| 日期 | Crossref：online 2026-08-08，print 2026-08-09；arXiv 首稿 2026-02-17、v2 2026-05-24、v3 2026-07-10 |
| 实际阅读版本 | [arXiv v3](https://arxiv.org/html/2602.21247v3)，18 页含附录；[本地 PDF](paper.pdf)、[分页文本](paper.md) |
| 代码 | 作者主页链接的 [ParAlg/PiPNN](https://github.com/ParAlg/PiPNN)，固定公开 commit `443a328dc71a3b7be753d52ca2b3c9a4ba77f3fd`；仅核入口/README，未编译或运行 |
| 分类 | `topic=vector-ann`；B，可迁移借鉴；不直接支持 vector join、属性过滤或动态目录维护 |

用户开头的“PiANN”与所给标题不一致；按标题、正文和作者来源确认本次目标为 **PiPNN（Pick-in-Partitions Nearest Neighbors）**，不把 PiANN 登记成论文正式别名。PDF 页眉存在带 “A Framework for” 的题名变体，以首页/arXiv/Crossref 标题为准。正式 ACM 页面和 PDF 本轮返回 403；书目由 Crossref 核实，**没有声称逐项比对正式全文与 v3**。元数据与 SHA 见 [日期化记录](metadata-2026-11-01.json)。

## 2. 问题与输出：三种图不能混同

- 输入：静态高维向量集合和构建参数；主实验包含 L2 和 MIPS。没有要求所有向量单位归一化，不把 MIPS 默认改写成 cosine。
- 索引输出：稀疏、有方向多样性的 **navigation graph**；建好后仍用 beam search 服务单查询近似 top-k。
- 叶子中间结果：每个小分区内部的局部 k-NN 图，双向化后仅用于产生候选边；默认局部 `k=2`，不是最终搜索的 `k=10`。
- 下游输出：对所有点执行查询产生 `k=10`、边 recall 至少 95% 的近似 k-NN 图。可视为逐点 top-k 的 self 检索任务，但不是全局 top-k pairs，也不是阈值内全部 pairs；正文未在此明确完整 self-match 排除/并列距离处理细节，待代码核对。
- 准确性：ANN 召回为实证指标；论文未提供任意数据下的端到端 Recall@k 下界或阈值 join 完整性保证。

来源：v3 §2、§4.2、§5.2，PDF pp.2、6、8–9；[原文 §2](https://arxiv.org/html/2602.21247v3#S2)。

## 3. 为什么分区之前还需要一个新剪枝器

HNSW/Vamana 的增量构建先在当前图上 beam search 找候选，再用 RNG/RobustPrune 类规则删冗余边。问题不只是距离次数，还包括依赖链长、随机访存和 cache miss。

重叠分区可以绕过构建时的图搜索，却引入另一压力：同一点在许多叶子里反复获得候选。全部攒起来再剪枝会产生大中间结果；逐叶子反复 RobustPrune 则既贵，又可能出现“早先用 y 删除 z，后面 y 自己被删”的顺序依赖。HashPrune 把合并规则改造成每点有界状态，才让分区候选流成为可用的构图路径。这里的 **online 指候选边流式到达**，不是已证明支持向量数据库的动态 insert/delete。

来源：v3 §2.1、§3，PDF pp.3–4。

## 4. HashPrune：桶内最近代表，再取有界数量的桶

对源点 p、候选 c，取 b 个独立随机超平面，拼接残差投影的符号：

$$h_p(c)=\operatorname{concat}_{i=1}^{b}\mathbf{1}[H_i\cdot(c-p)\geq0].$$

两个非零残差夹角为 θ 时，随机超平面哈希给出

$$\Pr[h_p(c)=h_p(c')]=(1-\theta/\pi)^b.$$

这表达的是**方向碰撞概率**，不是漏报概率界、邻居覆盖保证或 RobustPrune 条件的等价形式。相近方向更容易碰撞；桶也不是一个确定半角的“硬锥”。

每点维护容量 L 的 reservoir：

1. 相同哈希已存在：只有新候选更近才替换。
2. 不碰撞且没满：插入。
3. 不碰撞且已满：只有新候选比当前最远候选更近才淘汰后者。

在距离无并列（或使用统一确定性总序）时，可把结果理解成：对全部候选按 hash 取最近代表，再取这些桶代表中最近的 L 个。状态大小是 **至多 L**，更精确地说至多 `min(L, 不同哈希数)`；碰撞时并不保证填满。原 Algorithm 3 的输出行写作 `min(|V|, L)`，不能机械地当作精确基数承诺。

### 4.1 历史无关性的范围与并列距离

Theorem 3.1 / Lemma A.2 论证固定候选集、哈希函数和容量下的 history independence。整套 PiPNN 的确定性另依赖固定随机性、分区/merge 的调度无关实现（Lemma A.1）。这些性质不保证导航图连通，也不保证近邻召回。

**研究者核对：** Algorithm 3 使用严格 `<`，若同桶两个不同 ID 与源点等距，先来的 ID 会保留，因此字面规则不足以保证唯一的邻居 ID 集合。bf16 距离存储又使并列处理值得检查。这不是已确认的官方实现 bug：本轮没有核读该实现的比较器/锁/数值路径。

本轮独立有限构造的实际输出：5 候选、容量 2、距离互异的全部 120 种到达顺序均输出 `{b,c}`；2 个同桶等距候选、容量 1 的两种顺序分别留下 `{a}`、`{b}`。见 [实测 JSON](../../../researches/vector-join/2026-11-01-exps/data/pipnn-hashprune-semantics.json) 与 [复现代码](../../../researches/vector-join/2026-11-01-exps/experiments/pipnn-reading.md)。它只检查字面规则边界，不验证作者实现或一般定理。

### 4.2 表示、比较成本与内存边界

每槽 8 字节：4B 点 ID + 2B hash + 2B bf16 距离。先计算每点 b 维 Sketch，再通过两个 Sketch 坐标差的符号恢复残差哈希，无需每次重新读取完整 d 维向量；默认 b=12。

论文给出未满/碰撞插入的 `O(log l)` **比较次数**，满且不碰撞的最远候选更新可为 `O(L)`；数组保持 hash 有序，不能把二分定位的比较界直接当作数组移动的完整时间界。最远候选缓存能减少拒绝较远新候选时的重复扫描。作者剖析称 reservoir 维护通常不超过总构建时间 5%。

候选容器存储为 `8nL` 字节（只计槽位），但还要加向量、sketch、重叠桶成员、叶子矩阵、并行临时空间与最终图。增加副本不扩大每点 reservoir 上限，**不等于整个算法没有额外空间或所有阶段峰值 RSS 不变**。Table 6 是实际总内存的更好证据。

来源：v3 §3，PDF p.4；§A.8 p.16；[定理原文](https://arxiv.org/html/2602.21247v3#Thmtheorem1)。

## 5. PiPNN 的候选产生与最终图

### 5.1 Randomized Ball Carving（RBC）

子问题随机采样 leaders，每个点分给若干最近 leaders；递归处理过大分组。leader 数按子问题大小的比例选取，并有通常 1000 的上限。单次无 fanout 的划分不重叠；通过 replication 或 fanout 增加近邻共叶子的机会。

- replication：独立重复整个过程，上层扫描也重复。
- 单层 fanout：第一层一次算完点到 leaders 的距离，把点分发到多个子问题。
- 多层 fanout：如第一层 10、第二层 3，把一部分复制推迟到小子问题，减少上层重复工作、top-k 聚合和 group-by 成本。
- 叶子上限一般 1024–2048；太小分区在不超过上限的约束下合并。

这是几何分区的递归层级，不是属性树/DAG。重叠旨在缓解边界损失，论文没有给任意分布下真近邻一定共叶的保证。多层 fanout 与 replication 的“质量相近”来自所测数据，不能当一般等价定理。

### 5.2 叶子计算与选边

每个叶子计算完整局部距离矩阵，**不是整个数据集的 n×n 矩阵**。对平方 L2，可用恒等式（解释性推导）

$$D_{ij}=\|x_i\|^2+\|x_j\|^2-2x_i^\top x_j$$

组织为 GEMM；MIPS 本来就依赖内积，但不等于欧氏三角不等式成立。作者实现使用 Eigen 与 Highway 的向量化排序/部分排序。

矩阵之后并不将所有叶内 pairs 喂给 HashPrune，而是构建局部 k-NN，再加入反向边。**局部 k=2 的双向图不意味着每点出度≤4**：反向近邻入度可很大；总体候选边数才有相应线性上界。附录 A.3 比较 directed / inverted / bidirected k-NN、度受限 MST 和 leaf 内 RobustPrune；bidirected k-NN 的前后向边都对导航有价值。加大 k 会降低访问节点数，但每次访问更贵，不能只优化 visited-node count。

### 5.3 最终 RobustPrune

HashPrune 已限制候选列表，末尾再用连续参数 α 的 RobustPrune 更细地调图密度。算法接口允许关闭，但作者主实验启用；不能说主实验加速结果全靠 HashPrune 且不需要最终 prune。

来源：v3 §4、§A.1–A.5，PDF pp.5–6、11–15。

## 6. 理论：有证明的是什么，没证明的是什么

§A.7 在“随机 leaders 使子问题大致均衡”的假设下，给出作者的总期望工作量表达式：

$$O\!\left(n\left(df\ell\log_{\ell}n+cdf+fR+R^2\right)\right).$$

此处统一记 n=点数、d=维度、f=累计 fanout 上界、ℓ=每子问题 leader 数、c=最大叶子规模、R=度/候选规模参数（原附录用 m，勿与哈希位数混淆）。分区、叶子距离矩阵、HashPrune、最终 prune 分别贡献对应项。它不是对恶意不均衡数据无条件成立的线性构建保证，也不是 wall-clock 并行加速界。

还需注意该附录的记账简化：写了每候选约 12 次 `O(d)` 投影而后将 HashPrune 记作 `O(fnR)`；最终 RobustPrune 的 `O(nR²)` 也没有展开高维距离的完整代价。§3 实际预计算 sketch；本轮保留作者表达式并注明假设/成本口径，不把它提升为逐字节实现的精确总复杂度。论文未提供端到端 ANN 召回下界、最坏图导航保证、threshold join 完整性或属性过滤保证。

来源：v3 §A.7–A.8，PDF pp.15–16；[附录](https://arxiv.org/html/2602.21247v3#A1.SS7)。

## 7. 实验事实与比较口径

以下均为**论文报告数字，不是本仓库性能复现**。结构化摘录见 [paper-results JSON](../../../researches/vector-join/2026-11-01-exps/data/pipnn-paper-results.json)。

### 7.1 数据和机器

| 数据集 | 条数 | 维度 | 类型 | 度量 |
|---|---:|---:|---|---|
| BigANN | 1B | 128 | uint8 | L2 |
| DEEP | 1B | 96 | float32 | L2 |
| MS-SPACEV | 1B | 100 | int8 | L2 |
| MS-Turing | 1B | 100 | float32 | L2 |
| Wikipedia-Cohere | 35M | 768 | float32 | MIPS |
| OpenAI-ArXiv | 2M | 1536 | float32 | L2 |

v3 Table 1 已是 OpenAI **2M**；v1 Table 1 写 1M 而其 Figure 5 写 2M，本轮以 v3 为准，不将两版拼接。主实验四个 1B；附录 A.6 另含 Text2Image-1B（200D、MIPS），不是只评测了四个十亿数据集。

十亿规模与下游 k-NN 构图：Google Cloud `c4-highmem-288`，288 vCPU、2.2TB DRAM。其他主实验：四路 Xeon Platinum 8160，作者记 192 CPUs、1.5TB DRAM；不将 192 解释为物理核心数。单图/表在同机比较。峰值内存 Table 6 独立注明 128 threads。

基线：ParlayANN Vamana/HCNNG、HNSW，以及 MIRAGE、FastKCNA；附录补 SymphonyQG。通常最大度 64，HCNNG 允许 90。MIRAGE/FastKCNA 在十亿规模超过 2.2TB；FastKCNA 无所用 MIPS 支持。MIRAGE 为大规模索引溢出问题修改成 64-bit 整数，见 A.9；其速度要带此实现条件理解。

### 7.2 加速比不能跨阶段混用

| 项目 | 作者报告 | 来源与限制 |
|---|---|---|
| ANN 索引构建，相对 Vamana | 最高 11.6×，平均 6.32× | §1；匹配查询质量，不是固定所有参数的普适比值 |
| ANN 索引构建，相对 HNSW | 最高 12.9×，平均 10.4× | §1；单机多核与所测数据 |
| 十亿级索引构建 | 低于 20 分钟 | 摘要/§1 的作者结论；不是商品级小内存机器的承诺 |
| 下游 k=10、边 recall≥95%，相对 hnswlib | 2.2–6.9× | §5.2 / Fig.6；包含索引构建与全点查询 |
| 同一下游任务，相对 ParlayANN Vamana | 1.4–1.7× | §5.2 / Fig.6；不沿用 11.6× |
| 分区阶段：单层/多层 fanout 相对 replication | 平均 1.47× / 3.35× | §5.1、A.2 / Fig.9；只计 partition |
| 叶子构建优化：局部 k-NN / All-to-All Prune | 平均 27.03× / 12.71× | A.4 / Fig.12；不是整个 build |
| 最终 prune 对 QPS 的提升 | recall 0.90 时 10.18%；0.95 时 9.45% | A.5，四个消融集平均；占 build 平均 9.82% |

主 recall 为**查询平均 10@10**，必须在相同 recall 上比较 QPS。单 replica PiPNN 的质量与单 pass Vamana 相近；双 replica 与双 pass 接近，不能拿单副本构建时间替双副本质量背书。A.10 的 `100@100` 支持所测更大 k 的相对表现；`k=1000` 时作者称所有方法都未达到足够 recall，不外推“任意 k 都好”。

**原文内部数字不一致：** v3 PDF 摘要仍写下游最高 1.9×，而 §5.2/Fig.6 文字和 §6 写相对 HNSW 最高 6.9×、相对 Vamana 1.4–1.7×。本报告并列记录位置与比较对象，不替作者猜测或消除这个差异。

Table 4 的 perf 计数支持局部性改善，但明确 **关闭 Vamana 预取**。不能将此诊断条件当作所有基线主实验的配置，或当成预取开启时的硬件结论。

### 7.3 v3 的内存补充是重要限制

| 数据集（128 threads，峰值 RSS，GiB） | PiPNN | HNSW | Vamana |
|---|---:|---:|---:|
| BigANN-100M | 214.45 | 105.44 | 51.62 |
| MS-SPACEV-100M | 211.62 | 102.55 | 51.98 |
| OpenAI-2M | 18.01 | 28.66 | 17.17 |
| Wikipedia-35M | 177.32 | 228.88 | 115.12 |

来源：v3 A.10 / Table 6，PDF p.18。作者解释：桶成员与 leaf-building 的按点额外开销，在低维整数集更显著，在高维 float 数据里被向量本体空间部分掩盖。因此有界候选状态不等于整体低内存，尤其不能未经实验推断 PiPNN 适合 DiskJoin 的低内存 SSD 约束。

SymphonyQG 使用 RaBitQ 而其他方法禁用量化；在 OpenAI 的查询吞吐更高，但更慢构建。v3 明确提醒不是 apples-to-apples，不拿它证明 PiPNN 查询最强或量化本身无用。

### 7.4 参数消融边界

- hash bits 测 6/8/10/12/14/16；Fig.13 是 SPACEV-100M **无最终 prune** 的图，8–16 稳定、6 变差，默认 12。不是所有分布的鲁棒性定理。
- reservoir 64/128/192 与无容量上限对照，均使用双向局部 k-NN，再最终剪到 64；Table 5 是 SPACEV-100M recall>0.95 的平均距离比较次数。不能推断所有候选策略都不受容量影响。
- A.4 细读后，VQSort 对 All-to-All Prune 仅约 1.05×；文字称局部 k-NN 相对 priority queue 没有额外改善。27.03× 是组合优化结果，不全归因 SIMD 排序。

## 8. 对本项目的启发：复用机制，不挪用正确性

### 8.1 可直接借鉴的工程原则（B 类）

1. 若 ANN build 成为 end-to-end 的主要部分，优先消除构建期随机图搜索，而不是只减少距离次数。
2. 距离矩阵限制在小工作集，调 fanout/leaf size 时同时计重叠成员和线程临时空间；基准要匹配总 RSS，而非仅匹配最终图最大度。
3. 用有明确合并不变量的有界状态吸收多个候选源；同桶最近代表适合图稀疏化，不适合承载所有有效 join 输出。
4. 导航图质量最终看 QPS–recall 和端到端时间；局部 k-NN edge recall、visited nodes 或 build speed 单项都不充分。

### 8.2 与仓库现有工作的关系

| 对照工作 | 关系 | 不能混同之处 |
|---|---|---|
| [Unleashing Graph Partitioning](../2025-graph-partition-routing/paper.report.md) | PiPNN A.1.3 明确引用其 Sorting-LSH 分区方案（paper_explicit） | 该文主打分布式 ANN 分片/路由；PiPNN 在构建期用小叶子产边，不是 kRt/hRt 的同一用途 |
| [DiskJoin](../../vector-join/2508.18494-diskjoin/paper.md) | 本项目综合比较重叠分桶/局部性和 build 成本（researcher_synthesis） | DiskJoin 是 SSD 上阈值 self-join；PiPNN 是内存图 ANN build；本轮未认定两者有作者引用关系 |

### 8.3 不能直接替换阈值 join 的反例

源点 p 的两个合格近邻 c1、c2 距离都小于阈值 τ，且 `h_p(c1)=h_p(c2)`、c1 更近。HashPrune 删除 c2 对导航可能可接受，但 threshold join 要输出 `(p,c2)`，这就是漏报。即便不碰撞，容量 L 小于 p 的合法度数也必删结果。候选遗漏不能靠末尾准确距离验证补回。因此“HashPrune 历史无关”不推出“join 结果完整”。

固定 k 且每点都恰有 k 个非 self 真近邻时，查询平均 top-k recall 与有向 k-NN 边 global recall 在等权计数下可相等；阈值 join 各点结果数变化，两者一般不等。对 cross-join、无向去重、zero-result queries 和边界并列，还需单独定义。

## 9. 后续：作者方向与研究者假设分开

作者 §6 明确 future work：量化 GEMM、GPU/TPU、无需最终 prune 的更好 HashPrune，以及数百亿点分布式构建。A.10 又指向即将出现的量化后续工作。

作者主页已列 **Turbocharging PiPNN for Proximity and k-NN Graph Building**（VecDB@VLDB 2026），[OpenReview](https://openreview.net/forum?id=8GzbSQMrLA)。本轮 forum/PDF 都被浏览器验证页阻挡；仅核作者主页题名/列表，**不与 PiPNN 合并、不借其题名推断算法/结果**，留待补全文。

本项目研究假设（不是作者结论，新颖性尚未充分排查）：在相同总内存与最终召回下，把 PiPNN 作为 self/cross 逐点 top-k 执行的构建基线，能否降低首次执行总成本，而在多次复用时优势减弱？最小实验和停止条件登记到 [研究问题](../../../researches/vector-join/research-questions.md) 的 PiPNN 小节。本轮只做文献审读和有限规则构造，未运行 ANN、十亿规模或 join 性能实验。

## 10. 证据入口与未核事项

- 正式书目：[Crossref](https://api.crossref.org/works/10.1145/3770855.3817891)、[本地快照](crossref-2026-11-01.json)。
- v3 原文：[HTML](https://arxiv.org/html/2602.21247v3)、[PDF](https://arxiv.org/pdf/2602.21247v3)；本地 `paper.md` 的 `PDF page N` 可搜索定位。
- 方法：§3 pp.3–4；§4 pp.5–6；实验：§5 pp.6–9；消融/证明：A.1–A.9 pp.11–16；补充质量/内存：A.10 pp.17–18。
- 来源/代码关系：[作者主页](https://www.cs.umd.edu/~laxman/)、[固定 commit README](https://github.com/ParAlg/PiPNN/blob/443a328dc71a3b7be753d52ca2b3c9a4ba77f3fd/README.md)。
- 待核：正式版与 v3 的差异；官方实现的 bf16/等距处理、并发同步与 self-match 规则；完整构建参数/多次运行方差及复现；Turbocharging 后续全文；外存和属性过滤迁移的适用性。没有新颖性穷尽或复现实测性能主张。
