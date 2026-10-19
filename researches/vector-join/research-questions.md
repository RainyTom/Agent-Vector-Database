# 待验证研究问题

## RQ-001：输出规模与质量不确定性下的分层存储 join 计划选择

状态：**基于文献综合推导的研究假设；新颖性尚未充分排查**。不是作者共同提出的原话，也不声称首次提出成本优化或向量基数估计。

### 问题与现有边界

在固定内存/SSD 预算、给定距离阈值和目标 global pair recall 下，能否用小规模 pilot 估计候选数、距离计算、I/O 与结果物化成本，在可用 join 执行方案中选择更省时的计划，并对质量与估计偏差给出可检测的边界？首个版本限定静态 cosine/L2 cross join，self join 作为独立 workload；无需先实现 CPU/GPU/SSD 全部组合。

前序证据与差异：

- [LSH-SS](https://www.vldb.org/pvldb/vol4/p338-lee.pdf) §4–6 已估实向量 join cardinality；不能将“向量基数估计”本身作为新意。需补测 dense embedding 与 build 摊销。
- [Xling](https://arxiv.org/abs/2402.13397) §IV–VI 已做 query 邻居数预测/过滤；训练质量与 pair recall 不能直接等价。
- [Fast AVJ v2](https://arxiv.org/abs/2603.16360) §6.3–6.4 已讨论 NN-Map selection/composition、维护与 join statistics 的优化器消费者。增量维护或“统计用于优化器”本身不足以构成差异；witness 统计保证受 ANN accuracy 与特定输出 regime 限制。
- [DiskJoin](https://arxiv.org/abs/2508.18494) §5.2/§6 提供模型依赖剪枝、SSD 调度和含 build 的端到端评测。是否能迁移为 cross join，需重新验证；不能当现成可组合算子。
- [早期高维连接](https://research.google/pubs/high-dimensional-similarity-joins-2/) 已有 cost model，仍须精读其正文与后续引用。

候选差异是：**对比多个执行方案时把输出物化、离线摊销及端到端质量不确定性一起校准，实测模型选择损失**。当前证据仅足以提出实验，不足以证明该组合未被研究。

### 可检验假设

H1：与只根据数据规模/单 query ANN latency 选择计划相比，加入 pilot 输出密度与候选膨胀的预测，在稀疏至密集阈值变化时降低相对于实测最优可行计划的 selection regret；pilot 开销计入总时长。

H2：把 ANN witness 的 false-empty、过滤器 false-negative 与候选搜索漏报分开监测，可以更早发现 OOD 下 pair recall 违约。置信区间覆盖率需实测；不能先假设独立并把各模块 recall 相乘。

### 数据、基线与指标

- 数据：SIFT/GIST、GloVe、COCO/LAION 的原始 ID/OOD splits；先在小子集上完整 exact ground truth。分开扫描阈值/输出密度、norm、各向异性和簇倾斜；不将 100D SIFT 与 768D 跨模态结果直接横比。
- 执行基线：本项目已有 SimJoin 重实现、独立 graph range-search loop、分块 exact 全比较；硬件可用时再加入 GPU exact。DiskJoin/NN-Map 要先核代码与版本，仅在完成可信实现后作为 baseline。
- 估计基线：uniform pair sampling、LSH-SS（显式含 build 摊销）、简单 pilot extrapolation；Xling/NN-Map 统计在可验证实现后比较。
- 指标：global pair recall、query-average recall（零结果处理固定）、precision、输出 pairs、估计误差（含零输出的单独统计）、interval coverage、候选/距离计算数、I/O bytes、peak memory、build/update/online/verification/materialization time、pilot overhead、plan-selection regret。

### 最小可行实验

1. 选一个 ID 与一个 OOD 数据集，每侧取 5k–20k 向量，以分块 exact 计算完整真值。固定去重、对角、<= 或 <、归一化与浮点精度；真值误差先核对。
2. 每数据设置多个阈值，覆盖零/稀疏/中等/密集输出；执行 independent range loop、共享图搜索与 exact 三种方案。逐阶段计时与计数，物化到相同格式。
3. 在小 pilot 上拟合候选/输出/时间预测，并用独立阈值或独立 split 检验；避免在测试阈值上调出“最优模型”。
4. 比较静态规则、pilot 模型、oracle（实测最优可行方案）；将建索引、pilot 与结果写出计入总成本，并另列重复 K 次执行的摊销曲线。
5. 加入 norm/簇密度/OOD 压力，检查 false-empty 和召回违约；失败时记录回退策略成本，而非隐藏失败 query。

### 风险与反例

- 稀有结果抽不到、密集结果物化主导：更复杂 estimator 可能没有净收益。
- ANN 给的“最近”距离通常只提供到某候选的距离；大于阈值不证明真最近邻也大于阈值。若把它当空结果证书，会直接漏报。
- 只知每 query 是否有至少一个结果，不足以估总 pair 数；NN-Map 的紧阈值 regime 需明确，不能当全范围 exact cardinality。
- 图中不连通 in-range components、倾斜 bucket、cache contention 或混合硬件传输开销可能让 pilot 外推失效。
- 计划候选很少/性能排序稳定时，任何 pilot 都可能增加总时长；应作为反例报告。
- 现有 array/distributed similarity join optimizer 可能已覆盖部分设计；未精读前不能确定论文差异。

### 新颖性初筛进度与作者 future work

本轮同义词粗筛发现早期 epsilon-tree cost model、array join cost model 与 Fast AVJ NN-Map。尚缺系统性引用追踪、filtered join、external-memory cross join 和 quality-aware optimizer 同义检索。下一轮先排查这些近邻工作，再决定是否立项。

作者明确 future work：Fast AVJ v2 §7 列 disk/distributed、带 filters 的 vector-relational、multitable、non-graph indexes、streaming；这些是该论文的展望，不代表领域无人研究。RQ-001 的联合校准、regret 与最小实验是本项目综合推导。

## 邻近检索方向 层级属性约束向量 top-k

此方向为 B 类可迁移借鉴，当前不属于 vector join。研究假设为本项目综合推导，新颖性尚未充分排查。问题定义、已有关联与创新边界见 [创新性分析](../nested-attr-vector-search/novelty-analysis.md)。其中 H1 为分类树下的过滤表示与搜索结构共享，H2 为多标签 distinct-object 搜索预算，H3 为跨层级执行选择；每项的问题、差异、假设、强基线/指标、最小实验与风险均在该文件展开。优先验证 H1/H2，不能只凭层级语义或共享索引宣称首次研究。

### 层级方向后续细化 结构化 overlap

见 [细化笔记](../nested-attr-vector-search/overlap-refinement.md) 的 H4/H5：紧凑子树并集与固定总内存下的索引共享；按不同对象组织跨分支搜索。已明确一般 overlap 不是新谓词，强基线包括 ELI/Curator 的 flat 与 ancestor-expanded 归约、LSSG 和范围适配。每项含不足、区别、可检验假设、数据/指标、最小实验与反例，新颖性仍尚未充分排查；ELI 的作者 overlap 优化 future work 与本项目假设分别记录。

### 2026-10-19 当前过滤语义与候选问题

用户明确当前过滤为 Lq⊆Li，前述 overlap H4/H5 保留为历史方案，不能自动当作本轮目标。新的[包含过滤文档](../containment-filtered-vector-search/index.md)给出 C1 层级蕴含/共享与 C2 相关属性下覆盖索引选择，包括动机、现有先前工作、差异、假设、数据/指标/最小实验及反例。两者均为本项目综合推导，新颖性尚未充分排查；按 ELI/Packing/VADER 等已有能力收缩创新主张。

### 2026-10-19 前缀包含的当前定位

用户进一步明确目标是 p 为对象属性序列 s 的前缀，而非一般 Lq⊆Li。当前[前缀包含文档](../containment-filtered-vector-search/index.md)已给出有序定义、单路径层状性、多路径扩展与精确归约。此前 C1/C2 是集合建模下的候选，不能替代此次语义。原生前缀机制的假设为本项目推导，新颖性尚未充分排查：固定内存与最终Recall@k时，利用前缀共享能否优于位置标签containment、完整前缀singleton及单路径DFS-range归约？最小实验固定向量/距离/k，扫描路径深度、分支、查询前缀长度和命中比例，所有基线计编码/构建/临时成本，测标签/索引字节、端到端/P95与最终召回。风险是单节点归约已足够或浅树无收益；多路径需另测去重，不混用单路径结构保证。新发现2606.16903的TrieHI/路径展开原始摘要为直接约束，需精读后再决定差异，不能把Trie本身作为新意。

### F2 前缀嵌套复用的边预算

本项目综合推导，新颖性尚未充分排查，非Packing作者future work。问题为：单路径前缀链中不同层次的有效邻居需求共享物理边，能否在相同字节/最终Recall@k下改进预算？[第二篇讨论](../containment-filtered-vector-search/walkthroughs/02-packing.md)给出d_a(l)=#{b:LCP深度≥l}的累计需求表示、与Packing既有块共享/mask的区别、5K–20K最小实验/基线/指标及宽前缀导航受损等反例。需先核原实现是否已共享/去重，不以一般预算共享作为新意；尚无算法实现或性能结果。

### F3 前缀终止组导航与入口前沿

本项目综合推导，新颖性尚未充分排查，非UNG原文结论。见[第三篇](../containment-filtered-vector-search/walkthroughs/03-ung.md)：在单序列完整前缀编码下，原标签组包含DAG退化为终止路径森林；无对象的内部Trie节点不能直接作为向量入口。候选问题为Trie专用路由能否省标签层开销、按LCP启用的跨分支桥接能否改善宽前缀多入口搜索。两步分开消融，文中给假设、数据/强基线/指标、最小实验与内部终止组前沿很小等反例。尚未固定完整桥接算法或运行ANN实验，不能声称少量边保证连通/召回。

### F4 前缀祖先索引的物化选择

本项目综合推导，新颖性尚未充分排查，非ELI作者算法。见[第四篇](../containment-filtered-vector-search/walkthroughs/04-eli.md)：完整前缀链编码保留原ELI的祖先索引资格，singleton查询则需另加蕴含路由。在固定单祖先服务、可加独立索引成本、固定factor门槛/候选集下给出以最近物化祖先为状态的树DP；仅为受限代理模型精确递推，不移用通用NP-complete证明，也不扩展为实际ANN最优。包含F4a代理成本与F4b端到端收益假设、同候选greedy/穷举基线、数据/指标/最小实验和相关性/非加性等反例。后续需排查树覆盖/物化视图选择与原生前缀索引前序。

### F5 前缀成员表示与共享几何视图

本项目综合推导，新颖性尚未充分排查，非Curator作者新算法。见[第五篇](../containment-filtered-vector-search/walkthroughs/05-curator.md)：Curator已具有共享几何树、标签自适应buffer、临时/缓存视图与延迟求值思路。前缀可直接展开为单标签基线；属性DFS顺序与几何ID顺序一般不兼容，四对象交叉分区给出反例。候选问题限定跨前缀共享成员表示，以两顺序范围交集支持局部计数/报告，尝试减少全量合格ID准备，而不重新发明延迟构建。文中给出单路径计数加和、10K–50K静态最小实验、原生/临时/延迟/扫描强基线、全空间和端到端指标、范围报告/相关性/低合格数等反例。尚未实现ANN或验证加速，需补原代码与Directory-Aware/TrieHI及范围索引前序。

### F6 前缀扩散的终止路径计数表示

本项目综合推导，新颖性尚未充分排查，非RWalks作者结果。见[第六篇](../containment-filtered-vector-search/walkthroughs/06-rwalks.md)：固定图与随机游走，完整前缀单标签的原始扩散等于采样多重集在前缀子树中的质量，可用DFS终止秩、访问计数和局部累计数组恢复。原始祖先质量单调，标准化后不保证；深前缀零采样不是安全剪枝证据。F6a隔离表示等价与全部统计成本，对照原稀疏map；F6b祖先软导航/稀疏退让另做行为改动消融，策略尚未定型。文中含统一hnswlib的静态最小实验、全字节/评分/距离/端到端/recall指标与浅树、稀有目标、统计开销等反例。680多重集、10,200次分数等式核对通过，未运行ANN或性能验证；待补代码、标准化细节与层级扩散前序。

### F7 LSSG 前缀核验与合法深度分层

本项目综合推导，新颖性尚未充分排查，非LSSG作者结论。见[第七篇](../containment-filtered-vector-search/walkthroughs/07-lssg.md)：完整非空前缀集合Jaccard=h/(a+b-h)，相似门槛可精确化为候选终止深度对应的共同前缀门槛。F7a固定候选进行LCP等价核验；候选路由另消融。F7b限变长路径下的合法深度边层级/调度；等长情形原Jaccard已是LCP门槛，不能算新分层。与F1/F2/F3共用合法深度结构，非独立新颖性主张。文中含同字节/构建预算强基线、静态最小实验、scope/扫描/距离/端到端/recall指标与浅层多样性、入口、连通等反例。900路径对、5400门槛条件核对通过，无ANN性能结果；需补层级图/TrieHI前序与完整构图算法。

### F8 前缀谓词节点与向量入口预算（证据受限）

本项目综合推导，新颖性尚未充分排查，非UniFilter作者结论。见[第八篇](../containment-filtered-vector-search/walkthroughs/08-unifilter.md)：原文仍403，作者综述仅支持谓词AND/OR图与原向量图/cross-edges的框架。若所有语义前缀节点直接代表子树，则非空查询范围可由单节点精确表达，但不证明原UniFilter覆盖复杂度改进，更不代表单向量入口足够。F8限定同字节预算的前缀入口复用/几何多样性，附10K–50K统一hnswlib组件实验、路由与入口分开消融、完整空间/端到端/recall指标及长尾/单簇/既有共享等反例。未取得原cross-edge算法前不能命名UniFilter复现或改进版，须补原文与Directory-Aware/TrieHI等前序。

### F9 直接前序核查后：前缀约束参与ANN导航

2026-10-19取得Directory-Aware v1原文，见[第九篇](../containment-filtered-vector-search/walkthroughs/09-directory-aware.md)。固定命名空间单路径recursive DSQ与当前前缀scope一致，TrieHI已具原生Trie、祖先inclusive bitmap和MOVE/MERGE，因此F1–F8中泛化Trie/前缀/动态维护叙述不能作为新意。主问题定义不变，候选增量收敛至相同scope和总内存下的ANN候选/边/入口导航；F9为本项目综合推导、新颖性未充分排查，非作者future work。原作者future work为更丰富path predicates和cost-aware planning。文中给统一hnswlib的静态适配实验、对照TrieHI+相同ANN、PE-Offline与DFS-range、分解scope/导航成本与完整字节/recall指标，以及宽前缀、扫描、元数据抵消等反例；没有新算法或性能结论。需核原代码接口、后端与路径图前序后选一个组件实现。

### F10 PathSeer约束下的前缀判定与局部预算

见[第十篇](../containment-filtered-vector-search/walkthroughs/10-pathseer.md)。PathSeer已有双区融合索引、局部DFN/DOS混合和离线VOB调度，不能把一般局部自适应当作新意。F10为本项目综合推导、新颖性未充分排查：利用合法源下的LCP证书及准确前缀membership降低判定代价，再独立重profile控制预算。DFN可经不合格源，必须保留直接验证目的的分支；AX→ABC、queryAB说明无条件LCP测试会拒绝合法目的。原生过滤改变c_filter，旧VOB不能直接复用作公平对照。文中给静态最小实验、双阶段消融、总字节/profile/scope/导航/recall口径和bitmap低成本等反例；未运行ANN。hnswlib移植只属组件适配，原论文复现需Faiss双区与完整profile。
