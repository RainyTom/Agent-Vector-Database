# 新颖性审计：时间窗语义 episode 检索

更新日期：2026-10-23。状态：**初步审计，未完成系统查重**。本文回答两个问题：

1. 当前问题是否已经有直接同问题工作？
2. 如果没有，是否只需要自建简单基线，而不必改造他人方案？

结论先行：**当前不能再按“全新问题”定位，更稳妥的描述是“带语义与硬时间可行性的 representative subtrajectory query”。** GTRSS/TRSSQ 已经定义“每条数据轨迹取一个最佳连续子轨迹，再做全局 top-k”，与本问题的输出语义基本同形；ETQ 与 TISIS* 已覆盖语义轨迹匹配；ST2Vec 已把时间戳纳入完整轨迹相似度。尚未发现的是这些能力与绝对时间窗、持续时间、空间范围硬约束的同一组合，但“组件组合未命中”不等于独立新问题。

## 1. 轨迹检索问题族

### 1.1 相似检索与子轨迹检索

| 工作 | 查询/输出 | 相似度中的时间 | 语义 | 连续片段 | 与本问题的关系 |
|---|---|---|---|---|---|
| **TRSSQ** (ICDE 2024, Wang et al.) | 查询轨迹 → 每条轨迹一个代表子轨迹 → 全局 top-k；RSSE 学习型 filter-validate | 点含 timestamp，但建模省略时间；无窗口约束 | 无 | **有** | **问题定义来源**；已覆盖 episode 定位、轨迹级去重与 top-k，且已用学习模型估计相似度 |
| **GTRSS** (arXiv 2025) | 同上问题；双层图索引 + DTSM 连接 | 点含 timestamp，但 DTW/EDR/ERP 主要比较几何序列 | 无 | **有** | TRSSQ 的图索引后继；输出语义同形 |
| **ETQ** (ICDE 2017) | 点序列+关键词 → top-k 轨迹 | 无查询时间窗 | TF-IDF 关键词 | 否，代表点可分散 | 语义 top-k 最近，但不要求连续 |
| **TISIS / TISIS\*** (BDA 2024) | POI 序列+阈值 → 完整轨迹集合 | timestamp 只确定顺序 | TISIS* 用 Word2Vec POI embedding | 否 | 语义序列最近，但不是 top-k/episode |
| **ST2Vec** (KDD 2022) | 完整路网轨迹 → embedding；top-k 为 case study | **空间+时间软相似度** | 无 POI/text 语义 | 否 | 证明 timestamp 已进入轨迹相似学习 |
| **N-tree / DistanceAvg** (ACM T-SAS 2025) | 完整轨迹 → exact kNN / metric range | **连续时空距离** | 无 | 否 | 精确时空轨迹索引；不是 episode |
| **REPOSE** (ICDE 2021) | 查询轨迹 → top-k 完整轨迹 | 依所选距离；主数据为几何轨迹 | 无 | 否 | 分布式完整轨迹 top-k 索引 |
| **STKST-I** (ESWA 2023) | 关键词+时间+位置 → top-k 语义轨迹 | 时间相关性 | 关键词 | 否 | 同时有语义/时间/位置，但不做 episode |
| **SimSub 家族** | 查询轨迹 → 最相似或 top-k 子轨迹 | 通常无绝对时间窗 | 通常无 POI/text 语义 | 有 | 子轨迹检索直接前序 |

### 1.2 同一轨迹数据域、不同查询算子

**Towards Quantum Range Query for Spatial-Temporal-Semantic Trajectory Data**（arXiv:2609.29612v1）不能与 ACORN 一起排除为“单对象过滤”。它处理轨迹段记录，并在形式模型中包含 temporal、spatial、semantic 分量；查询是多维窗口，输出窗口内精确匹配段的量子叠加态。

但它与本问题仍有三项算子差异：

1. QRQ 是 **range reporting**，不按相似度 top-k；
2. 返回轨迹段记录集合/叠加态，不为每条轨迹寻找最佳 episode；
3. Table 1 只评测 Time、Space、Space-time；结论把 semantic key 扩展列为 future work。因此标题中的 semantic 不能写成“已实测完整语义检索”。

### 1.3 非轨迹问题

ACORN、Filtered-DiskANN、TANNS/TANGO、CubeGraph、LIST 仍属于单向量或单对象检索。它们只作为过滤图、时间/空间分区等执行机制参考，不参与轨迹问题新颖性判定。

## 2. 最近邻差异矩阵

| 维度 | GTRSS/TRSSQ | ETQ | TISIS* | ST2Vec | Quantum Range Query | 本问题 |
|---|---|---|---|---|---|---|
| 查询单位 | 查询轨迹 | 点+关键词序列 | 完整 POI 轨迹 | 完整路网轨迹 | 多维属性窗口 | 语义 episode |
| 输出单位 | **每轨迹最佳子轨迹的 top-k** | top-k 轨迹 | 阈值内完整轨迹 | 完整轨迹 embedding/top-k | 窗口内轨迹段 | **每轨迹最佳 episode 的 top-k** |
| 连续片段定位 | **是** | 否 | 否 | 否 | 相交段，不做最佳片段 | **是** |
| 语义 | 无 | TF-IDF | POI embedding 匹配 | 无 | 形式模型有；实验未实现 | 稠密语义序列 |
| 时间 | timestamp 非主 score | 无查询约束 | 只保序 | 软时间相似度 | 时间范围硬约束 | 绝对窗+持续时间硬约束 |
| 空间范围硬约束 | 无 | 点级邻近 | 无 | 路网 | 有 | 有 |
| 排序/选择 | DTW/EDR/ERP top-k | 聚合 top-k | LCSS threshold | learned distance | range membership | 语义 DTW top-k |

## 3. 判定

**已经被覆盖的部分**：

- GTRSS/TRSSQ 已覆盖“连续子轨迹定位 + 每条轨迹一个代表片段 + 全局 top-k”；不能再把 episode 区间或轨迹级去重写成新意。
- TISIS* 已覆盖 POI embedding 序列匹配；ETQ 已覆盖语义点序列到 top-k 轨迹。
- ST2Vec 与 DistanceAvg 已覆盖时间戳参与完整轨迹相似度。
- Quantum Range Query 已覆盖轨迹段上的时空混合范围访问，但不是相似度 top-k，且 semantic 尚未实测。

**仍待证明的候选增量**：

- 绝对时间窗、持续时间、空间范围作为不可放宽的 episode 可行性约束；
- 在该可行域内做语义序列距离排序；
- 为这组约束设计比“先过滤再执行 TRSSQ”更有效的访问路径。

**当前结论**：这更像 **constrained semantic TRSSQ 的系统扩展**，而不是可以直接宣布的新检索问题。只有证明硬约束改变了算法结构、复杂度或执行计划，并显著优于 GTRSS/TRSSQ + filter 的直接适配，才有独立贡献。

## 4. 基线策略

1. **精确 oracle**：枚举时间/空间/持续时间可行 episode，再做语义 DTW。
2. **Filter → exact TRSSQ**：先按硬约束裁剪，再为每条轨迹求最佳连续子轨迹；这是最重要的直接基线。
3. **GTRSS/TRSSQ 适配**：使用相同语义距离与可行性验证；若无代码，至少复现问题等价的 filter-validate 版本。
4. **ETQ-embedding**：检验“不要求连续”的语义轨迹 top-k 与本问题差异。
5. **TISIS*-LCSS**：语义 POI 匹配的序列距离消融。
6. **ST2Vec/整轨迹 embedding + verify**：完整轨迹表示基线。
7. **当前点级 ANN + episode verify**及分区/early-abandon 消融。

Quantum Range Query、ACORN、REPOSE、N-tree 的原系统不应硬改成同题基线；分别属于 range reporting、单对象过滤、完整轨迹分布式检索和 metric whole-trajectory search。可迁移其机制，但实验标签必须写清。

## 5. 下一步（查重门槛）

1. TRSSQ 全文已精读（13 页正式版）：度量 DTW/EDR/ERP/LCSS，RSSE 过滤后仅在 top-M 内用 ExactS 验证，无精确保证，无时间/空间/语义约束。剩余：代码未公开、数字未复现。
2. 对 GTRSS/TRSSQ、ETQ、TISIS*、STKST-I 做前后向引用闭包。
3. 检索 `constrained representative subtrajectory query`、`semantic subtrajectory retrieval`、`time-windowed subtrajectory top-k`、`activity trajectory episode search`。
4. 若已有方法支持语义距离及时间/空间/持续时间硬约束，则 pivot；若没有，冻结为 constrained semantic TRSSQ，不再用“首次 episode 检索”表述。

## 6. 证据状态

- Quantum Range Query：已核 arXiv v1 全文4页；形式模型包含 semantic，实验只做 time/space/space-time，semantic extension 是 future work。
- TISIS/TISIS*：已核 arXiv v2 问题定义与 embedding 章节；全文明确 TISIS* 使用 Word2Vec POI embeddings。
- REPOSE：已核 Definition 3 与 Crossref 正式书目。
- N-tree：已核 exact kNN/range、DistanceAvg 与正式 DOI。
- ST2Vec：已核时空学习目标与 KDD DOI；完整轨迹，不含 POI/text 语义。
- GTRSS：已核 Definitions 1–4 与距离；它是当前输出语义最近工作。
- TKDE 2024 survey：已核 taxonomy；它不以语义 POI/text 为主要分类轴。
- TRSSQ：全文13页已核（定义为每轨一个代表片段 + 全局 top-k；RSSE = Bi-GRU + 点级 attention + interaction；候选预算 M；ExactS/PSS/RLS 验证；无端到端精确性保证；无时间/空间/语义约束）。
- ETQ：已读定义 1–7、算法 1–2；实验未复现。STKST-I 仍需精读。
- 本审计仍不是完整系统综述，不能据此声称“无人研究”。

## 2026-10-25 查重更新（并行子代理）

### 最接近的工作

| # | 工作 | 索引单位 | 支持的约束 | 是否返回每轨迹 episode |
|---|---|---|---|---|
| 1 | Fast Subtrajectory Similarity Search in Road Networks (PVLDB 2020) | 位置倒排 `(轨迹, 符号位置)`；候选点 + WED 验证 | 时间区间约束是**后过滤**，或按出发时间排序加速 | 返回全部匹配子轨迹，不是每轨最佳 + top-k |
| 2 | TRSSQ (ICDE 2024) / GTRSS (2025) | 轨迹级图；代表子轨迹在过滤/验证中计算 | 无时间窗/时长/空间 | **是**（每轨一个代表片段） |
| 3 | S-RAILS (ICASSP 2015) | 物化**单段**（0.4–1 s）embedding + LSH/PLEB，DTW 重排 | 仅段时长；无绝对窗/空间/多段 episode | 否 |
| 4 | RAILS (Interspeech 2012) | 帧级 embedding ANN + Hough 检测对角（相对偏移） | 查询时恢复顺序与相对时间；无跨度索引 | 否 |
| 5 | LaSTR (arXiv 2026) | 物化单段 `(series,a,b)` + Conformer embedding | 仅最小段长 | 否（段级 top-K） |
| 6 | STS-Join / STS-Index (SSDBM 2021) | 时间区间 B-tree + 段 MBR 四叉树 | 距离阈值 + 时长阈值；轨迹对 join | 否 |
| 7 | SeRF (SIGMOD 2024) / UDG (2026) / RF-ANNS (KDD 2026) | 单向量 + 区间属性图 | 单对象的区间包含/重叠 | 否 |
| 8 | QuT/ReTraTree (DMKD 2017) | 代表子轨迹层级索引 | 时间区间、LCSS | 否（聚类/离群） |

### 判定

- **未被覆盖**：把多段 episode 作为索引单位，并联合索引其跨度属性（绝对起止、时长、bbox、段数），返回每轨迹最佳 episode 与全局 top-k。检索式查重未发现同形工作，但这是**未发现**，不是证明。
- **已被覆盖、不能主张**：
  - “slack band” $j\in[u+i-1,v-m+i]$ 是 episode matching / SPRING 中已有的可行出现区间；只能作为实现剪枝与成本分析引用，不能作为新引理。
  - “物化段 + ANN + DTW 重排”已有 S-RAILS/LaSTR；单纯把段换成 episode 不足以构成方法贡献。
  - “时间约束后过滤/按时间排序倒排”已在 PVLDB 2020 明确讨论。
- **最危险的审稿意见**：这是 “PVLDB 2020 / TRSSQ + 硬过滤 + 物化”。防御必须给出：(i) 一个选择性区间，使 filter-first 与 post-filter 都失效，并测到与两者在等召回下的差距；(ii) 无误判（no false dismissal）论证 + 候选膨胀/成本模型；(iii) 单段 ANN 无法在不漏解的情况下回答 episode 查询的证据。
- **备选定位**：若加速比停在 1.5–3×，改为“何时物化 episode 目录优于精确扫描”的系统性刻画与负结果，而不是新索引。
