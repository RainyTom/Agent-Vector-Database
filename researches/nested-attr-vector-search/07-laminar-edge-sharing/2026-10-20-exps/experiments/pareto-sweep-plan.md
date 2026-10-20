# 重启实验：参数扫描与帕累托前沿

日期：2026-10-20。状态：**规划待远端执行；现有B1数据的探索图已生成**。用户明确以效率–recall trade-off 的帕累托前沿作为本轮SOTA判据，因此重新开启系统参数评测，替代此前“本轮不再新增实验”的执行状态。本轮不要求LES全面胜出，也不直接启动A1/联合构图。

## 1. 判据与比较域

单个配置p支配q，当 `recall_p >= recall_q` 且 `QPS_p >= QPS_q`，至少一项严格大于。两轴都最大化；不按r95/r98把所有点简化为通过/失败。某策略未达其原r95目标，其0.9453点仍是合法曲线点，但不可标成r95点。

**主结论：固定预算下的二维前沿。** 每个预算先排除完整索引字节超限的配置，再对剩余配置求各方法前沿和所有方法的联合前沿。预注册预算为54、64、80、128 MB（10^6 B），不是强迫每个方法恰好占满预算；小于预算的配置均可竞争。主图明确是index bytes，峰值RSS另图/表，不能混称内存预算。包含共享向量、必要元数据、policy文件及外部必需查询结构；固定输入与query向量不作为某一方法私有索引重复计费。

**补充：三目标前沿。** 同时最小化空间、最大化recall/QPS，不将53.27和53.58MB视作相等。构建耗时与峰值内存单列，不隐去LES底图构建。

结论分三层：

1. 已测配置集合的非支配点：计算事实，不需要有统计显著性才允许展示。
2. 参数充分扫描后的经验前沿：需要双方覆盖相近recall区域，不能依靠竞争方法没有扫到高recall的空白。
3. 稳健的前沿贡献：冻结配置在新数据/重复运行下保留优势，报告不确定性。本项目以此作为目标，但只相对于列明的比较集合与负载，不扩大为全领域已穷尽的SOTA。

不使用直线插值值当作实测QPS。折线仅用于连接离散点。允许另报 `best_measured_QPS(r)=max{QPS_i:recall_i>=r}` 的阶梯包络，未覆盖区域为NA，不外推。不把随机混合两个配置的效果假定为线性插值。

## 2. 已完成的探索图

文件：`../../figures/pareto_b1_observed_20261020.{png,pdf,points.csv,frontiers.csv}`。

来源仅为225条冻结B1 block与对应recall summary，15配置、300条历史重划留出查询。没有加入post-hoc ef72/80或历史1000条其他计时会话。空间取已归档index bytes并加对应policy文件字节，未重测RSS。

- 54MB图中，LES policy r98是一个已测联合前沿点，其他联合前沿点来自M12；**M12最高冻结recall仅0.979，故LES高recall前沿资格首先说明覆盖缺口，不证明高recall算法优势**。
- 64MB图中，加入M16后，现有LES点不在联合二维前沿；这个结论也只适用于当前离散点集合。
- 空心点保留被支配配置，实心折线为各方法非支配点，黑色虚线/圈标联合前沿，竖线为QPS IQR，不是recall置信区间。

画图工具可用于新扫描，不需要另写绘图程序：

```bash
python production/nested-attr-vector-search/src/plot_pareto_frontier.py \
  --b1-data researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps/data \
  --out-prefix researches/nested-attr-vector-search/07-laminar-edge-sharing/figures/pareto_b1_observed_20261020 \
  --budgets-mb 54,64

# 新扫描完成后：一个dataset/split/seed/计时协议cohort一份表
python production/nested-attr-vector-search/src/plot_pareto_frontier.py \
  --points PATH_TO_MEASURED_POINTS.csv \
  --out-prefix PATH_TO_NEW_FIGURE_PREFIX --budgets-mb 54,64,80,128
```

输入列：`method,config,recall,qps,qps_q25,qps_q75,index_bytes,cohort`。config必须唯一；每点来自相同配置多block，不允许不同M/ef/policy取各自最好值后拼一个点。缺失/失败点不填0、不进图，失败原因单独留存。方法名建议保留M及layout用于细分图，再另生成按算法家族合并的总包络。

## 3. 远端扫描矩阵：先覆盖，再精化

### 第一阶段：LES与共享PSG的完整竞争曲线

| 参数 | 预注册取值 | 控制要求 |
|---|---|---|
| 底图M | 8,12,16,24,32 | LES与PSG使用同seed、同底图导出规则；预算超限仍登记字节 |
| efConstruction | 初扫200 | 仅前沿邻域的M补100/400，双方同样机会；每个配置重建一次 |
| 查询ef | 10,12,14,16,20,24,28,32,40,48,56,64,72,80,96,128,192,256 | k=10；所有候选方法覆盖同目标recall区域，不要求同ef代表同工作量 |
| beam规则 | fixed、旧adaptive、B1 | 每种规则独立曲线；不能只为LES调策略 |
| LES布局 | varint主线；u32作预算允许时的速度/空间消融 | 同底图/同ef结果应一致，不能把布局收益当作recall提升 |
| LES桶裁剪 | 初扫不裁剪；前沿邻域再试8/16/24 | 每项独立索引；不使用全局top-24负结果配置充当主线 |
| 图seed | 初扫固定1个，验证3个预注册seed | 不能挑最好seed；单seed曲线及跨seed分布分别给出 |

“前沿邻域”仅在calibration确定：实测前沿及同recall要求下QPS距前沿不超过20%的配置。20%为预注册保留带，不代表统计等价；bootstrap不确定性很宽的候选一并保留。给两方相同搜索机会并记录实际配置数量与校准时间。

recall目标网格：0.90、0.925、0.95、0.96、0.97、0.98、0.985、0.99、0.995。若某方法未达最高目标，继续ef512/1024，或记录平台期/资源上限，不删去失败。若最低ef已超过低recall目标，保留真实端点，不通过截断答案制造低recall点。

在calibration相邻点跨越目标或曲线交叉处补中间ef（整数，不低于k），最多两轮精化，防止只扫粗网格造成虚假的前沿。同一轮提出双方的新增配置后一起执行。

B1不能只训练r95/r98两张表：对上述目标网格生成策略候选，fit建表、validation选表并冻结；策略是否达到原目标与实际test坐标分开记录。稀疏桶用预定义fallback，记录最大桶recall。每个方法/索引各自校准；不将未达目标点删除，只按实际recall绘图。

### 第二阶段：覆盖现有其他基线

只有LES/PSG局部扫描完成还不足以宣称相对于全部filtered ANN方法的前沿。沿用现有实现加入：

- BF/Filtered-HNSW：M16/32/48、efC200起步、查询ef32/64/128/256/512/1024/2048。
- ACORN：先核实本地实现暴露的gamma、M_beta/M参数及语义限制，记录实际支持的配置，再在其文档允许范围扫描；未对齐多标签Subtree则标“语义不匹配”，不能伪装成公平落后基线。
- PathSeer：先核对本地实际接口，扫描已有M_exp及搜索宽度选项；不要用一次固定配置代表方法包络。参数名/数值核实后写入manifest，未验证前不编造命令。
- PFS：精确recall=1的参考点，完整计时；TRIEHI+同一BF图若加入，注明是目录层消融，不作为另一种独立图算法的创新。

各方法必须遵守相同过滤语义、距离/量化、k、顺序、线程与资源口径。原生接口不支持者列覆盖缺口，不以不可执行的参数填曲线。不要求不同图算法统一内部结构，但不得隐藏backend差异。

## 4. 数据划分、调参与冻结

1. 原1000条仅用于历史探索图。优先从未使用过的query vector id构造新查询，fit/validation/test每组至少1000；保证query id级别不重叠，并保存scope及全部输入哈希。若不足，报告实际数且保持“回顾诊断”标签，不重新洗牌后称全新独立测试。
2. 先在calibration扫全部参数，用validation决定保留哪些配置；test前冻结**整组曲线配置**，不是每方法只冻一个r95点。
3. test可以评估全部冻结配置并画出其观测前沿，但不得用test新增/调整配置。报告“validation选择的前沿配置在test的表现”与“所有冻结点的test经验包络”两层，防止将事后包络当作无选择偏差的预测性能。
4. 先sift100k完成协议，再用SIFT25k/50k、MNIST25k、Fashion25k检查是否只对一个规模有效。每个dataset单独图；前三者不是三个独立语料。量化代理语料另列，不参与原生任务主张。

## 5. 运行与统计

继续扩展现有参数化runner，不新建冗余扫描控制脚本。配置用JSON manifest提供method/index/ef/policy/seed路径；修复硬编码/tmp、清理继承GRAPH_*控制变量；写实际环境、版本和参数。每run使用新目录，禁止覆盖旧CSV/日志。

- calibration初扫各配置3个交错block，前沿及保留带进入正式测量；正式test15个交错block，同一CPU亲和性、单线程、显式预热。保存CPU/NUMA/频率可用信息，缺失项如实记录。
- 每block所有保留配置按固定随机seed打乱，主QPS=查询数/batch wall；完整query latency另报p50/p95/p99。
- 所有点先查合法性/重复/填满/recall；逐轮保存per-query hits与timings的摘要和原始哈希。不把重复同查询15次当作15个质量样本。
- 速度不确定性：按block整体bootstrap，所有方法用相同抽样block。recall差异：在新test上按query成对bootstrap；同scope相关明显时另给scope簇bootstrap敏感性。不能用计时IQR代替recall CI。
- 从bootstrap样本重算前沿成员率可作为描述性稳定性指标，明确其重采样单位与条件；不将成员率自动解释为算法全局最优概率。
- 逐桶recall作为质量诊断；主曲线仍按预注册聚合定义。可另做“每个样本充足桶都达标”的约束前沿，不能混同两种目标。

## 6. 交付与决策

回传每dataset/seed/cohort的全部测量点、筛选理由、失败配置和资源字节；图至少包括每方法折线、联合前沿、预算分面、每桶质量补充表；输出PNG+PDF及生成CSV/命令。只连接实测非支配点，不拟合平滑曲线。

判定方式：若LES在充分扫描后，在某个明确预算/recall区间形成可复现的联合前沿贡献，即按用户定义达成本比较域的前沿目标，不要求所有区间获胜。若只在竞争者未覆盖区间或单seed端点出现，结论是“覆盖不足/待验证”。若完整曲线始终落后，记录整个trade-off的负结果；不能凭两个阈值点提前终止。

本轮先完成第一阶段及新图；第二阶段是扩展SOTA比较集合的必要工作，完成前只能使用“相对LES/PSG已测配置的经验前沿”表述。A1/联合构图依然不是当前任务。
