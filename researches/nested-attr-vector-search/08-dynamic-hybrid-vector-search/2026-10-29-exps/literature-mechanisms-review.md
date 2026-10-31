# 独立文献机制审计（2026-10-29）

## 范围与证据等级

先读 `papers/papers.jsonl`、`papers/relations.jsonl`、本地全文/笔记及本目录的 `stage1-literature.md`、`stage2-candidates.md`。本轮直接阅读原文相关章节：ACORN v1、Curator v3、iRangeGraph v1、Filtered-DiskANN 作者公开稿、Quake OSDI 正式稿、VADER v2、EMA v1；额外读 ELI 正式稿 §3/4，结合 TrieHI、PathSeer 相关章节的交叉核查见 [独立判断](independent-review.md)。这不是全面查重；未重新阅读全文或运行作者实现。SeRF、UNG、FreshDiskANN 本轮未独立核实其方法全文，不能把它们列为“本轮已读全文”的证据。

书目元数据：本轮直接读 Crossref `https://api.crossref.org/works/<DOI>`，核对 `message.title,author,volume,issue,published-print,published-online,type`：ACORN DOI 10.1145/3654923，PACMMOD 2(3), 2024；Curator DOI 10.1145/3786635，PACMMOD 4(1), print 2026-04-02 / online 2026-04-07；iRangeGraph DOI 10.1145/3698814，PACMMOD 2(6), 2024；Filtered-DiskANN DOI 10.1145/3543507.3583552，WWW 2023, 3406–3416。前三篇读的是本地预印本文本，不能声称已逐项比对正式版。Quake 正式 PDF 首页明确 OSDI 2025。VADER arXiv 页面核标题/作者/2026-09-29，正式 VLDB 卷期未核；EMA HTML 首页仍有 PVLDB 14(1):XXX-XXX,2020 和 DOI 占位符，只能标 2026 预印本，不认作正式 VLDB。

## 已读论文：真正机制、不变量及约束

### 1. ACORN：不是“把 HNSW 加过滤回调”

原文：[v1 §5.1–5.3 pp.4–6](https://arxiv.org/pdf/2403.04871v1)，本地 `papers/filtered-ann/2403.04871-acorn/paper.md:414-444,499-545,558-644`。

- 目标为在谓词诱导子图内近似模拟专门建在合格集合上的 oracle HNSW。ACORN-γ 建图时扩邻到 Mγ，再压缩；压缩保留近邻并保证被该规则删掉的候选可以经指定二跳边恢复，查询扩邻后精确过滤。ACORN-1 把扩邻放在查询期；v1 明确其构建为无普通 pruning 的 HNSW 特例，不能把任何 hnswlib+callback 命名为作者 ACORN-1。
- 关键不变量不是全图连通，而是**过滤后仍可恢复相应邻域候选**。普通 RNG 的中间点一旦被谓词移除，三角形替代路径就失效（596–617）。即使如此，其理论搜索复杂度分析假设 exact Delaunay graphs（641–644），不是实现上的任意分布 recall 保证。
- γ 与最低选择率关联，原文明确低选择率预过滤 fallback（531–557）；所以 `stage1:34`“唯一索引级策略自适应是 UNIFY”过强：ACORN 本文就有基于选择率的 index/pre-filter 切换。
- 对共享图：必须报告过滤诱导可达性/候选恢复以及实际 distance/filter/edge-expansion 代价；“一个图”“图连通”“MST 骨架”不构成相对 ACORN 的新机制。对 C1/C2：应与真实 filter-aware traversal 同质量比较，不能只赢全局 HNSW post-filter。对 C3：谓词无关几何图是属性-only更新的天然强对照，但论文未给本项目层级更新协议或其成本界。

### 2. Curator：共享结构＋密度自适应 buffer＋可回收摘要均已有

原文：[v3 §3–5 pp.4–8](https://arxiv.org/pdf/2601.01291v3)，本地 `papers/filtered-ann/2601.01291-curator/paper.md:409-493,599-640,641-699,819-969`。

- 几何 hierarchical k-means base tree 存一份向量；各 label 复用分区但以 buffer 容量决定停止深度，稀疏 label 在较高节点聚合。这不是每 label 复制完整 ANN。
- Bloom 的不变量是无假阴性；假阳性只多走树，external node 无 buffer 不会产生结果。label insertion/deletion 触发 buffer 下沉/合并；删除后**根据子节点及本地 buffer 递归重算 Bloom**，直到无变化（918–934）。所以“EMA OR 不可回收→本项目首次可回收摘要”不能成立。
- §4.2 明确 AND/OR/NOT 任意复杂谓词→外部执行器提供 sorted qualified IDs→按连续路径编码二分构造临时 virtual-label tree。§4.2 后半明确缓存热点临时索引或永久集成并复用更新例程。因此 `stage1:36`“复杂布尔索引级仅 ACORN”若无狭义定义就是错误；它在输出合格ID后直接构造索引内加速结构，不只是 callback。
- §5 局部重建以 U_n/|P_n| 阈值触发，并原子替换；这不等价于全局快照/MVCC证明。几何树不是语义 taxonomy，但语义层级不是排除其基线资格的理由。
- 查询使用 beam+best-first，score 为质心距离减加权平均半径；buffer 不再改进结果集时停（599–640），是启发式，不是 admissible lower bound。低维/高维质心失败并未自动否定这整个方法空间。
- 对 C1：层级 coarse-to-fine 搜索、稀疏区域升层聚合已有；新贡献需指出语义树不能自由重分区时如何仍获得可校准路由。对 C3：需同时打败“OR 单调＋周期重算”和 Curator 式局部重算，不能只选永不回收的弱基线。对 C7：热点缓存/预物化直接重合，单独做不够。

### 3. iRangeGraph：共享范围图与按需组图已有，且有明确非保证边界

原文：[v1 §3.1–3.4 pp.4–7，§5.2.3 p.11](https://arxiv.org/pdf/2409.02571v1)，本地 `papers/filtered-ann/2409.02571-irangegraph/paper.md:441-498,503-595,615-670,740-780,1267-1286`。

- segment-tree 各区间物化 approximate RNG elemental graph，总空间 O(n m log n)。查询只在访问某点时从各层选择其边，跳过与查询交集相同的层，构边摊销 O(m+log n)，而非每范围完整索引或分别搜后拼。
- 关键关系：某边在超集 RNG 中不被剪，则在子集也不被该剪枝规则剪；但有限候选/元素图可能漏掉目标范围原本应保留的边。原文 §3.4 明确按需图不必等于从头构建的专用 RNG（756–770），故不能偷换成 recall 定理。
- 原文 BasicSearch 基线恰是把区间分解为 O(log n)不交段、分别 ANN、最后 merge（1267–1276）。故“层级分片＋合并”本身也不是新贡献。
- [INFERENCE] 单绑定静态 taxonomy 经 DFS 将 subtree 转连续区间，是必须纳入的强归约对照；动态 MOVE 的 DFS/辅助结构维护成本和多绑定 overlap 非连续性才可能产生额外约束。不得把静态 laminar 特例扩张为任意重叠标签。共享图新机制至少须超出 segment decomposition/edge reuse，并交代 degree、space、更新后作用域可导航性的 trade-off。

### 4. Filtered-DiskANN：标签安全剪枝与共享图拼接是已知基线

原文：[作者稿 §3 pp.3–5、§5.4.4 p.7](https://harsha-simhadri.org/pubs/Filtered-DiskANN23.pdf)，本地 `papers/filtered-ann/10.1145-3543507.3583552-filtered-diskann/paper.md:288-297,376-434,850-866`。

- FilteredRobustPrune 只有当共同标签 F_p ∩ F_p' 被替代点 F_p* 覆盖时，才允许再按几何支配删边；每标签入口做负载平衡。StitchedVamana 将每标签 Vamana 图并起来再剪枝。
- 单标签为实验主语义，OR 可拆查询合并；AND及复杂表达式明确 future work。不能直接拿普通 conjunction benchmark 的成绩推到祖先闭包/任意集合谓词。
- §5.4.4 只定性论证 FilteredVamana 插入比 stitched 容易，动态删除与详细评价留 future work。`stage1:7`“无更新协议”最好精确写为“无完整动态删除协议及对应实证”，不能读成不支持插入思想。
- 对共享图：per-label图合并、几何＋标签安全剪枝不新；必须解决在资源约束下保存哪些作用域导航边，而不是只加连通边。对 C3：祖先闭包标签改变会改变剪枝/入口义务，单纯改 metadata 不自动维护已有的过滤图质量。
- 额外原文纠错：EMA §2.2 把 Filtered-DiskANN 条件写为端点标签 **并集** 被中间点包含；原始 Algorithm 3 是 **交集**。比较该基线应依原文算法，不沿用后作概述。

### 5. Quake：动态层级＋逐查询分区预算不是空白

原文：[OSDI 2025 正式稿 §3–5 pp.156–160，§7.2–7.6](https://www.usenix.org/system/files/osdi25-mohoney.pdf)。PDF有会议封面，正文页153是PDF第2页；§4是PDF6–8，§5是PDF8–9。

- 多层 IVF式分区；cost C=Σ access_frequency×profiled_scan_latency(size)。split/merge 采取 estimate→verify→commit，只有预测 cost下降过阈值才提交；verify 重新测尺寸但保留频率假设。所谓稳定收敛是固定 workload 下模型代价，不是对任意真实延迟的无条件单调保证。
- APS 在每层对每查询动态选择扫描多少 partition：当前第k距离形成球，按球与近似 Voronoi半空间交体积估邻居概率，按概率扫描并更新半径，估计recall达标即停。其前提含 uniform-density 与半空间近似（§5），不是确定性安全剪枝/严格逐query recall证书。
- 直接削弱 `stage1:43–46`“ANN只有统一ef/nprobe”“向量shard距离分布路由基本空白”“层级路由无先例”。Quake 不是语义 scope routing，也不是已证明任意语义 shards 可用同一模型，但它已覆盖多层、动态、逐query预算和概率停止的核心模块。
- §7 清楚拆 search/update/maintenance，区分 ST 与16线程MT，quantization统一禁用；部分基线固定参数未达recall被星号标出，某些未完成结果为10%查询外推。引用其加速比必须保留这些条件，不可直接当本项目性能预测。
- 对 C1/C4：候选技术缺口应写成“谓词条件下、不可自由重划分的重叠/移动语义分区如何估计剩余近邻质量与预算”，不是“首次不等预算”。对 C7/C3：热点维护和split/merge成本驱动已有；语义MOVE与几何重聚类差异需要落实为实际不变量与端到端成本优势。

### 6. VADER：创新可以在停止/校准层，不必造更复杂索引

原文：[v2 §3.1–3.4 pp.3–6](https://arxiv.org/pdf/2609.38053v2)，本地 `papers/filtered-ann/2609.38053-vader/paper.md:299-340,403-465,559-640`。

- 在现有 Sweeping、ACORN、Filtered-ScaNN上加 filter-aware GBDT recall estimator；以检查/通过/失败数、observed selectivity与中间距离/搜索进展预测recall；达到目标停止。自适应调用间隔继承 DARTH/DARTH+；新意集中在过滤状态特征、训练与更重罚 recall高估的非对称loss，而不是新的物理索引。
- GT用于离线训练样本标注（619–628），不是在线停止读取GT；区别对本项目 oracle路由尤其重要。预测recall不是逐query下界，需报告欠达标率/分位数，且跨predicate、更新、scope漂移都需另测。
- 对 C1/C2/C4：只说“动态分配ef”“根据估计召回停”不新；必须可部署地估计语义条件scope里的未见近邻，不偷用GT，并把训练/推理/校准成本计入。对共享图：它示范不改变图也可能有贡献，所以不能按索引复杂度排新颖性。

### 7. EMA：保守摘要不等于端到端召回；C3仍需比局部重算更强

原文：[v1 §3.1–3.5、§4.2、§5.1](https://arxiv.org/html/2606.00734v1)。

- edge Marker 对几何支配邻域的属性分桶OR聚合；查询先 Marker检查，必要时恢复不匹配边以满足d_min，再做节点原属性精确验证。零假阴性声明只在Marker编码层；受限图度、入口、预算及有限恢复不提供全局ANN recall保证。
- §3.5 属性-only更新不改连接，搜索并对一跳反向边OR新属性；删除lazy，沿查询记录坏边，patch用被删点最近有效邻居替换，删除多时重建。它是“廉价metadata更新＋渐进失效＋局部/全局修复”直接前序。
- §4.2 FPR推导要求predicate satisfaction跨点i.i.d.，Codebook桶概率受界等条件；祖先闭包与相关taxonomy不能直接套用。多个属性聚合还可能由不同对象分别见证，而不是一个对象满足全部谓词，最终精确验证必需。
- [INFERENCE] OR-only更新会留下旧bit，但“任何可回收OR摘要”不新（Curator已有）。本项目要证明的应是受影响范围/维护量界、删除/重挂后的保守性和可见性，并在同精确谓词/同召回/同内存预算下显示相对周期重算、局部重算/patch的收益。
- §5.1 EMA对ACORN/NaviX改为lazy predicate evaluation，并统一部分M/efC；这是作者修改的对照，不可冒充未经修改的作者系统结果。其泛化性能主张应与本项目自己的受控对照分开。

## 最相近的五组强对照（按判别价值，而非混为一个算法排名）

1. **ACORN-1/γ＋Curator low-selectivity补充**：排除“只比弱post-filter或scan赢”的伪贡献；保持metadata/GT、硬件和完整filter预处理成本一致。
2. **iRangeGraph＋其BasicSearch式分解合并**：单绑定静态scope的强归约，隔离共享边与独立子索引究竟差在哪；动态时额外记DFS/ID/范围元数据维护，不能免费。
3. **FilteredVamana/StitchedVamana**：共享图、每标签入口和剪枝安全性对照；先将目标谓词明确编码为受支持的single-label/OR，不偷换AND。动态实验不得假设原文已有完备删除实现。
4. **Quake APS / VADER-style合法停止＋相同底层搜索器**：C1/C2/C4机制对照；不是强迫直接将unfiltered Quake当完整taxonomy系统。与固定预算、仅大小加权、仅质心排序比较，并独立记录路由损失/局部ANN损失/欠达标率。
5. **EMA式OR＋周期/patch修复 vs Curator式局部递归重算**：C3可回收摘要的必要强对照。C7另必须ELI/Curator缓存对照（主代理已有ELI正式原文证据）。

## 新颖性门槛和最小判别实验建议（均为建议，未执行）

- **C1：保留但新颖性改为待证。** 最小离线实验采用训练/验证/测试隔离及更新后时间切分，所有summary必须包含inclusive作用域。固定每query实际candidate/filter集合，用可执行质心/样本/概率停止策略与固定预算及oracle上界比较。只有oracle上界有利不能证明部署收益；只有一个错误hierarchical probe失败不能关闭整个方向。需显示计入路由与维护后仍存在同质量Pareto优势。
- **C2：作为C1可替换机制，不独立认定高新颖性。** 本轮未查清sample-ANN向量路由引用链，不能用“未找到”认证首次。最小先exact-sample打分，再评估sample-ANN引入的第二层漏报；样本规模/刷新成本同预算。仅更换采样大小带来的效果是设计空间实证，不一定论文机制。
- **C3：定义缩窄，不认“层级MOVE首次”。** 主代理已读TrieHI并发现MOVE/MERGE直接先例。候选必须拿出超过TrieHI稳定重挂＋EMA OR＋Curator局部重算的明确维护不变量和紧的受影响量界。最小用完全相同更新流及查询流比较OR-only、周期重算、局部重算/计数，测保守性、假阳性/读放大、更新工作、总字节和端到端成本；只证明计数可减不够。
- **C7：工程强对照，不先关闭其优化价值，也不独立包装论文。** Curator已显式讲热点临时索引缓存/永久化；ELI已给物化空间/效率选择问题。贡献至少需新的层级/重叠/动态限制下可证明更好的选择/维护机制，或明确反常规结果，而非简单LFU+阈值。
- **共享图：先写作用域相关剪枝/候选恢复不变量，再决定实现。** “共享一份向量/图、MST保连通、祖先边复用”都不足。最小小图反例＋枚举可检查：过滤后可达性、有效候选覆盖、degree截断破坏、MOVE前后受影响边；必须区分图连通、贪心可导航、有限预算recall三件事。

最终判断：新问题定义只提供研究动机，不自动提供算法贡献；索引更复杂既非必要也非充分。可发表的门槛应是“可复述的新机制/不变量＋针对最近强前序的差异＋可重复同质量全成本优势或可证边界”，当前文献证据不足以认可任何候选为领域空白，更不足以把历史未实现方案判成实验失败。本轮修订8条已有论文的项目证据，并实际执行保存结果的离线审计；未修改索引算法或执行新的ANN性能实验。
