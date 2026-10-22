# 问题定义对照：我们的问题 vs 现有论文

更新日期：2026-10-23。用途：让读者逐条判断新颖性。先给我们的定义，再按“最接近 → 较远”列出已有论文的问题定义。**证据级别**逐条标注；摘要级条目不是逐字定义，不能当作原文公式。

---

## 0. 我们的问题：时间窗约束的语义 Episode 检索（TW-SER）

### 0.1 数据

一条语义轨迹是带时间戳的停留点序列

$$
T=\bigl((p_1,t_1),\ldots,(p_n,t_n)\bigr),
\qquad t_1<\cdots<t_n,
$$

其中 $p_i=(\ell_i,\mathbf e_i)$，$\ell_i\in\mathbb R^2$ 是位置，$\mathbf e_i\in\mathbb R^d$ 是语义向量。episode 是连续子序列

$$
T[a:b]=\bigl((p_a,t_a),\ldots,(p_b,t_b)\bigr),
\qquad 1\le a\le b\le n.
$$

### 0.2 查询

$$
Q=\bigl(\mathbf q_1,\ldots,\mathbf q_m;\ \tau_1,\ldots,\tau_m;\ W;\ R;\ \delta_\Delta;\ k\bigr),
$$

其中 $\mathbf q_i\in\mathbb R^d$ 是查询停留点语义向量，$\tau_i$ 是相对时间偏移（$\tau_1=0$），$W=[w_s,w_e]$ 是绝对时间窗，$R$ 是空间范围，$\delta_\Delta\ge0$ 是持续时间容差，$k>0$。查询时长为 $\Delta_Q=\tau_m$。

### 0.3 可行 episode

$T[a:b]$ 关于 $Q$ 可行，当且仅当：

$$
w_s\le t_a,\qquad t_b\le w_e,
$$

$$
\bigl|(t_b-t_a)-\Delta_Q\bigr|\le\delta_\Delta,
$$

$$
\forall i\in[a,b]:\ \ell_i\in R,
$$

且 $T[a:b]$ 连续、不跳点。记可行集合为 $\mathcal E_Q(T)$。

### 0.4 分数与精确结果

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

$$
s_Q(T)=
\min_{T[a:b]\in\mathcal E_Q(T)}
\frac{D_{\mathrm{DTW}}(Q,T[a:b])}{\max(m,\,b-a+1)}.
$$

精确 top-k 为按 $(s_Q(T),\mathrm{id}(T))$ 字典序最小的 $k$ 条不同轨迹，并返回其最佳 episode 区间。若可行轨迹少于 $k$，返回全部；若为空，返回空集。

### 0.5 近似结果

近似结果 $\widehat R(Q)$ 必须满足：每条结果的 episode 硬可行、轨迹不重复、$|\widehat R(Q)|\le k$。近似只放宽距离排序，不放宽时间/空间/时长约束。召回率按轨迹集合计。

### 0.6 实现限制（不是形式定义的一部分）

当前 oracle 为控制枚举成本，额外限制 episode 长度 $b-a+1\in[m-2,m+2]$。若正式定义不允许该限制，需要重新生成 oracle。

---

## 1. ETQ：Top-k Exemplar Trajectory Queries（ICDE 2017）— 最接近

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

**为什么它最危险**：如果把关键词相似度换成 embedding 相似度，ETQ 的“查询点序列 → top-k 轨迹”框架与本问题的候选生成/排序很接近。因此我们的新颖性必须落在“时间可行性 + 连续 episode + episode 区间”，而不是“点序列查轨迹”。

---

## 2. TISIS / TISIS*：Trajectory Indexing for Similarity Search（BDA 2024）

**证据级别：已读 arXiv HTML 问题定义与算法 1–2；实验未逐项核对。**

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

## 3. SimSub 家族：相似子轨迹搜索（PVLDB 2023；ICDE 2024/2025）

**证据级别：摘要/相关工作报告级；ICDE 2024/2025 全文未读，定义按问题名与摘要概括。**

给定查询轨迹 $Q$ 和数据轨迹 $T$，找子轨迹

$$
T[i:j]=\langle p_i,\ldots,p_j\rangle
$$

使得

$$
T[i:j]=\arg\min_{i\le j} d\bigl(Q,T[i:j]\bigr),
$$

其中 $d$ 是几何轨迹距离（Fréchet、DTW、EDR 等）或学习距离。

变体：

- **单结果 SimSub**：返回最相似的一条子轨迹；
- **Top-k representative SimSub**（ICDE 2024）：返回 $k$ 条有代表性的相似子轨迹；
- **带约束/简化的 exact SimSub**（ICDE 2025）：在约束与轨迹简化下精确求解。

**与本问题的差异**：

- 子轨迹是几何连续片段，不是“语义向量序列 + 时间可行性”的 episode；
- 没有绝对时间窗、持续时间容差、空间范围硬约束；
- 距离是几何/学习距离，不是稠密语义 DTW；
- 输出通常是子轨迹，不要求“轨迹级 top-k + 最佳 episode 区间”；
- 不处理“同一轨迹多段候选、轨迹去重”的语义。

---

## 4. STKST-I：Semantic Trajectory Search by Keywords（ESWA 2023）

**证据级别：摘要级；正式 DOI/实验未核对。**

查询包含关键词、时间与位置；数据是带语义关键词、时间戳和位置的轨迹。目标：

$$
\text{返回 top-}k \text{ 条轨迹，使其}
$$

- 包含与查询语义、时间最相关的关键词；
- 空间上接近查询位置。

摘要只说明“semantically and temporally relevant keywords + close to query position”，没有给出可复述的精确打分公式。**不能把摘要概括当作原文公式。**

**与本问题的差异**：

- 关键词匹配/语义相关，不是稠密 embedding 序列 DTW；
- 不要求连续 episode，不返回 episode 区间；
- 时间语义是关键词的时间相关性，不是绝对时间窗 + 持续时间可行性；
- top-k 轨迹，不是“轨迹 + 最佳可行片段”。

---

## 5. TANNS：Timestamp / Interval ANN（TKDE 2026）

**证据级别：摘要与作者页书目；全文未获取。**

数据：

$$
\mathcal D=\{(x_i,I_i)\},
$$

其中 $x_i$ 是向量，$I_i$ 是有效期区间（时间戳查询可视为退化区间）。

TANNS：

$$
\operatorname{TANNS}(q,t)=
\operatorname{TopK}_{x_i:\ t\in I_i}\ \|q-x_i\|.
$$

IANNS：

$$
\operatorname{IANNS}(q,[t_s,t_e])=
\operatorname{TopK}_{x_i:\ I_i\cap[t_s,t_e]\ne\varnothing}\ \|q-x_i\|.
$$

**与本问题的差异**：

- 查询是单向量，不是序列；
- 时间只决定“是否有效”，不约束 episode 的起止/持续时间；
- 没有空间范围、序列对齐、episode 定位；
- 输出是向量，不是轨迹 + 区间。

---

## 6. TANGO / TDVS：Time-Decayed Vector Search（arXiv 2026）

**证据级别：摘要级。**

时间衰减向量搜索（TDVS）把语义相似度与时间新鲜度结合：

$$
\operatorname{score}(q,x,t)
=
\operatorname{combine}\bigl(\operatorname{sim}(q,x),\ \operatorname{decay}(t)\bigr),
$$

返回

$$
\operatorname{TopK}_{x\in\mathcal D}\ \operatorname{score}(q,x,t).
$$

摘要强调“连续时间衰减 + 语义相似度联合决定相关性”，并给出精确度量变换与图索引；具体 combine 公式未在摘要中展开，**不逐字引用**。

**与本问题的差异**：

- 单向量查询，不是序列；
- 时间是单调新鲜度，不是绝对时间窗/持续时间可行性；
- 无空间范围、无 episode 定位；
- 不处理序列顺序。

---

## 7. CubeGraph：Vector + Spatial Constraints（arXiv 2026）

**证据级别：摘要级。**

查询是向量 $q$ 加空间约束（区域/空间单元）：

$$
\operatorname{TopK}_{x_i:\ \mathrm{loc}(x_i)\in R}\ \|q-x_i\|.
$$

索引用层级网格划分空间，查询时拼接与 $R$ 相交的单元图。

**与本问题的差异**：

- 查询是单向量，不是序列；
- 只处理空间范围，不处理绝对时间窗/持续时间；
- 输出单对象，不是轨迹 + episode 区间；
- 不支持序列对齐。

---

## 8. LIST：Spatio-Textual Embedding ANN（VLDB J. 2025）

**证据级别：摘要/HTML 级；全文未精读。**

查询：

$$
Q=(q_{\mathrm{loc}}, q_{\mathrm{text}}),
$$

对象：

$$
o=(\mathrm{loc}, \mathrm{text}).
$$

打分同时考虑空间距离与文本相关性：

$$
\operatorname{score}(Q,o)
=
f\bigl(\operatorname{dist}(q_{\mathrm{loc}},\mathrm{loc}(o)),\
\operatorname{rel}(q_{\mathrm{text}},\mathrm{text}(o))\bigr),
$$

返回 top-k 对象。

**与本问题的差异**：

- 单对象查询，不是序列；
- 空间 + 文本，不是空间 + 时间窗 + 持续时间；
- 不返回轨迹/episode；
- 不做序列 DTW。

---

## 9. ACORN / Filtered ANN（PACMMOD 2024）

**证据级别：项目已有全文与复现笔记。**

查询：

$$
Q=(q,P),
$$

其中 $P$ 是任意结构化谓词（标签、范围、位图等）。返回

$$
\operatorname{TopK}_{x_i:\ P(x_i)=\mathrm{true}}\ \|q-x_i\|.
$$

**与本问题的差异**：

- 查询是单向量，谓词是对象级布尔条件；
- 谓词不能表达“连续 episode + 持续时间 + 序列顺序”；
- 输出单对象，不是轨迹 + 区间；
- 没有时间窗内的序列对齐。

---

## 10. 汇总矩阵

| 工作 | 查询 | 数据表示 | 时间硬约束 | 空间硬约束 | 输出 | 序列对齐 | episode 区间 |
|---|---|---|---|---|---|---|---|
| **本问题** | 语义 episode + 时间窗/时长/空间 | 稠密向量序列 | **绝对时间窗 + 持续时间** | **有** | **top-k 轨迹 + 最佳 episode 区间** | **DTW** | **有** |
| ETQ 2017 | 点序列 + 关键词 | 关键词 + 坐标 | 无 | 点级邻近 | top-k 轨迹 | 点级 max 聚合（可选顺序） | 否 |
| TISIS* 2024 | 完整轨迹 | POI embedding | 无 | 无 | 阈值集合 | LCSS | 否 |
| SimSub 2023–2025 | 查询轨迹 | 坐标/学习表示 | 无 | 几何 | 子轨迹 / top-k 子轨迹 | Fréchet/DTW/EDR | 几何区间 |
| STKST-I 2023 | 关键词+时间+位置 | 语义轨迹 | 关键词时间相关性 | 有 | top-k 轨迹 | 关键词匹配 | 否 |
| TANNS 2026 | 单向量+时间戳/区间 | 向量版本 | 有效期/区间 | 无 | top-k 向量 | 无 | 否 |
| TANGO 2026 | 单向量+时间 | 向量 | 时间衰减 | 无 | top-k 向量 | 无 | 否 |
| CubeGraph 2026 | 单向量+空间 | 向量+空间单元 | 无 | 有 | top-k 对象 | 无 | 否 |
| LIST 2025 | 单向量+文本+空间 | 向量+文本 | 无 | 有 | top-k 对象 | 无 | 否 |
| ACORN 2024 | 单向量+谓词 | 向量 | 任意谓词（可含时间） | 任意谓词（可含空间） | top-k 对象 | 无 | 否 |

**读表要点**：ACORN 的谓词可以“表达”时间/空间条件，但它不表达顺序、持续时间、连续 episode 和 episode 区间；ETQ 最接近“点序列 → top-k 轨迹”，但没有时间可行性；TISIS* 最接近“语义轨迹”，但不定位 episode。**目前没有一行同时覆盖本问题的全部列。**

---

## 11. 判定（供讨论）

- **可主张的候选增量**：绝对时间窗 + 持续时间容差 + 空间范围下的连续语义 episode top-k 检索，返回 episode 区间，DTW 排序。
- **不能主张**：首次提出轨迹检索、首次使用 ANN、首次做时间向量检索、首次做子轨迹检索。
- **最危险审稿意见**：ETQ 已经把“查询点序列 → top-k 轨迹”形式化；把关键词换成 embedding 是否足以构成新问题？必须用“时间可行性 + 连续 episode + 区间输出”回答。
- **下一步**：系统查重（SIGMOD/VLDB/ICDE/SIGSPATIAL/CIKM/WSDM/KDD 2017–2026 + 引用闭包），把每一篇候选填入第 10 节矩阵；若出现一行覆盖全部列，则 pivot。
