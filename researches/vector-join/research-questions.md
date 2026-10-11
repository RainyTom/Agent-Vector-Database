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
