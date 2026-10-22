# 时间窗约束的语义 Episode 检索：问题定义

更新日期：2026-10-22。本文定义第一轮研究问题。定义是研究者为实验建立的规范版本，不声称与任何单篇论文完全一致。

## 1. 数据模型

一条语义轨迹（semantic trajectory）是带时间戳的停留点序列

$$
T=\bigl((p_1,t_1),\ldots,(p_n,t_n)\bigr),
\qquad t_1<\cdots<t_n,
$$

其中每个停留点 $p_i=(\ell_i,\mathbf e_i)$ 包含二维位置 $\ell_i\in\mathbb R^2$ 和语义向量 $\mathbf e_i\in\mathbb R^d$。$d$ 由固定的 embedding 模型给出；本轮实验用 POI 类别共现与名称哈希构造可复现的 $d=128$ 向量，后续替换为文本/多模态模型。轨迹按用户或 session 组织，同一轨迹内时间严格递增；不同轨迹可以是同一用户的不同时段。

一个 episode 是轨迹的连续子序列

$$
T[a:b]=\bigl((p_a,t_a),\ldots,(p_b,t_b)\bigr),
\qquad 1\le a\le b\le n.
$$

其持续时间（duration）为 $\Delta(T[a:b])=t_b-t_a$，空间范围（footprint）为 $\{\ell_a,\ldots,\ell_b\}$ 的包围盒或最小覆盖圆。

## 2. 查询

查询为

$$
Q=\bigl(\mathbf q_1,\ldots,\mathbf q_m;\ \tau_1,\ldots,\tau_m;\ W;\ R;\ \delta_\Delta;\ k\bigr),
$$

其中：

- $\mathbf q_1,\ldots,\mathbf q_m$ 是查询停留点的语义向量，按时间顺序排列；
- $\tau_1<\cdots<\tau_m$ 是相对时间偏移，令 $\tau_1=0$，查询时长为 $\Delta_Q=\tau_m$；
- $W=[w_s,w_e]$ 是绝对时间窗；
- $R$ 是空间约束（半径、矩形或城市边界）；
- $\delta_\Delta\ge0$ 是持续时间容差；
- $k>0$ 是返回轨迹数。

查询不要求候选轨迹与查询共享 POI 或类别；相似性由 embedding 序列决定。

## 3. 可行 episode

给定轨迹 $T$ 和 episode $T[a:b]$，称其关于 $Q$ 可行，当且仅当：

1. **时间窗**：$w_s\le t_a$ 且 $t_b\le w_e$（episode 完全落在查询窗内；另一种“与窗口重叠”的语义需单独标注，不能混用）；
2. **持续时间**：$|\Delta(T[a:b])-\Delta_Q|\le\delta_\Delta$；
3. **空间**：$T[a:b]$ 的所有点落在 $R$ 内（或到 $R$ 的距离不超过给定半径）；
4. **顺序**：点按时间递增，episode 连续，不允许跳过中间点。

记 $T$ 的可行 episode 集合为 $\mathcal E_Q(T)$。若 $\mathcal E_Q(T)=\varnothing$，该轨迹不参与排序。

## 4. 序列距离

对长度分别为 $m$ 和 $b-a+1$ 的两条向量序列，使用带时间惩罚的动态时间规整（DTW）：

$$
D_{\mathrm{DTW}}(Q,T[a:b])
=
\min_{\pi\in\Pi}
\sum_{(i,j)\in\pi}
\Bigl(
\|\mathbf q_i-\mathbf e_j\|_2
+
\lambda\,\bigl|(\tau_i-\tau_1)-(t_j-t_a)\bigr|
\Bigr),
$$

其中 $\Pi$ 是满足端点对齐、单调、步长为 $(1,0),(0,1),(1,1)$ 的规整路径集合，$\lambda\ge0$ 是时间惩罚系数。$\lambda=0$ 时退化为纯语义 DTW；$\lambda>0$ 时要求时间轮廓也相近。

为使不同长度的分数可比，报告两种归一化：

$$
D_{\mathrm{avg}}=\frac{D_{\mathrm{DTW}}}{|\pi|},
\qquad
D_{\mathrm{norm}}=\frac{D_{\mathrm{DTW}}}{\max(m,|T[a:b]|)}.
$$

主实验固定一种并预注册；不能在不同方法间混用。

备选相似度：带 embedding 匹配阈值的 LCSS（TISIS* 风格）。LCSS 返回匹配长度而非距离，适合阈值查询，但难以做 top-k 连续排序；本轮以 DTW 为主，LCSS 作为对照。

## 5. 轨迹分数与精确结果

轨迹 $T$ 的分数为其最佳可行 episode 的距离：

$$
s_Q(T)=
\begin{cases}
\min_{T[a:b]\in\mathcal E_Q(T)} D_{\mathrm{norm}}(Q,T[a:b]), & \mathcal E_Q(T)\ne\varnothing,\\
+\infty, & \text{otherwise}.
\end{cases}
$$

精确 top-k 结果为按 $(s_Q(T),\mathrm{id}(T))$ 字典序最小的 $k$ 条不同轨迹，且每条结果附带其最佳 episode 的时间区间 $[t_a,t_b]$。若可行轨迹少于 $k$，返回全部；若为空，返回空集。同一轨迹只出现一次；若数据组织为用户，则同一用户的不同轨迹视为不同候选，需在实验中明确。

## 6. 近似结果与指标

近似结果 $\widehat R(Q)$ 必须满足：

- 每条结果的 episode 可行（时间、空间、持续时间约束不放宽）；
- 结果轨迹互不重复；
- $|\widehat R(Q)|\le k$。

Recall@k 以轨迹集合计：

$$
\operatorname{Recall@k}(Q)=
\frac{|\widehat R(Q)\cap R^*(Q)|}{|R^*(Q)|},
$$

其中 $R^*(Q)$ 是精确 top-k 的轨迹集合。若 $|R^*(Q)|<k$，分母用 $|R^*(Q)|$，不把欠填算作召回。另记录：

- **episode recall**：返回的 episode 区间与 oracle 最佳区间的时间 IoU 或起止点误差；
- **可行违规数**：返回结果中不满足硬约束的条数，必须为 0；
- **候选扫描数**、**距离计算次数**、**P50/P95 延迟**、**索引字节**、**构建时间**。

## 7. 与相邻问题的区别

- **不是整轨迹 kNN**：整轨迹表示把一条长轨迹压成一个向量，无法定位匹配发生在哪一段时间，也无法返回 episode 区间。
- **不是 TANNS/时间过滤 ANN**：那些方法检索单个向量，时间只决定有效性；本问题检索序列，时间窗约束 episode 的起止。
- **不是子轨迹几何搜索**：几何子轨迹搜索比较坐标轨迹（Fréchet/DTW/编辑距离），本问题比较语义向量序列，并要求绝对时间窗与空间范围。
- **不是 vector join**：本问题是单查询 top-k，不枚举 pair，也不要求全局 pair recall。
- **不是语义谓词判定**：embedding 距离是数值相似度，不涉及 LLM 判定“是否同一事件”。

## 8. 待固定的实验选择

1. 时间窗语义：完全包含 vs 重叠；主实验用完全包含，重叠作为敏感性分析。
2. 持续时间容差：绝对（如 ±2h）或相对（如 ±25%）；主实验用相对。
3. 空间约束：查询 footprint 的缓冲区半径；主实验用 2 km，另测无空间约束。
4. DTW 归一化与 $\lambda$；主实验 $\lambda=0$，另测 $\lambda>0$。
5. 轨迹粒度：用户级长轨迹 vs 单次 trail；主实验用户级，trail 级作为对照。
6. 查询采样：按时间窗分层，避免只选到高密度日期；固定随机种子。
