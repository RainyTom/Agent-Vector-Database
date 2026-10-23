# 2026-10-23：问题对照范围纠正

## 当日目标

纠正“时间窗约束的语义 episode 检索”的相关工作分类，避免用输入输出语义不同的单对象过滤检索支撑问题新颖性。

## 核查方法

按四个字段重新划分工作：查询单位、数据单位、返回单位、排序/约束语义。只有共享“轨迹、序列或子轨迹检索”语义的工作进入问题新颖性矩阵。

本轮是文献分类纠正，没有运行新的性能实验，也没有新增结果数字。

## 本轮新增核查

用户指出 Quantum Range Query 属于轨迹+时间戳混合搜索，并提供六篇轨迹相似检索论文。下载并逐篇核对原始 PDF；书目信息以 arXiv 与 Crossref 正式记录交叉确认。

| 工作 | 核查结果 | 对当前问题的影响 |
|---|---|---|
| Towards Quantum Range Query…，arXiv:2609.29612v1 | 轨迹段记录含 temporal/spatial/semantic 分量；窗口内精确 range reporting；实验只做 Time/Space/Space-time | 从“非轨迹过滤”纠正为“同数据域、不同算子”；semantic 实现仍不能确认 |
| TISIS / TISIS*，arXiv:2409.11301v2 | POI 轨迹 LCSS threshold；TISIS* 明确使用 Word2Vec POI embeddings | 语义序列匹配已有直接前序，但无 episode/top-k/时间硬约束 |
| REPOSE，ICDE 2021 | 分布式完整轨迹 top-k，支持多种轨迹距离 | 扩展性/索引参考，不是 episode 输出 |
| N-tree，ACM T-SAS 2025 | exact whole-trajectory kNN/range；DistanceAvg 为时空 metric | 时间感知精确轨迹检索前序 |
| ST2Vec，KDD 2022 | 学习完整路网轨迹的空间+时间软相似度 | “时间进入轨迹相似度”不能作为新意 |
| GTRSS，arXiv:2507.05542v1 | 每条数据轨迹先取最佳连续子轨迹，再对不同轨迹做 top-k | **输出语义与当前问题同形**；episode 定位、轨迹级去重和 top-k 已有 |
| TKDE 2024 trajectory similarity survey | 25种 measure taxonomy 与统一评测维度 | 用于距离/benchmark 设计，不是直接方法 |

## 纠正结果

直接问题家族现在分三层：

1. **输出语义直接前序**：GTRSS/TRSSQ。已覆盖查询轨迹到 representative subtrajectory、每轨迹一个结果、全局 top-k。
2. **语义/时间组件前序**：ETQ、TISIS*、ST2Vec、STKST-I。分别覆盖语义点序列、POI embedding+LCSS、时空软相似度、关键词+时间+位置。
3. **同数据域但不同算子**：Quantum Range Query。它解决轨迹段时空范围查询，不解决语义相似度 top-k。

ACORN、Filtered-DiskANN、TANNS/TANGO、CubeGraph、LIST 仍是单对象检索，只作为执行机制参考。

## 对研究判断的影响

此前“连续 episode + 轨迹级去重 + episode 区间”候选增量被 GTRSS/TRSSQ 否定。当前可检验增量收缩为：

- 绝对时间窗、持续时间与空间范围的硬 episode 可行性；
- 在该可行域内进行稠密语义序列排序；
- 比 `hard filter → exact/approximate TRSSQ` 更有效的联合访问路径。

因此当前方向应称为 **constrained semantic representative subtrajectory query**。不能宣称“首次 episode 检索”；若硬约束没有改变算法或不能优于直接适配，应 pivot。

## 持久化

- 新增 `papers/trajectory-search/` 主题与7条结构化论文记录；
- 保存7份原始 PDF、日期化元数据与逐篇问题定义证据；
- 更新问题对照、新颖性审计、前序工作、检索日志和研究入口；
- 新增 survey→REPOSE、survey→ST2Vec、GTRSS→REPOSE 三条原文关系。

## 追加：TRSSQ 直接前序核查（同日）

GTRSS 在正文中把 TRSSQ 归给 Wang et al.（ICDE 2024），并称其为“the only approach directly targeting the TRSSQ problem”。据此完成书目与可获取证据核查：

| 项 | 结果 |
|---|---|
| 书目 | DOI:10.1109/ICDE60146.2024.00335；ICDE 2024, pp. 4396–4408；Crossref/OpenAlex/官方列表一致 |
| 方法（转述） | RSSE：GRU 点级匹配 + 轨迹编码对齐 + 局部相似度，filter-validate |
| 全文 | 未获取：Unpaywall closed；IEEE 202 挑战页；CSDL 无摘要；S2 429；arXiv 无 |
| 被引 | OpenAlex 2 次（MATIBL 2025、SimCFT 2026），均非受约束 TRSSQ 后续 |

对判断的影响：

1. **问题定义早于 GTRSS**：TRSSQ 2024 已给出“每轨迹代表子轨迹 → 全局 top-k”，GTRSS 只是图索引后继。
2. **学习型候选估计也不新**：RSSE 已用 GRU 估计相似度并过滤；把“学习/语义打分”当成贡献会被直接对应到 RSSE。
3. 因全文未读，**不能**断言 TRSSQ 不含时间/空间/语义约束；这是待核项，不是已排除项。

新增文件 `evidence/trssq-2026-10-23.md` 记录书目、摘要残段、GTRSS 原话、获取尝试与未核实清单；该文**不进入** `papers.jsonl`（全文缺失），总数仍为 28 篇/25 关系。

### 全文核对（同日，用户提供正式版 PDF）

用户提供 13 页 IEEE 正式版 PDF（本地 sha256 `99e38b36…`），已入库并逐节核对。上文“全文未获取”与“总数仍为 28 篇/25 关系”**作废**，以本节为准：

| 项 | 全文核对结果 |
|---|---|
| 问题定义 | Definitions 1–4：每轨取 Θ 最大的连续子轨迹，再跨轨迹取 top-k |
| 度量 | DTW、EDR、ERP、LCSS（LCSS 用距离形式） |
| 时间 | 点含 $t_i$，§II-A 明言省略；无时间窗、持续时间或空间范围约束 |
| 语义 | 无 POI/text embedding |
| 方法 | RSSE：Bi-GRU + 点级 attention + interaction + pooling + 3 层 FC；MSE + 排序损失；层次化采样率 $r$ |
| 保证 | 无端到端精确/召回保证；RSSE 取 top-$M$ 后仅在候选内用 ExactS 验证 |
| 实验 | Xi'an 85,802 / Chengdu 221,781 / Porto 1,252,547；30k 数据 + 2k 查询；RSSE 全面优于 SRN/T3S/TMN；LBF 在 ExactS 验证下 RR 多数最优 |
| 索引 | 转正为第 29 篇；关系 27 条（新增 GTRSS→TRSSQ、TRSSQ→REPOSE） |

结论不变且更强：episode 定位、轨迹级去重、top-k 与学习型候选过滤均已有前序；本方向只剩“语义序列距离 + 绝对时间窗/持续时间/空间硬可行性”的组合及其执行优势。更新后的下一步：复现 `hard filter → RSSE + ExactS` 作为基线，再做引用闭包。

## 追加：问题定义 v2 —— 语义载体放在停留段

v1 假设每个采样点都带语义向量。按学术定义检查后发现该建模不成立，改为“语义只挂在停留段（停留/POI）上”，transit 采样点不携带向量。

判定依据（研究者设计，不是文献结论）：

1. **重采样不变性**：同一停留内改变采样频率不应改变分数。逐点向量 + DTW 会按采样数重复匹配，任何按路径长度的归一化都随采样率变化。
2. **episode 规范化**：逐点时，同一停留内的多个下标区间投影到相同语义序列，最优 episode 不唯一；停留段上一个区间对应一个语义序列。
3. **查询可比性**：$n$ 由采样时钟决定（可达 $10^3$–$10^5$），$m$ 由活动数决定（3–10）；逐点距离被采样率主导。
4. **语义未定义**：逐点向量只能来自最近 POI 吸附（分段常数，等价于已做段聚合）或逐点学习编码（缺少“该采样点属于什么活动”的监督）。

同时把归一化从 $D/\max(m,\lvert E\rvert)$ 改为 $D/m$：按路径长度或较长序列归一化时，给 episode 增加停留会同时增大分母，可能产生“越长越优”的病态；固定查询长度使额外停留只增加代价。

影响评估：

- 现有 Massive-STEPS 实验中每次签到就是一个停留段，因此已跑结果与 oracle 不失效；v2 只是把隐含假设写成定义。
- 需新增的验证：停留段划分参数（时间阈值、空间漂移）如何改变 $s$、候选规模与 Recall@10；路径口径 $R$ 与 $\lambda$ 的敏感性。
- 本轮未运行新实验，未生成新数字。

文档：`problem-definition.md` 重写为 v2；`problem-comparison.md` 的 §0 同步为停留段记号。

## 追加：问题定义 v3 —— 不相交段划分（取代 v2 的载体选择）

v2 把语义挂在停留段上，留下的 transit 点不携带向量；这会让空间约束与 episode 边界在 transit 段上产生歧义，也无法表达“坐地铁”这类移动语义。用户指出更合适的原语：**一条轨迹划分为多个互不相交的连续段，每段一个语义向量**。

v3 定义（`problem-definition.md`）：

- 划分 $\Pi=\{\sigma_1..\sigma_s\}$，$I_k=[l_k,r_k]$ 连续、互不相交、覆盖 $[1,n]$；$\sigma_k=(I_k,\mathbf e_k)$；
- episode 为连续段区间 $\tilde T[u:v]$，原始点为 $X(E)=\bigcup X_k$；
- 可行：时间窗、时长容差、$X(E)\subseteq R$、段数 $\ge m$；
- 分数：段向量上的 DTW + 相对时间惩罚，归一化 $D/m$；
- 输出：每轨迹最佳 episode，全局 top-k，轨迹不重复。

相比 v2 的收益：覆盖性使空间约束与起止时间无歧义；段区间与语义序列一一对应，最优 episode 规范；逐点模型与“只留停留段”都成为特例。

代价是新增三个必须写明的假设：

- **A1 段内语义同质**：一段只表达一种活动/移动方式。固定时间分箱会违反它（“咖啡+办公”被压进一段），因此默认用活动/停留感知的变化点划分，分箱只作敏感性分析。
- **A2 单向量**：多标签段需先聚合；多向量扩展会破坏三角不等式，需另评估剪枝。
- **A3 划分数据侧固定**：不随查询变化，同一数据集共用规则与参数。

新增待验证项：A1 的纯度（段内标签熵）、划分参数对 $s$/候选规模/Recall 的影响、聚合规则、$D/m$ 与可选时长项 $\mu$、段边界量化对 episode IoU 的影响。

本轮未运行新实验；Massive-STEPS 的每次签到本身即一个段，已跑结果仍有效。

## 追加：问题定义 v4 —— 数据对象收敛为坐标/时间/向量集

按讨论把定义收敛到最小数据对象：一条轨迹是 $T=(X,\tau,E,\beta)$，即坐标序列、时间戳、**段向量集**，以及采样点→段的归属 $\beta$。分段怎么做不在定义内，只保留契约：

- C1 连续：$\beta$ 单调不减；
- C2 覆盖且非空：$\beta$ 满射，每段至少一个采样点；
- C3 查询无关：$\beta$ 与 $E$ 在数据集上固定；
- C4 编码固定：$\mathbf e_k$ 由固定编码器产生。

查询问题相应固定为：给定 $Q=(\mathbf q_1..\mathbf q_m;\tau_1..\tau_m;W;R;\delta_\Delta;k)$，在轨迹库中找 top-k 轨迹，使每条轨迹存在一个连续段区间 $[u,v]$ 满足绝对时间窗、持续时间容差、$X(E)\subseteq R$、段数 $\ge m$，并在段向量序列上按 DTW + 相对时间惩罚最小化 $D/m$。

这样处理的好处：$s=1$（整轨迹）与 $s=n$（逐点）都是特例；不同分段/编码实现只要满足 C1–C4 就在解同一个问题，结果可比；算法、索引、剪枝明确留在系统层。

本轮仍未运行新实验。

## 局限

- 没有运行新性能实验，也未复核论文报告的性能数字；
- ICDE 2024 `Efficient Learning-based Top-k Representative Similar Subtrajectory Query` 原文尚未获取/精读；
- GTRSS/TRSSQ、ETQ、TISIS* 的完整前后向引用闭包尚未完成；
- Quantum Range Query 当前为 arXiv v1，正式发表状态未核实；
- 当前仍不能声称完整组合无人研究或足以构成独立问题。

## 下一步

1. 获取并精读 ICDE 2024 TRSSQ 原文，核对 learned distance、输出去重、约束接口、代码与 DOI；
2. 实现/复现 `hard filter → exact TRSSQ`，再评估 GTRSS 式近似搜索；
3. 用相同语义距离、硬可行性、global trajectory recall 与资源预算对比当前方案；
4. 做 GTRSS/TRSSQ、ETQ、TISIS*、STKST-I 引用闭包；若发现全覆盖工作则 pivot。
