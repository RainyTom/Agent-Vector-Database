# 轨迹-语义 Episode 检索：形式定义（v4）

更新日期：2026-10-23。本文定义**数据对象**与**查询问题**。轨迹如何切分为语义段、段向量如何编码，属于预处理，不进入定义；只要求满足 §2.2 的契约。

## 1. 记号

| 记号 | 含义 |
|---|---|
| $n$ | 轨迹的原始采样点数 |
| $s$ | 语义段数，$1\le s\le n$ |
| $d$ | 语义向量维度 |
| $m$ | 查询的语义单元数 |
| $X(E)$ | episode $E$ 覆盖的原始坐标集合 |
| $\beta$ | 采样点 → 段编号的映射 |

## 2. 数据模型

### 2.1 轨迹

一条语义轨迹是四元组

$$
T=\bigl(X,\ \tau,\ E,\ \beta\bigr),
$$

其中

- $X=(x_1,\ldots,x_n)\in\mathbb R^{n\times 2}$：坐标序列；
- $\tau=(t_1,\ldots,t_n)$：时间戳，$t_1<\cdots<t_n$；
- $E=(\mathbf e_1,\ldots,\mathbf e_s)\in\mathbb R^{s\times d}$：**语义向量集**，每个向量对应一个轨迹段；
- $\beta:[1,n]\to[1,s]$：采样点到段的归属，满足 §2.2 的契约。

第 $k$ 段为

$$
I_k=\beta^{-1}(k)=[l_k,r_k],\qquad
X_k=\{x_i:i\in I_k\},\qquad
t^{s}_k=t_{l_k},\quad t^{e}_k=t_{r_k},\quad
\Delta_k=t^{e}_k-t^{s}_k .
$$

### 2.2 分段契约

分段算法不在本文定义内；但任何分段必须满足：

- **C1（连续）** $\beta$ 单调不减：每段是原始轨迹的连续区间。
- **C2（覆盖且非空）** $\beta$ 满射到 $[1,s]$：每段至少含一个采样点，全部采样点恰属于一段。
- **C3（查询无关）** 同一数据集上 $\beta$ 与 $E$ 固定，不随查询变化。
- **C4（编码固定）** $\mathbf e_k$ 由固定编码器产生；编码器与聚合规则不随查询变化。

C1–C2 保证“段区间 ↔ 语义序列”是一一对应；C3–C4 保证查询不能挑选分段或编码。

**两个退化情形**：

- $s=1$：整条轨迹一个向量，问题退化为“带时间约束的向量检索”；
- $s=n$：每个采样点一段，问题退化为逐点语义序列匹配。

二者都是本定义的特例，不需要单独定义。

### 2.3 段与原始点的关系

段 $k$ 覆盖原始点区间 $I_k$，其坐标集合与时长为 $X_k$、$\Delta_k$。episode 覆盖的原始点集合与时间区间由段区间直接给出（见 §4），因此空间约束与持续时间都作用在真实采样点上，不需要插值。

## 3. 查询

$$
Q=\bigl(\mathbf q_1,\ldots,\mathbf q_m;\ \tau_1,\ldots,\tau_m;\ W;\ R;\ \delta_\Delta;\ k\bigr),
$$

- $\mathbf q_i\in\mathbb R^d$：查询第 $i$ 个语义单元的向量；
- $0=\tau_1<\tau_2<\cdots<\tau_m$：相对时间偏移，查询时长 $\Delta_Q=\tau_m$；
- $W=[w_s,w_e]$：绝对时间窗，$w_s\le w_e$；
- $R$：空间范围（矩形、圆或城市边界）；
- $\delta_\Delta\ge0$：持续时间容差；
- $k\ge1$：返回轨迹数。

查询合法当且仅当 $m\ge1$、$\tau$ 严格递增、$w_s\le w_e$、$\delta_\Delta\ge0$、$k\ge1$。查询**不含坐标**，也不要求与候选共享段边界或 POI。

## 4. Episode 与可行性

episode 是连续段区间

$$
E_{u:v}=\bigl(\mathbf e_u,\ldots,\mathbf e_v\bigr)
\quad\text{对应原始点}\quad
X(E_{u:v})=\bigcup_{k=u}^{v}X_k,\qquad
[t^{s}_u,\ t^{e}_v].
$$

$E_{u:v}$ 关于 $Q$ 可行，当且仅当：

1. **时间窗**：$w_s\le t^{s}_u$ 且 $t^{e}_v\le w_e$；
2. **持续时间**：$\bigl\lvert (t^{e}_v-t^{s}_u)-\Delta_Q\bigr\rvert\le\delta_\Delta$；
3. **空间**：$X(E_{u:v})\subseteq R$（$R$ 为圆时按距离阈值判定）；
4. **段数**：$v-u+1\ge m$。

记 $\mathcal E_Q(T)=\{E_{u:v}: u\le v,\ E_{u:v}\text{ 可行}\}$。若为空，$T$ 不参与排序。

## 5. 对齐与分数

对齐是单调路径

$$
\pi=\bigl((i_1,j_1),\ldots,(i_L,j_L)\bigr),
$$

满足 $i_1=1$、$i_L=m$、$j_1=u$、$j_L=v$，每步为 $(0,1)$ 或 $(1,1)$，且每个查询下标 $i$ 至少出现在一个对中。$(0,1)$ 允许 episode 含额外段；不允许 $(1,0)$，因此查询点不能被跳过。

$$
D(Q,E_{u:v})
=
\min_{\pi}
\sum_{(i,j)\in\pi}
\Bigl(
\lVert \mathbf q_i-\mathbf e_j\rVert_2
+
\lambda\,\bigl\lvert \tau_i-(t^{s}_j-t^{s}_u)\bigr\rvert
\Bigr),
\qquad \lambda\ge0 .
$$

$$
D_{\mathrm{norm}}(Q,E_{u:v})=\frac{D(Q,E_{u:v})}{m}.
$$

分母固定为 $m$。若按路径长度或 $\max(m,v-u+1)$ 归一化，增加段会同时增大分母，可能出现“加段反而更低分”的病态。

## 6. 结果语义

轨迹分数

$$
s_Q(T)=
\begin{cases}
\min_{E\in\mathcal E_Q(T)} D_{\mathrm{norm}}(Q,E), & \mathcal E_Q(T)\neq\varnothing,\\
+\infty, & \text{otherwise}.
\end{cases}
$$

查询结果

$$
R_k(Q)=\operatorname{TopK}_{T\in\mathcal D}\ \bigl(s_Q(T),\operatorname{id}(T)\bigr),
$$

按字典序取最小的 $k$ 条**不同轨迹**。每条结果返回：

- 轨迹标识 $\operatorname{id}(T)$；
- 分数 $s_Q(T)$；
- 最佳 episode 的段区间 $[u,v]$ 与时间区间 $[t^{s}_u,t^{e}_v]$；
- 若同分，取 $u$ 最小的 episode。

边界情形：可行轨迹少于 $k$ 时返回全部；为空时返回空集；同一轨迹只出现一次；$+\infty$ 的轨迹不进入结果。

**近似结果** $\widehat R(Q)$ 只需满足：每条结果的 episode 可行、轨迹不重复、$\lvert\widehat R(Q)\rvert\le k$。近似只放宽分数排序，不放宽 §4 的可行性。

## 7. 复杂度

单条轨迹有 $O(s^2)$ 个候选段区间；每个区间的 DTW 为 $O(ms)$（路径长度 $O(m+s)$，每步 $O(d)$）。穷举为

$$
O\bigl(m\,s^3\,d\bigr)\ \text{每条轨迹},\qquad
O\bigl(N\,m\,s^3\,d\bigr)\ \text{全库}.
$$

因此实际系统必须有：时间窗/时长/空间预过滤、段级语义下界剪枝、以及候选预算控制。索引与剪枝属于系统设计，不改变上述问题定义。

## 8. 与前序工作的关系

| 工作 | 数据对象 | 语义单元 | 输出 | 与本定义的关系 |
|---|---|---|---|---|
| **TRSSQ** (ICDE 2024) | 点序列，无向量 | 无 | 每轨迹一个代表子轨迹，全局 top-k | 取 $s=n$、$d=0$、无硬约束的特例 |
| **GTRSS** (2025) | 同上 | 无 | 同上，图索引近似 | 同上的近似执行 |
| **TISIS\*** (BDA 2024) | POI 序列 | POI embedding | 完整轨迹，LCSS 阈值 | 只保留停留段、丢弃移动段、且不做 episode |
| **ETQ** (ICDE 2017) | 点 + 关键词 | 关键词 | 完整轨迹 top-k | 语义在点上，但不要求连续 |
| **ST2Vec** (KDD 2022) | 路网轨迹 | 无 | 完整轨迹距离 | 时间进入软相似度，非硬约束 |
| **本定义** | $(X,\tau,E,\beta)$ | 段级向量 | 每轨迹最佳 episode，全局 top-k | — |

## 9. 定义之外的事项

以下内容有意不写进定义，只作为实验协议：

1. **分段算法**：停留检测、语义变化点、固定分箱等；只要求满足 C1–C2，并在实验中固定、报告参数。
2. **段向量编码器与聚合**：POI embedding、文本编码器、段内均值或代表点。
3. **索引与剪枝**：段级 sketch、时间/空间分区、下界证明。
4. **近似参数**：候选预算、early abandon、图索引参数。

这样做的好处是：查询问题本身不依赖任何分割或编码实现；不同实现只要满足 §2.2 的契约，就在解同一个问题，结果可比。

## 10. 证据状态

- TRSSQ：13 页正式版全文已核；无时间/空间/语义约束，无端到端精确保证。
- GTRSS：arXiv v1 定义与框架已核。
- TISIS*：POI embedding 仅用于点对匹配，返回完整轨迹。
- ETQ：定义 1–7、算法 1–2 已读；实验未复现。
- ST2Vec：时空学习目标已核（完整轨迹）。
- 本定义中的 C1–C4、$D/m$ 与“禁止 $(1,0)$”是研究者设计，不是任何论文的结论。
