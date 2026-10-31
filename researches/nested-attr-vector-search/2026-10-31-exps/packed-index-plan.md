# 整目录层级分块共享图：完整实现与冻结验证方案

> **当前执行范围已收窄。** 用户要求先超过基线查询效率，维护仅理论分析；本文件算法定义继续有效，§6完整实验矩阵暂不执行，以[查询优先协议](packed-query-first-plan.md)为准。

## 0. 决策和定位

**本轮研究“减少需要启动的独立图搜索”，不再给旧H添加邻接规则。** 实现已落在 `src/packed_index.h` 与 `dynamic_triad --arm P`。不改变基础HNSW构图启发式、邻边或停止规则；不使用proxy近似选块。主基线仍是忠实的纯NodeANN，不人为降低其参数、换慢距离核或增加无用工作。

研究假设：目录原子粒度小于适合ANN的物理粒度时，将DFS相邻的**整个direct-owner组**合并为共享图，可以摊薄多图入口/队列/导航和固定容器开销；子树的层状结构约束合并产生的scope外污染。逻辑owner与物理place分离，让目录更新不强制迁移向量。这个假设可以失败，尤其在大目录、稀疏Extra和随机迁移条件下。

这是一项完整可执行候选，不是已证明的新论文贡献。旧owner/place解耦、Trie、bitmap、分区ANN、局部top-k合并都不是新发现。若收益只能来自少建hnswlib固定锁表，或扫描小E，它最多支持系统组织/执行策略实测。不能预先声称CCF-A门槛通过。

## 1. 问题和不变量

- 数据：不可变FP32向量，唯一外部ID；每个活对象恰有一个当前owner目录。内部节点可以直接拥有对象。
- 查询：`Live ∧ Scope(anchor, recursive/direct) ∧ Extra`。Extra支持category、int64闭区间、tag模式及启用字段间AND/OR。距离为公共RawL2的原始squared-L2，结果以距离/ID排序。
- V修改标量属性，T修改tag，M单对象改owner，B批量移动子树内对象，R重挂目录，D删除，I插入预留ID或复活。V不是向量值更新；本轮不支持改向量payload。
- 所有活对象恰位于一个物理块；每个非空块一张普通HNSW。资格集合必须等于独立canonical求值；物理位置不代替当前目录资格。
- 无proxy遗漏：凡含合格对象的块，都搜索一次。这里保证的是块覆盖与资格正确，不是ANN召回。

## 2. 索引组织：目录层级真正参与什么

### 2.1 初始布局和完整repack

1. 目录按真实树DFS前序排列，子节点按ID递增；目录自己的direct对象排在其孩子之前；组内对象ID递增。
2. **direct-owner组不可拆分。** 小于B的连续组用next-fit贪心装入块；放不下就关闭当前块。
3. 单组大小≥B时，关闭前块，为该目录单建一张图，不把大目录切成许多图。B为软目标，不是向量数硬上限。
4. `target=max(B, 初始块活对象数)`，图容量为`2*target`。B4096为主，B1024仅诊断敏感性。
5. 保存`loc[id]`和目录home。每个对象只属于一张物理图，但canonical原始向量和hnswlib内部payload各有存储，**不是整个进程仅一份向量**。

三个布局采用相同距离核、资格计算和搜索器：

| 布局 | 初始组织 | 作用 |
|---|---|---|
| node | 一direct-owner组一图；target=max(1,组大小)，capacity=max(16,2target) | 隔离资格前置/ef封顶；不是主基线NodeANN，M后也不搬物理位置 |
| shuffle | seed100打乱整个目录组，再按同样规则合并 | 排除“只要少建图就够了”；组数/装箱数可能不同，必须披露 |
| dfs | 按真实目录DFS顺序合并完整组 | 检验层级局部性是否有独立价值 |

shuffle不是打乱对象；random/coordinate所有权数据控制又是另一维，不得混淆。

### 2.2 精确资格索引

复用已经验证的 `eligibility_index.h`：目录children、direct-owner稀疏word postings、live bitmap、category/numeric/tag bitplanes。只存direct成员，不为所有祖先复制完整对象列表。

查询先构造精确Scope和Extra，再得到eligible mask及E。这是在线有成本的索引操作，不是免费GT。其实现仍会扫描全局bitmap words；宽范围大量eligible对象还要枚举E个ID来分配到物理块。因此本方案不是“完全不访问eligible ID”的新证书。

## 3. 查询算法

1. 在线构造完整eligible mask，计入e2e。
2. E=0直接空返回，距离和图调用为0。
3. **主候选P-dfs/cap**：枚举eligible ID，经loc统计各块`E_b`；只跳过`E_b=0`块。
4. 每个选中块令`k_b=min(k,E_b)`、`ef_b=min(E_b,max(efSearch,k_b))`。运行普通过滤HNSW，允许不合格节点作导航；过滤只控制结果资格。上层下降和底层访问均计数。
5. 各块最多返回k_b个结果，统一距离/ID小顶结果合并。无需再次物化所有向量或扫描全部scope。
6. **辅助P-dfs-aware/hybrid**：全局E≤max(k,efSearch,512)时直接扫描eligible向量；其他查询同上。该收益单独归因，不算分块机制贡献。
7. Exact控制走相同资格索引的精确扫描。当前runner依然构建/维护pack图，因此它只提供真实查询效率参照，不提供独立无图结构的build/RSS结论。

固定k10、efSearch15/20、M16、efConstruction200、构图seed100。C++ `--ef` 是搜索预算，15对应k+5；不是把M叫ef。所有臂使用同一公共原始FP32距离核。P为启用HNSW metrics复用固定版本的上层下降和 `searchBaseLayerST<false,true>`；没有新图算法。

## 4. 可证明的内容与不能证明的内容

### 初始/完整repack后的块数界

对任意查询，每个有合格对象的direct-owner组只有一个物理位置。因此其选中块数不超过有合格对象的目录组数，也不超过纯NodeANN遍历的非空scope目录数。驱动逐query核对后一性质。这不是距离访问数或延迟上界。

对recursive子树，全部direct-owner组在DFS组序中是连续段。相交pack中，最多首尾两个pack含scope外组。内部相邻pack的合计大小大于B：否则贪心不会关闭前块；大组自身≥B。故相交pack数为`O(N_scope/B+1)`，且由完整小组/大组结构约束。此处N_scope是活对象数；不能将它替换为Extra过滤后的E。

- Extra可以使每个块仅剩一个合格对象；层级连续性不保证属性稠密或少距离。
- direct查询没有“整棵子树内部块”的解释；它的完整owner组初始只在一块，块中可能混有其他目录。
- M/R后物理位置冻结，上述连续性界不再适用；真实资格仍精确。不要用初始定理为长期动态数据背书。
- 全局精确top-k来自各分区精确local top-k的并集，是经典合并事实；HNSW local top-k是近似的，不能据此推出Recall保证。

### 成本模型

查询为 `资格构造 + O(N/64 + E + 已分配块槽数) + Σ选中块HNSW搜索 + 结果归并`。比NodeANN少启动图并不保证Σ搜索工作更小。初始布局为O(N+D)元数据工作加实际HNSW构图；不声称HNSW具备分布无关对数搜索界。

## 5. 完整生命周期

| 事件 | 已实现动作 | 必须计入的代价 |
|---|---|---|
| V/T | 更新canonical、资格bitplanes及pack元数据；不动图 | 属性更新、写入；图距离应为0 |
| M | 更新当前owner posting；loc保持 | posting维护及元数据；图距离应为0 |
| B | 从资格索引取得原子树对象集合，逐对象M | scope取得、枚举、posting重写；不是O(1) |
| R | 更新三份一致的目录元数据；重算pack目录rank用于以后split | affected统计用精确scope索引，树更新/rank重建；不扫描向量、不做图距离，但并非O(1) |
| D | markDelete，移除live成员；墓碑≥1/3时同步重建该块；空图释放 | 删除找成员O(block_live)、实际局部重建和插入；无后台免费债务 |
| I | 复活优先旧loc；首次I用目录home；空索引可插 | 首插/复活、必要的槽压实、图距离 |
| split | 插入后将超过2target时，先对prospective成员按当前DFS owner/ID排序并平分，重建两图 | 两图完整重建、位置更新；不先写爆图容量 |
| full repack | 按当前目录重新布局所有活对象，重建图，重置dead loc和home，回收旧块槽 | 全部时间、距离、插入、峰值RSS；是明确停顿，不是后台自动完成 |

split优先复用空块槽，避免空图容器不断增长。完整repack可重新编号物理块；只要求切换前后canonical查询语义一致。局部split可能拆开已增长的owner组，故静态“不拆owner”定理不承诺跨所有更新成立。

单线程实现，无并发查询/更新、快照隔离或在线无停顿repack主张。现有ID空间来自输入base向量，未实现任意新增向量流。

## 6. 最少有判别力的实验

### E0：本地行为门

真实C++ CLI验证所有事件、空索引首I、非连续live ID、同label复活、2target边界、墓碑压实、空块复用、全repack回收、内部节点owner、资格边界和GT对照。不用toy速度证明性能。纯A基线对既有同轨迹结果/计数逐条比较，不修改其执行路径。

### E1：原WIKI/SIFT诊断

复用原始 `static-7654f1e-run1` manifest所指定的真实输入和前200Q；沿用历史qidx分组。两数据集各57次（9臂×2ef+Exact）×3串行轮换，共114次。完整记录失败点。

主要比较：P-dfs vs A-graph；P-dfs vs P-node；P-dfs vs P-shuffle；P-dfs-aware vs P-dfs。诊断矩阵同时提供node-aware、shuffle-aware和B1024的cap/aware，防止把扫描收益或单个块大小伪装为层级贡献。

预测：旧WIKI碎小目录可显著减少graph_calls，但同图中过滤稀疏可能抵消收益；旧SIFT目录少，改善未必明显。此轮只诊断，不要求小N胜出后才能检查规模假设。

### E2：SIFT规模与应用背景

`prepare_packed_sift.py`读取真实128D fvecs，不归一化、不产生假SIFT。生成**明确合成**的层级及属性：fanout≤8，64叶粗目录与2048叶细目录；1/8对象直接属于内部节点。random所有权和固定前8坐标线性投影的coordinate所有权保留完全相同目录计数；不使用query/GT建立布局。不能称真实应用目录。

先N=100000的粗/细×random/coordinate四个单元，再按下面门槛决定N=1000000。每单元256个不同源query向量，默认取原query文件offset1024；偶/奇qidx分组，两组都有各谓词族。不声称与历史全部实验无重合。

查询包含固定选择率族、宽窄scope、direct/recursive、单/多属性AND/OR；另有全局**期望E=128/512**的numeric窗口，真实E实测，不保证刚好等于目标。固定选择率与固定期望E分别报告。

规模矩阵每单元33次：A-graph、P-node、P-shuffle、P-dfs、P-dfs-aware各ef15/20，加Exact，三次串行轮换。粗目录是预期不利控制：在1M、64叶时，多数组≥B，各自独立，不能仅靠放大N就宣称NodeANN必慢。保留该控制，不只选择碎目录。

**规模推进门**：100k四单元都完成后，至少一个细目录单元在预先固定ef20的确认非空查询中，A/P双方macro和pair均≥.99，P-dfs或P-dfs-aware平均耗时比≤.80，且三次各≤1，才运行1M四单元；另报告.95层，不得拿其替代.99的推进门。要求确认非空独立qidx≥20；覆盖不足不是性能失败。没有通过就停止1M，保留100k全部结果。不得增大ef或偷换B追正。

若只有aware过门，1M仍有执行策略规模研究价值，但不代表pack机制已过门。内存不设必须更优门槛；只要求在机器实际预算内运行并披露峰值。

### E3：有限维护（不是旧H Dynamic）

对已完成静态的100k四单元运行maintenance；若1M获准，再对1M四单元运行。每单元30次：五臂ef20×3重复×mixed/envelope两轨迹。

- mixed使用相同输入前256Q及之间真实事件；生成器含持续V/T/M、D/I，以及在第64/128Q后各1%-N对象的固定迁移burst，后续Q看见新owner。不是只更新后立刻还原。
- envelope复用既有明确合成包络：R往返、约10%子树B及逐M恢复、删除初始活对象前缀ceil(N/3)并触发实际压实、一次I。对象迁移/删除数量及真实调用全部记录；它不是自然更新频率或稳态分布。
- 每个P运行结束**必须full repack**，费用记入总工作负载。A已经在事件中迁移/压实，不强加一个没必要的全重建来放慢它。

这套维护探针不证明长期自动repack策略已最优；它保证本批次没有把恢复初始局部性的完整成本藏到实验外。大N包络会产生较多更新和raw CSV，先查内存/磁盘，不缩水或只记便宜M。

## 7. 固定评价和失败分类

主要效率指标是**共同质量目标下e2e均值、p95/p99**，不是距离调用或内存。主质量层.99，.95仅次要观察；实际macro/pair同时给出，空GT单列。主预算ef20，ef15敏感性；两点全部公开，不在确认集挑一个更好点覆盖另一个。驱动比较文件标注common quality targets，不把同预算自动叫同质量。

1. 语义错误/容量错误/GT泄漏：实现失败，修复后另起目录；不能当机制负结果。
2. 质量未达共同层：近似质量不足；时延仅诊断，不叫同质量加速。
3. 覆盖不足：证据范围不足，保留样本内数字。
4. 合并少图但查询未加速：分块假设负结果，追查资格、距离、图内访问与合并，不改图追正。
5. dfs不优于shuffle：没有层级特有收益；即便胜A，也先降级为通用图容器合并/资格策略。
6. aware赢而cap不赢：扫描/执行策略收益，不算层级新索引证据。
7. 查询赢但包括更新/恢复后的总工作量不赢：披露查询—维护工作负载边界；不要求所有Q/U都赢。

所有query需报scope、E、图调用、距离、hops、资格时间、search/e2e和非法/重复/mask误差。各运行报构建、初始化图数、payload/metadata估计、峰值RSS、各事件成本、split/compaction、final repack。`index_bytes`不是精确RSS；serialized graph bytes漏掉锁表等驻留结构。固定hnswlib每图65536标签锁，减少该开销是真实结果，但不是独立算法创新。

完整比较：`T = build + queries + updates + final_repack`。summary的`total_workload_us`不含build，汇总时加`1e6*build_s`。若查询有净收益，给出覆盖一次完整维护差额所需的查询数，而不是任意选择Q/U：`Q_break_even=max(0, Δbuild+Δupdates+Δrepack)/(A_mean-P_mean)`；分母≤0时不定义查询摊销胜点。

## 8. 直接前序与论文门槛

- [TFANNS/ICDE2025](https://doi.org/10.1109/ICDE65448.2025.00272) §V，本地paper.md705–725：Packing使块内向量共享标签degree资源、频繁tag用mask。当前是整个目录组共用普通HNSW，不修改degree分配；“packing”这个名称及共享资源思想本身不新。
- [Window Filters/ICML2024](https://jshun.csail.mit.edu/filteredANN.pdf) §4.2–4.3、§5.2：按有序标签建子集ANN、查询覆盖、blowup/空间权衡已有系统研究。当前仅一层不相交图、保留完整owner和动态逻辑树，但不能把“DFS区间+分区ANN”作为首次贡献。仅核相关正文，不冒称完整新颖性审查。
- [iRangeGraph](https://doi.org/10.1145/3698814)、[Curator](https://doi.org/10.1145/3786635)：范围/子集索引共享与资格求值均须保留为前序；本轮未重做作者实现。
- 项目2026-10-29 `experiment-server-prompt.md`早已提出owner/place/routes解耦和维护债务；本轮复用，不能重新命名为新机制。
- TrieHI的问题定义与层级元数据组织需要引用，不能因为预印本尚未正式发表就忽略。当前没有为每个路径前缀建立物理ANN副本，层级作用是完整owner组的物理装箱；是否形成足够贡献仍要由机制对照和更充分前序排查决定。

**支持论文候选的结果组合**：共同质量下稳定胜纯A，dfs相对同算法node/shuffle存在可解释且可复现增量；粗细目录/相关性/迁移控制符合机制预测；包含完整repack仍能给出有用工作负载区间；并能把上述机制与直接前序区分。单次胜A或少几个锁表不足以声称已达论文门槛。完全未跑的真实SIFT1M结论继续标未测，不拿本地synthetic烟测替代。
