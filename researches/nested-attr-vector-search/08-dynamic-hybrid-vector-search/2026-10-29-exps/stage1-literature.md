# 阶段 1：文献核查与空白判定

日期：2026-10-29。三个并行检索线（动态/可更新 ANN、多属性混合过滤、联邦/分片搜索），每篇核实标题/作者/年份/venue；未核实项显式标注。完整清单见各线记录；本文是综合与空白判定。

## 1. 核查中解决的事实矛盾

1. **Filtered-DiskANN 无更新协议**：原文 §5.4.4 仅定性论证 FilteredVamana 更适合插入，动态删除明确留 future work。EMA Table 1 的 "Dynamic=✓" 不成立，MCI Table 1 的 "Upd.=✗" 接近事实。此前本地记录的两表矛盾就此结案。
2. **SeRF 是 SIGMOD/PACMMOD 2024**（DOI 10.1145/3639324），不是 VLDB'23。
3. **FreshDiskANN 只有 arXiv（2105.09613）**，无正式 VLDB 版本；**SPFresh 是 SOSP 2023**（非 SIGMOD）。
4. **"Diff-Index/LEAPER" 作为动态 posting list 文献不存在**；IR 侧正典是 Cutting & Pedersen (SIGIR'90)、Lester et al. (ACSC'04/IP&M'06)、Büttcher & Clarke (SIGIR'06)。其成本模型只能迁移"分层缓冲/触发阈值/摊销记账"，**图的边修复成本是向量侧独有**。
5. MCI（2604.22171）附录 A 自述静态构建，其 Table 1 的 Upd. 列是其对别人的判断，自身不是动态证据。
6. UNIFY 存在：PVLDB 18(4):1118–1130 (2024)。NHQ-NPG 正式 venue 未核实（仓库自述 TKDE-under-review）。

## 2. 三条线的结论

### 2.1 动态/可更新 ANN（19 篇/族）

| 协议族 | 代表 | 成本模型 | 空白 |
|---|---|---|---|
| 缓冲+批量 merge | FreshDiskANN (arXiv'21)、LSM-VEC (arXiv'25)、Milvus segment (SIGMOD'21) | 摊销；recall-删除比例退化曲线已有实测 | — |
| 局部就地+阈值重平衡 | SPFresh/LIRE (SOSP'23) | 局部化工程界 | — |
| per-op 就地边修复 | IP-DiskANN (2502.13826)、Creator (2503.00402，patch 修复量比 FreshDiskANN 少 ~66%) | 实测修复量 | — |
| 属性-only 廉价更新 | **EMA**（2606.00734：边 Marker + 一跳反向 OR；属性-only 2.5s/1M vs 向量插入 90.8s/1M；20%/10%/50% patch/重建阈值） | patch ≈ 12% 重建成本 | OR 单调不可回收 |
| 分区/区间动态 | DSG (PVLDB 18(10)'25，插入期望 O(log n) 新边的**理论界**)、DIGRA、RangePQ+ (PACMMOD 3(3)'25) | DSG 有理论界 | 仅单数值 range |
| 子树阈值局部重建 | Curator (SIGMOD'26 §5：buffer flush + 子树更新率阈值局部重建 + 原子换入) | 启发式阈值 | tier 非语义树 |

- **属性更新与向量更新解耦计价已有先例**（Curator、EMA）——本身不能作贡献。
- **层级属性（祖先闭包/子树 MOVE/重挂）的更新协议：未见任何工作。** 已排查 EMA/MCI/Curator/Filtered-DiskANN/DIGRA/DSG/RangePQ+/TrieHI。判**空白**（置信度高；保留条款：单次检索，taxonomy relabeling/subtree migration 同义词需复核）。
- 索引级 MVCC/快照一致性：学术空白（仅专利 US12524385B2 与 Milvus/Qdrant/Weaviate 的副本可见性层文档）。
- HNSW 原生删除 = 标记删除（TPAMI'20），无物理清除。

### 2.2 多属性混合过滤（20 篇）

- **谓词执行阶段**：pre（Milvus bitset、Faiss selector、ELI）、post（PASE）、in（Filtered-DiskANN、ACORN、NHQ/HQANN 融合、SeRF/DSG 边级区间、UNIFY 边 bitmap、LSSG、TFANNS）、执行层统一（VBase relaxed monotonicity）。**唯一显式做执行策略自适应选择的索引级方案是 UNIFY**；AnalyticDB-V 在计划层做代价选择。
- **集合四语义支持矩阵**（详见检索记录）：containment（query⊆item）有 TFANNS/ELI/UNG/LSSG/Curator/UniFilter；overlap 有 Filtered-DiskANN/LSSG/Curator/ELI 附录；equality 有 UNG/LSSG/CAPS；**item⊆query（反向 containment）无任何 in-filter 索引加速，全部只能 pre-filter 或 callback——空白**。
- **复杂布尔（NOT/嵌套）索引级仅 ACORN**；系统级（Milvus/VBase/AnalyticDB-V）执行层全覆盖。
- **属性-导航协同谱系**：融合距离（NHQ/HQANN/RWalks）、边级标注（SeRF/DSG/UNIFY/FilteredRobustPrune）、入口协同（UNG 的 LNG、TFANNS 稀有标签锚定、LSSG MinHash 入口、Curator Bloom 路由）、量化协同（iFVS）。**joint-embedding 路线语义偏移（硬布尔软化）+ 属性变更=向量变更的更新灾难，证据充分，本方向排除。**
- 层级属性 × 多属性联合索引：除 TrieHI（目录感知）外无人原生支持层级。

### 2.3 联邦/分片搜索（28 篇/族）

- 经典三段式（representation/selection/merging）：CORI'95、ReDDE'03、CRCS'07、Rank-S'12、Taily'13、LTR'17、Dynamic Shard Cutoff'18（per-query per-shard 不等预算的最直接先例）；合并：TA/NRA（实例最优，严格停）、RRF、分数归一化。
- **向量同空间 ⇒ 文本侧的分数归一化问题消失**，合并大幅简化；但 ANN 侧现有实践只有全局统一 ef/nprobe，**per-shard 不等 ANN 预算未见（尚未充分排查）**。
- **向量 shard 的距离分布建模路由（Taily 向量版）：基本确认空白。**
- **Sample-ANN 路由（CRCS/Rank-S 向量版：中央样本索引 + 按 shard 归属投票）：未见显式工作（尚未充分排查）。**
- **利用"谓词决定的层级子树"做路由/预算/合并：无先例**（仅有的层级先例：Lu & Callan'04 P2P poster、SHiRE 学位论文、Hierarchical BM25 2608.00229 preprint、PLAID SHIRTTT 时间分层）。**三路扫描共同认定的最强新颖性候选。**
- ANN 分片学术工作：Gottesbüren et al. (PVLDB 18(6)'25，图划分 sharding + kRT/LSH routing)、Semord (2609.25514 preprint)、SHINE (2507.17647)、PLAID SHIRTTT (SIGIR'24)。全部 flat shard、静态划分。
- 更新并发下跨 shard 合并正确性：工程有解（Milvus TSO、Vespa bucket merge），学术无形式化——空白。

## 3. 空白汇总（按置信度排序）

| # | 空白 | 置信度 | 对应任务书方向 |
|---|---|---|---|
| G1 | 层级属性（祖先闭包/子树 MOVE/重挂）的更新协议 | 高（附同义词复核保留） | D |
| G2 | 谓词决定的层级子树上的路由/预算/合并（联邦视角） | 高 | A |
| G3 | per-shard 不等 ANN 预算（向量版 Taily/Rank-S/Dynamic Cutoff） | 中（尚未充分排查） | A |
| G4 | sample-ANN 路由（中央样本索引 + 节点投票） | 中（尚未充分排查） | A |
| G5 | 索引级 MVCC/快照一致性 + 并发合并正确性形式化 | 高（学术空白） | D |
| G6 | item⊆query 的 in-filter 加速 | 中高 | C |
| G7 | 四集合语义 + AND/OR/嵌套统一 + 动态更新 | 中高 | C、D |

## 4. 来自本仓库实测的硬约束（候选设计不得违背）

1. **几何下界在大 scope 失效**（2026-10-28：d=1024 块剪枝 0%）→ 任何"摘要下界剪枝"在 WIKI 上预期无效；路由/剪枝必须用分布估计等启发式并实测召回损失。
2. **fan-out 启动成本不是瓶颈**（2026-10-29：目录枚举 ≤2.5%、merge ≤0.6%）→ 路由的价值定位只能是"减少大 scope 内访问的 item 数"，不是"省启动"。
3. **B1 是强基线**（同 recall 快于 B0、快于精确扫描）→ 新机制必须在同 recall 下打败 B1 才算数。
4. **局部 HNSW 是召回损失唯一来源**（D4）→ 预算分配可以直接用召回曲线标定。
