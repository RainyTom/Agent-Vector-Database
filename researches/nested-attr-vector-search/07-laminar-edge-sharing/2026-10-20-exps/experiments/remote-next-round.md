# LES-v3 下一轮远端实验指导（2026-10-20）

执行回传后的后续入口：[round2预算策略指导](remote-round2-budget-policy.md)。下文保留原交接；完成状态及未完成项以新指导的文件审计为准。

状态：**待远端执行**。本文件是研究侧向实验侧的 Git 交接协议，不是实验结果。先完成 P0/P1，再按证据选择 P2；本轮不直接投入联合构图大改。

具体改进规格见 [les-improvement-plan.md](les-improvement-plan.md)：优先 E0 搜索内存复用、S1 共享存储下恢复作用域邻接；R1 并集补充搜索与 A1 多入口按条件进入。远端不仅要测瓶颈，还须完成 E0 和 S1 静态空间分析，并按门槛决定结构原型。

## 1. 目标与已核查问题

目标：解释 sift100k 上 LES-v3 与共享向量 PSG 的吞吐差距，判断 53.3 MB 的空间优势在严格召回门槛及相近空间预算下是否成立。任务是 Subtree filtered top-k ANN，不是 vector join。

依据：同目录 `../data/five_l2_les_v3_20261020T104139Z-68414a0f.csv`、`five-l2-benchmark.md` §9/12，以及 `production/nested-attr-vector-search/src/{les_query.cpp,baseline.cpp}`。

1. 共享 PSG ef24 的 recall=0.9494，**不满足严格 r95**。旧 0.50× 只能标为近似工作点比较。已存 CSV 中固定 PSG ef32 达 0.9673/58,606 QPS，adaptive ef32 达 0.9559/68,866；这些可用于审计旧表，但新的配对结论必须重测。
2. LES 34,313 QPS 对应 varint、**adaptive=0**、ef12；不能把这一行标为 adaptive 的收益。四格消融必须拆开 layout 与 beam。
3. LES `QUERY_SECONDS` 是各轮逐查询时间和的最小值，PSG 是批次 wall time 最小值；两者 timings 文件均保留最后一轮。不能用最好轮 QPS 与最后轮 p95 共同描述一个样本。
4. PSG `baseline.cpp` 先构造 `node_qs`，按作用域集中查询；LES 按原始 qi 顺序。当前对比混入调度/缓存局部性收益。
5. LES 的 `SCANNED/distcomp` 跨所有 repeats 累加，且不包含完整 upper-level 距离计算；不能直接当作每查询总成本。当前 PSG 导出器将 level-0 邻接截到至多 M 条（源码 `write_inline_graph`），M16 是当前导出图的上限，不是每次恰好扫描16条；必须统计真实读取条目。
6. 桶内 u16 + mapping 不改变边集和扫描条数，还引入映射读取。它是表示假设，不能预先称为解决并图冗余。

## 2. P0：冻结输入、修正测量契约

远端先 `git pull --ff-only`，记录 `git rev-parse HEAD`、工作区状态和依赖 revision。工作区有本地改动时先保存，不 reset。数据和大索引通常不随 Git 传输：确认本机已有 fixtures、query.bin、gt_sub.bin 及底图；缺失则按现有复现文档重建，记录生成 seed/哈希，不能默默换数据。

固定 sift100k、1365 节点、同一 1000 条 Subtree workload、k=10、u8 squared L2、M16/efConstruction200。核对 float32 与 u8 查询数值一致；保存 base/query/workload/GT 的 SHA256。保留旧文件，新实验使用唯一 run_id 子目录。

在现有 C++ 入口和现有参数化 runner 内扩展，不另建冗余控制脚本。新增功能在实现后才能执行，以下参数名称是接口建议：

- 两种引擎均提供 `--query-order original|scope`。P1 主实验使用 original；scope 单独作为 batch 调度消融。先确认 workload 每行只有一个 scope；若有 antichain，另列任务，不能只按第一个 scope 排序。
- 两种引擎均输出同一轮 `query_sum_seconds` 与 `batch_wall_seconds`，前者包含该查询的搜索和结果合并，后者包含在线调度/排序/合并。单列读取索引、量化、预处理时间；预计算调度如复用必须声明 amortized，不混成在线成本。
- 分离 warmup 与正式轮，输出每轮结果/耗时。未改计时代码前，`--repeats 1` 可避免 best/last 混用，但不能解决调度和计时边界差异。
- `GRAPH_ADAPTIVE` 在代码中按环境变量是否存在启用：固定 beam 要 **unset**，不能设为 `0`。`GRAPH_SHARE_VECTORS` 同理。

先跑正确性小检查：原顺序/作用域顺序返回结果恢复到原 qi 后一致；u32/varint 在相同固定 ef 下逐位一致；插桩开关不改变 id。检查索引版本、counts 是否存在及其数值；缺失时 adaptive 静默退回固定 beam 必须报失败。

## 3. P1：最小实验矩阵

| 因素 | 配置 |
|---|---|
| 主比较 | LES u32 fixed、u32 adaptive、varint fixed、varint adaptive；共享 PSG fixed、adaptive |
| 结构 | 从同一份 M16 底图生成 LES 两种布局；不启用桶裁剪、prefetch、扫描混合规划 |
| ef 初扫 | 12,16,20,24,28,32,40,48,64,96；不达目标继续至128/192并标记范围 |
| recall 目标 | 严格 >=0.95、>=0.98；不取整、不放宽、不用插值点当实测 |
| 主顺序 | original；选定工作点后再跑 scope 调度消融 |
| 重复 | 每配置预热1轮，正式至少7个交错 block，单线程、同一 CPU affinity |

先用独立 calibration queries 选各方法/目标的 ef，再冻结并测 test；为 calibration 单独生成过滤精确 GT，记录生成器、seed、哈希及与 test 无交集。当前 1000 条可作为历史诊断集保留，但若继续在该集合调参，须标为 retrospective，不能声称独立测试泛化。固定现有 adaptive 两个阈值，不在 test 上继续搜阈值。

每个 block 所有配置各跑一次，以固定 seed 随机化或平衡反转运行顺序；记录温度/频率可用信息、CPU/NUMA、编译命令和并发负载。禁止同时跑其他重负载实验。记录每轮而非只保留最好值。若波动仍接近30%，先查运行条件，不能用挑选最快值消除。

报告每配置实际 recall、query-sum QPS、batch-wall QPS、p50/p95/p99、每个 block 的 LES/PSG 配对比值及其中位数/IQR。以 block 为重采样单位给 bootstrap 95% CI；7轮只作初筛，CI宽则补至15轮。若 test 未达阈值，写“未达标”，不要回到 test 调参后仍标独立测试。

分桶：可行对象数 `0`、`1–9`、`10–499`、`500–9999`、`>=10000`，另按树深度统计。单 scope 从去重可行集计算基数，不用标签重复数。每桶报告 nq、实际 ef 分布、recall、延迟、填满数、违规和重复 id。桶内 QPS 用桶内查询数/耗时和，不平均单查询 QPS。

历史 `score()` 分母固定 nq*k；主历史复现保留该定义并说明。新协议另报 `hits/min(k,|F|)` 的 query-average recall（空集排除并单列 empty correctness）及 micro recall=`sum(hits)/sum(min(k,|F|))`，不能混名。所有返回 id 必须合法、去重；|F|<k 时返回全部可行对象，其余槽位为 -1。

## 4. P1：独立诊断运行与成本归因

新增可关闭的 per-query counters，保持默认性能二进制热路径无计数负担；诊断结果单独跑，禁止把插桩版 QPS 放进性能主表。PSG 和 LES 使用相同计数定义：

- `upper_distance_evals`、`l0_distance_evals`（统计实际调用，包括入口）；
- `expanded_nodes`（level-0 实际弹出并扫描邻接的次数）；
- `scanned_edges`（visited 判断前读取的邻接条目）、`visited_skips`；
- `selected_runs`、`decoded_bytes`（LES 专属）、heap pushes/pops；
- scope_id、scope_size、depth、ef_actual、qid。

计数归零到每查询/每轮；scanned_edges/expanded_nodes 用来判断平均扩展出度，scanned_edges/l0_distance_evals 用来观察重复邻居成本。不要混入 upper 层条目。

可用时另跑 perf stat 的 cycles/instructions/cache-misses/branch-misses，记录事件、权限和原始输出；进程级 perf 包含加载时必须明确，不能直接归因到查询。不可用写“未测”，不阻断主实验。阶段计时也单独跑，避免细粒度时钟扰动主 QPS。

判别规则：

1. 相同查询顺序后差距显著缩小：先优化调度，论文分开报告 online 与 grouped batch。
2. LES expanded_nodes 接近但 scanned_edges/visited_skips 更多：证据支持冗余枚举；局部 id 只能压缩这部分读取，后续才考虑 scope-aware 邻接选择，并重新检查可达性/recall。
3. LES 距离计算/扩展数明显更多：先检查 upper 层入口一致性及可行区导航，再设计有/无 anchor 消融。
4. varint 降空间却增加 cycles/decoded byte：保留 u32 性能点，评估局部编号的实际字节与映射代价。

## 5. P2：按证据开展，禁止同时改多项

**优先补齐空间 Pareto 对照。** 共享 PSG M12 历史空间约53.6 MB，接近 LES-v3 53.3 MB；M16 为62.3 MB。新增 M12 的独立 ef 扫描至达 r95/r98 或明确上限失败，不能拿 M12 ef48 的0.9717就断言其无法达到r98。LES 固定 M16作为候选；另报同 M16 对照。明确“相近空间”不是“相同图”。

按文件类别累计逻辑字节：共享向量、level-0、upper、gid 映射、activation/run offsets、counts、入口和其他必需元数据；全目录字节另报。所有查询必需外部文件计入部署总成本，共享部分只算一次。RSS另报并注明是否加载未用的 float32 base；临时底图占用单列。构建分别记录底图、归并/压缩、完整 wall time、峰值RSS，不能把复用底图算作零成本。

**局部编号先做静态尺寸估算。** 明确字典范围是 source/run、activation bucket 还是 scope；计算 entries×id_width + 去重字典 + offsets + 溢出表的实际字节。覆盖 >65535 情况及边界，不能截断。若没有超过 varint 的实际总空间优势，或 mapping 成本过大，停止实现并保留负结果；实现后要求全量解码与同 ef id 一致，再测性能。

**anchor 仅在导航计数支持时做最小原型。** 保留同一 LES 图，只改变入口/增加少量可行 anchor，额外距离评估计入预算，比较同 recall、同总成本及空间。记录断连/小作用域/多标签风险。JAG/fiber/BOA 等先作为线索；本交接没有重新核验其全文，不将原型称作对应论文复现，也不据此主张新颖性。

## 6. 远端回传验收

在实际执行日期的 `.../<YYYY-MM-DD>-exps/` 新增 run_id 文件，不能覆盖本日历史 CSV。如仍为2026-10-20，使用本目录并追加 report。

- `data/<run_id>-summary.csv`：方法、layout、adaptive、M/ef、顺序、split、block、实际recall、两种耗时/QPS、分位数、完整空间、status；未测项为 null。
- `data/<run_id>-per-query.csv`：qid、scope及桶、hits/GT count、延迟；诊断 counters 单独文件。
- `experiments/<run_id>/`：manifest、命令与环境、编译日志、逐轮日志、正确性检查、统计说明；ids/timings保留可追溯路径及哈希。
- `report.md` 追加实际设置、结果数字、局限、负结果、下一步；同步修正论文/导航中 r95 门槛与 adaptive 标注，旧快照保留并加勘误链接。
- 提交代码和小型结果文件到 Git，push 后回传 commit SHA、报告路径、严格 r95/r98 配对表、计数归因结论。大索引/数据不默认入库；提供保存位置、生成命令和哈希。

完成标准：先得到严格阈值、同调度/计时边界、交错重复的曲线及成本归因，再判断 LES 的优势区间。无论正负结果均回传；不要只回传最好一行或继续无上限微优化。
