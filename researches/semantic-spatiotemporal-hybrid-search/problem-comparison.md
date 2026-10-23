# 问题定义对照：我们的问题 vs 现有论文

更新日期：2026-10-23。用途：让读者逐条判断新颖性。先给我们的定义，再按“最接近 → 较远”列出已有论文的问题定义。**证据级别**逐条标注；摘要级条目不是逐字定义，不能当作原文公式。

---

## 场景速览

### 我们的场景

伊斯坦布尔的城市交通分析员想回答：

> “2018 年春天，Levent 附近 10 km 内，有没有人经历过和我给的这段早晨相似的一段行程——先坐地铁，再去咖啡馆，最后到办公室，总时长 1–2 小时？给我 10 个候选，并告诉我具体是哪一天、几点到几点。”

系统返回的不是“10 个咖啡馆”，也不是“10 条完整的一天轨迹”，而是：

- 10 条不同用户的轨迹；
- 每条轨迹中**一段连续、时间可行**的 episode；
- 具体日期与起止时间；
- 一个基于语义序列对齐的相似度分数。

这就是“时间窗约束的语义 episode 检索”。地点提供语义（地铁/咖啡/办公室），时间戳提供季节窗口、持续时间和顺序，空间范围限定城区。

### GTRSS / TRSSQ 的场景（输出语义最接近）

一个网约车平台拿到一段查询路线，想在每辆车的一天轨迹中找出**最像查询路线的连续片段**，然后返回 10 辆不同车辆及各自的最佳片段。GTRSS 的做法正是：

> “每条数据轨迹先选一个与查询最相似的 representative subtrajectory，再在不同轨迹之间取 top-k。”

这已经覆盖“定位连续片段、同一轨迹只留一个最佳片段、全局 top-k”三个关键输出语义。关键差别不是 episode 是否存在，而是 GTRSS 比较几何路径（DTW/EDR/ERP），没有“地铁→咖啡→办公室”的 POI/text 语义，也没有“2018 年春天、1–2 小时、Levent 10 km”的硬可行性。

### Quantum Range Query 的场景（同一数据域、不同算子）

交通管理者画出 Levent 的空间框，再选定 2018 年春天，要求列出所有落入该时空窗口的轨迹段。Quantum Range Query 把轨迹段编码成 temporal/spatial/semantic 多维记录，并输出窗口内精确匹配段的量子叠加态。

关键差别：它回答“哪些段落在窗口里”，不是“哪些段落与查询活动序列最相似”；没有 top-k 排序，也不为每条轨迹挑一个最佳 episode。论文实验只做 Time、Space、Space-time，semantic key 仍是 future work。

### ETQ 的场景（语义 top-k 最接近）

Jack 去洛杉矶，指定两个地点（酒店、海滩），并给每个地点写活动关键词（“coffee”“swimming”）。ETQ 找的是：

> “哪些历史轨迹在酒店附近有咖啡、在海滩附近有游泳？”

它把每个查询点与整条轨迹中**任意**一个最相似点匹配，再求平均。返回 top-k 轨迹。匹配点可以分散在不同日期或不同路段，不必构成同一段连续行程。

### TISIS* 的场景

一个用户走完了一整天的 POI 序列（A→B→C→D），想找“去过相同或语义相近地点顺序”的其他完整轨迹。TISIS* 用 Word2Vec POI embedding 判定点对是否匹配，再用 LCSS 比例返回超过阈值的完整轨迹。

关键差别：它不定位“哪一段”，查询和候选都是完整轨迹；timestamp 只确定顺序，不比较绝对时间窗、间隔或总时长。

### ST2Vec、N-tree 与 REPOSE 的场景

- **ST2Vec**：道路规划者要找“空间路线和行驶时间节奏都相似”的完整行程；模型把空间与时间作为软相似度学习，而不是硬窗口。
- **N-tree / DistanceAvg**：轨迹库要精确回答“与这条完整行程距离最近的 k 条轨迹”或“距离半径内有哪些轨迹”；DistanceAvg 可比较连续时空轨迹。
- **REPOSE**：同样查询完整轨迹 top-k，但重点是用 Spark、reference-point trie 扩展到分布式数据。

三者都返回完整轨迹，不告诉分析员“具体是哪一天几点到几点的一段活动 episode”；也没有 POI/text 语义序列。

### STKST-I 的场景

用户搜索“北京春天、有博物馆或樱花关键词的轨迹”，系统按关键词、时间相关性和位置返回 top-k 轨迹。它仍属于轨迹检索，但查询不是稠密语义序列，也不定位连续 episode。

### 不纳入问题新颖性对照的工作

ACORN、Filtered-DiskANN、TANNS/TANGO、CubeGraph、LIST 的输入和输出单位都是单个向量或对象，解决的是属性过滤、有效期、新鲜度、空间范围或文本相关性下的对象检索。它们与轨迹/episode 检索不是同一个问题，**不作为直接相关工作、问题基线或“本问题尚未解决”的证据**。Quantum Range Query 不在此列：它确实处理轨迹段，只是查询算子不同。

### 一句话对照

| 场景 | 用户真正问的 | 返回 |
|---|---|---|
| **本问题** | “找一段同季节、同区域、时长相当、活动顺序相似的具体行程片段” | 每条入选轨迹的最佳可行 episode |
| **GTRSS/TRSSQ** | “每条轨迹中哪段几何路线最像查询路线？” | 每条轨迹一个最佳子轨迹，再取 top-k |
| Quantum Range Query | “哪些轨迹段落在给定多维窗口里？” | 全部精确匹配段/量子叠加态 |
| ETQ | “哪些轨迹包含这些地点和活动？” | top-k 轨迹，匹配点可分散 |
| TISIS* | “哪些完整 POI 序列足够相似？” | 阈值内完整轨迹 |
| ST2Vec / N-tree / REPOSE | “哪些完整行程在时空或轨迹距离上最相似？” | 完整轨迹 |
| STKST-I | “哪些轨迹与关键词、时间和位置相关？” | top-k 完整轨迹 |

**判断新颖性的关键问题已经改变**：GTRSS/TRSSQ 已经提供连续片段、轨迹级去重和 top-k。现在要问的是，语义序列与绝对时间窗、持续时间、空间范围的硬可行性，是否迫使算法/索引改变，并优于直接的 `hard filter → TRSSQ`；不能再把“返回 episode 区间”本身当作新意。

---

## 0. 我们的问题：时间窗约束的语义 Episode 检索（TW-SER）

### 0.1 数据（与[问题定义](problem-definition.md) v2 一致）

原始轨迹是带时间戳的采样序列。预处理把连续、近似静止的采样划为**停留段**

$$
\sigma_k=\bigl(\ell_k,\,[t^{in}_k,t^{out}_k],\ \mathbf e_k\bigr),
$$

得到压缩语义轨迹 $\tilde T=(\sigma_1,\ldots,\sigma_s)$；不属于任何停留段的采样是 transit 点，**不携带语义向量**。episode 是连续停留段区间

$$
\tilde T[u:v]=(\sigma_u,\ldots,\sigma_v),\qquad 1\le u\le v\le s .
$$

语义只挂在停留段上；每个采样点自成一停留（$\mathbf e_i$ 逐点给定）是退化特例，不是另一套问题。

### 0.2 查询

$$
Q=\bigl(\mathbf q_1,\ldots,\mathbf q_m;\ \tau_1,\ldots,\tau_m;\ W;\ R;\ \delta_\Delta;\ k\bigr),
$$

其中 $\mathbf q_i\in\mathbb R^d$ 是查询活动的语义向量，$\tau_i$ 是相对时间偏移（$\tau_1=0$），$W=[w_s,w_e]$ 是绝对时间窗，$R$ 是空间范围，$\delta_\Delta\ge0$ 是持续时间容差，$k>0$。查询时长为 $\Delta_Q=\tau_m$；查询不需要坐标。

### 0.3 可行 episode

$\tilde T[u:v]$ 关于 $Q$ 可行，当且仅当：

$$
w_s\le t^{in}_u,\qquad t^{out}_v\le w_e,
$$

$$
\bigl|(t^{out}_v-t^{in}_u)-\Delta_Q\bigr|\le\delta_\Delta,
$$

$$
\ell_u,\ldots,\ell_v\in R\quad(\text{停留口径；路径口径另含 transit 点}),
$$

且 episode 连续、不跳停留段，并满足 $\lvert\tilde T[u:v]\rvert\ge m$（保证查询可被全覆盖）。记可行集合为 $\mathcal E_Q(T)$。

### 0.4 分数与精确结果

$$
D(Q,\tilde T[u:v])
=
\min_{\pi}
\sum_{(i,j)\in\pi}
\Bigl(
\|\mathbf q_i-\mathbf e_j\|_2
+
\lambda\,\bigl|\tau_i-(t^{in}_j-t^{in}_u)\bigr|
\Bigr),
\qquad
D_{\mathrm{norm}}=\frac{D}{m},
$$

$$
s_Q(T)=
\min_{\tilde T[u:v]\in\mathcal E_Q(T)} D_{\mathrm{norm}}(Q,\tilde T[u:v]).
$$

精确 top-k 为按 $(s_Q(T),\mathrm{id}(T))$ 字典序最小的 $k$ 条不同轨迹，并返回其最佳 episode 的停留区间与时间区间。若可行轨迹少于 $k$，返回全部；若为空，返回空集。归一化分母固定为 $m$，避免“加停留反而降平均分”的病态。

### 0.5 近似结果

近似结果 $\widehat R(Q)$ 必须满足：每条结果的 episode 硬可行、轨迹不重复、$|\widehat R(Q)|\le k$。近似只放宽距离排序，不放宽时间/空间/时长约束。召回率按轨迹集合计。

### 0.6 实现限制（不是形式定义的一部分）

当前 oracle 为控制枚举成本，额外限制 episode 的停留段数 $v-u+1\in[m-2,m+2]$。若正式定义不允许该限制，需要重新生成 oracle。Massive-STEPS 中每次签到即一个停留段，故 $s$ 等于签到数。

---

## 1. ETQ：Top-k Exemplar Trajectory Queries（ICDE 2017）— 语义 top-k 近邻

**证据级别：已读论文定义 1–7、算法 1–2、相关工作表；实验未复现。**

点：

$$
p=(\mathrm{loc},\mathrm{act}),\qquad \mathrm{act}=\{t_1,\ldots,t_s\}.
$$

轨迹：

$$
T=(p_1,\ldots,p_n).
$$

查询：

$$
Q=\{q_1,\ldots,q_m\},\qquad q_i=(\mathrm{loc}_i,\mathrm{act}_i).
$$

点对相似度（原文定义 4）：

$$
S(p_i,p_j)=
\begin{cases}
0, & \mathrm{act}_i\cap\mathrm{act}_j=\varnothing,\\
\alpha S_S(p_i,p_j)+(1-\alpha)S_T(p_i,p_j), & \text{otherwise},
\end{cases}
$$

其中 $S_S$ 是归一化欧氏空间相似度，$S_T$ 是关键词 TF-IDF 相似度。

点对轨迹：

$$
S(q_i,T)=\max_{p_j\in T} S(q_i,p_j).
$$

轨迹相似度：

$$
S(Q,T)=\frac{1}{m}\sum_{i=1}^{m} S(q_i,T).
$$

Top-k ETQ：

$$
R=\operatorname{TopK}_{T\in\mathcal D} S(Q,T).
$$

**与本问题的差异**：

- 查询是点序列 + 关键词，不是稠密语义向量序列；
- 没有绝对时间窗、持续时间容差、空间范围硬约束；
- 每个查询点独立取全轨迹最佳匹配点，不要求匹配点连续，也不要求时间顺序（顺序敏感是可选扩展）；
- 输出 top-k 轨迹；虽然会报告匹配点，但不返回“时间可行的 episode 区间”；
- 不要求 DTW 序列对齐。

**为什么仍然危险**：ETQ 已把“查询语义点序列 → top-k 轨迹”形式化。将关键词相似度替换为 embedding 相似度不是贡献；本问题必须在硬时间/空间可行性与连续片段执行上给出不可由 ETQ 简单获得的结果。

---

## 2. TISIS / TISIS*：Trajectory Indexing for Similarity Search（BDA 2024）

**证据级别：已核 arXiv v2 问题定义、算法与 contextual POI 章节；实验数字未逐项复核。**

轨迹是 POI 序列：

$$
T=(p_1,\ldots,p_n),\qquad q=(q_1,\ldots,q_m).
$$

匹配函数 $\mathrm{equals}(\cdot,\cdot)$：TISIS 用 POI 相等；TISIS* 用 POI embedding 相似度阈值 $\epsilon$。

LCSS 长度：

$$
\mathrm{LCSS}(q,T)=\text{最长公共子序列长度（按匹配函数）}.
$$

相似性阈值 $S\in[0,1]$：

$$
q\approx_S T
\iff
\frac{\mathrm{LCSS}(q,T)}{|q|}\ge S.
$$

查询结果：

$$
T'(q)=\{T\in\mathcal T\mid q\approx_S T\}.
$$

**与本问题的差异**：

- 要求完整查询轨迹与完整候选轨迹的 LCSS，不做 episode 定位；
- 没有绝对时间窗、持续时间容差、空间范围约束；
- 输出是阈值集合，不是 top-k 连续排序；
- 不做 DTW，不考虑时间间隔/持续时间；
- TISIS* 的 POI embedding 只用于“匹配/不匹配”，不是序列距离。

---

## 3. TRSSQ（ICDE 2024）与 GTRSS（arXiv 2025）：representative subtrajectory top-k

**证据级别：已核 Definitions 1–4、距离定义、GARI/CNDI 框架与 related work。**

该问题**不是 GTRSS 首次提出**：TRSSQ 由 Wang et al.（ICDE 2024, pp. 4396–4408, [DOI](https://doi.org/10.1109/ICDE60146.2024.00335)）定义。方法为 RSSE：Bi-GRU 编码 + 点级 attention 匹配 + embedding interaction，Siamese 训练（MSE + 排序损失）；RSSE 对全库打分取 top-$M$，再用 ExactS（或 PSS/RLS）在候选内求代表片段与 top-k。评测度量为 DTW、EDR、ERP、LCSS；点模型含 timestamp 但 §II-A 明言省略时间维；无绝对时间窗、持续时间、空间范围或语义约束；无端到端精确性保证。**全文13页已核**（[论文记录](../../papers/trajectory-search/10.1109-icde60146.2024.00335-trssq/)）。

给定查询轨迹 $Q$ 与数据轨迹 $T_i$，候选是连续子轨迹

$$
T_i[s:e]=\langle p_s,\ldots,p_e\rangle,\qquad 1\le s\le e\le |T_i|.
$$

按原文 Definitions 2–4 等价改写，每条数据轨迹的 representative subtrajectory 为

$$
T_i^*=\arg\min_{T_i[s:e]\subseteq T_i}
\operatorname{Dist}\bigl(Q,T_i[s:e]\bigr),
$$

最终结果是在不同数据轨迹的代表片段之间取 top-k：

$$
R_k(Q)=\operatorname{TopK}_{T_i\in\mathcal D}
\left(T_i^*,\operatorname{Dist}(Q,T_i^*)\right).
$$

论文使用 DTW、EDR、ERP 等轨迹距离，以 GARI 图索引与 CNDI 邻域扩展近似搜索。

**与本问题相同的部分**：

- 查询是一条序列；
- 候选是数据轨迹的连续片段；
- 每条数据轨迹只贡献最佳片段；
- 在不同轨迹之间做全局 top-k，并返回片段边界。

**仍有差异的部分**：

- GTRSS 的点是空间坐标，距离主要衡量几何轨迹；没有 POI/text embedding 语义；
- timestamp 虽在数据模型中出现，实验距离通常省略，不含绝对时间窗、持续时间容差；
- 没有要求 episode 全部落入查询空间范围；
- 本问题只允许硬约束内的片段参与排名。

**直接后果**：episode 定位、轨迹级去重和 top-k 都已有直接前序。最强基线必须是“先做硬约束过滤，再执行 exact/approximate TRSSQ”，而不是 ACORN。

---

## 4. STKST-I：Semantic Trajectory Search by Keywords（ESWA 2023）

**证据级别：摘要级；正式 DOI/实验未核对。**

查询包含关键词、时间与位置；数据是带语义关键词、时间戳和位置的轨迹。摘要目标是返回与语义、时间相关且空间接近的 top-k 轨迹，没有给出足以复述的精确打分公式。

**与本问题的差异**：

- 关键词相关性，不是稠密 embedding 序列 DTW；
- 不要求连续 episode，不返回 episode 区间；
- 时间相关性不等于“绝对时间窗 + 持续时间”的硬可行性；
- 输出完整轨迹。

---

## 5. Towards Quantum Range Query for Spatial-Temporal-Semantic Trajectory Data

**证据级别：已核 arXiv:2609.29612v1 全文4页。**

论文将轨迹段记录写作

$$
r_i=(\tau_i,p_i,s_i,\mathrm{id}_i),
$$

其中 $\tau_i$、$p_i$、$s_i$ 分别表示 temporal、spatial、semantic 分量，再编码为多维 key $z_i$。给定轴对齐窗口 $W$，目标是找出所有 $z_i\in W$ 的记录，并输出相应量子叠加态。

**它确实解决的场景**：例如“列出 2018 年春天、Levent 空间框内的所有轨迹段”；这是轨迹+时间戳的混合范围搜索，不是 ACORN 式单向量过滤。

**与本问题的算子差异**：

- range membership，而不是语义距离 top-k；
- 返回所有匹配段，不按数据轨迹选最佳 episode；
- 不做语义序列对齐；
- Table 1 只报告 Time、Space、Space-time；Conclusion 把 semantic keys 扩展列为 future work。

因此它是同一数据域的重要邻近工作，但不能替代 GTRSS 作为 top-k episode 输出的直接对照。

---

## 6. 完整轨迹相似检索：REPOSE、N-tree、ST2Vec

| 工作 | 真实使用场景 | 算子与距离 | 为什么不是同一问题 |
|---|---|---|---|
| REPOSE（ICDE 2021） | 在海量车辆历史中找与一条完整路线最像的 k 条路线 | 分布式 top-k；支持 Hausdorff、Fréchet、DTW、LCSS、EDR、ERP | 返回完整轨迹；无 POI/text 语义和硬时间窗 |
| N-tree（ACM T-SAS 2025） | 精确找完整轨迹的 kNN 或距离半径结果 | metric index；DistanceAvg 支持连续时空轨迹 | 不在长轨迹中定位 episode |
| ST2Vec（KDD 2022） | 找空间路径与行驶时间节奏都相似的完整路网行程 | 学习加权空间+时间距离；top-k 为 case study | 时间是软相似度；无语义 POI/text 与硬可行性 |

这些工作说明“top-k 轨迹”“精确 metric index”“时间进入轨迹相似度”都不是空白。它们适合做整轨迹表示/索引基线或机制参考，但不能冒充 representative-subtrajectory 基线。

---

## 7. 边界：单对象过滤检索不参与直接比较

ACORN、Filtered-DiskANN、TANNS/TANGO、CubeGraph、LIST 都以单个向量或对象为检索单位。即使其谓词包含时间或空间字段，也没有“查询轨迹 → 每条数据轨迹的最佳连续子轨迹”语义。

因此：

- 不用这些工作证明本问题新颖；
- 不把它们列为问题级实验基线；
- 只在实现章节讨论 filter-aware graph traversal、时间分区和空间分区。

---

## 8. 直接问题对照矩阵

| 工作 | 算子 | 相似对象 | 语义 | 时间角色 | 硬时空约束 | 返回 |
|---|---|---|---|---|---|---|
| **本问题** | top-k exact/approx | 查询 episode vs 连续子轨迹 | 稠密向量序列 DTW | 相对时间进 score；绝对窗/时长为硬约束 | **时间窗+时长+空间** | **每轨迹最佳可行 episode** |
| **TRSSQ 2024** | learning-based filter + validate | 查询轨迹 vs 连续子轨迹 | 无 | 建模省略时间；无约束 | 无 | 每轨迹代表子轨迹 top-k |
| **GTRSS 2025** | approximate top-k | 查询轨迹 vs 连续子轨迹 | 无 | timestamp 非主 score | 无 | **每轨迹最佳子轨迹** |
| Quantum Range Query | exact range reporting | 窗口 vs 轨迹段记录 | 形式模型有；实验未实现 | 范围维度 | **时间+空间** | 全部匹配段/叠加态 |
| ETQ 2017 | top-k | exemplar points vs 完整轨迹 | TF-IDF 关键词 | 无查询时间窗 | 空间为点级软相似 | top-k 完整轨迹 |
| TISIS* 2024 | threshold | 完整 POI 轨迹 vs 完整轨迹 | Word2Vec 点匹配 + LCSS | 只保序 | 无 | 阈值内完整轨迹 |
| ST2Vec 2022 | learned similarity；top-k case | 完整轨迹 | 无 | 空间+时间软距离 | 无 | 完整轨迹 |
| N-tree 2025 | exact kNN/range | 完整轨迹 | 无 | DistanceAvg 时空 metric | 距离半径，不是属性窗 | 完整轨迹 |
| REPOSE 2021 | distributed top-k | 完整轨迹 | 无 | 依距离 | 无 | 完整轨迹 |
| STKST-I 2023 | top-k | 关键词/位置 vs 轨迹 | 关键词 | 时间相关性 | 未核为硬窗口 | 完整轨迹 |

**读表要点**：GTRSS/TRSSQ 是输出语义最近的工作；ETQ/TISIS* 提供语义匹配前序；Quantum Range Query 提供轨迹时空范围前序。没有一篇已核论文同时占满本问题一行，但这只能支持“组合尚未命中”，不能自动支持独立新问题。

---

## 9. 判定（供讨论）

- **不能主张**：首次轨迹检索、首次 top-k 轨迹、首次子轨迹/episode 定位、首次每轨迹一个代表片段、首次时间感知轨迹相似度、首次用学习模型估计代表子轨迹相似度。
- **候选增量**：在绝对时间窗、持续时间和空间范围的硬可行域内，用稠密语义序列距离选择 representative subtrajectory，并设计联合执行。
- **最危险审稿意见**：这是否只是 `hard filter → GTRSS/TRSSQ`，再把几何点距离换成 semantic embedding distance？
- **成立门槛**：证明硬约束改变候选生成、剪枝或复杂度；与同距离、同可行性、同召回定义的 filter-then-TRSSQ 比较。
- **下一步**：TRSSQ 已精读；优先复现其 filter-validate（RSSE + ExactS）作为 `hard filter → TRSSQ` 基线，并做 GTRSS、ETQ、TISIS*、STKST-I 引用闭包。若已有方法覆盖全部约束，则 pivot。
