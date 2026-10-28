# HLG：层级血统连通图 —— 理论笔记（G0 门槛）

日期：2026-10-28。对象：单一目录约束下的向量 top-k（单查询 DSQ），单绑定目录树。
本笔记先于任何实现代码写成，作为 G0 门槛载体。凡未成立者写明反例并修正构造；凡属前序工作直接可得者写明"非贡献"。

## 0. 结论摘要

1. 计划中的原始构造**不成立**：局部点簇 $L(u)$ 内部无连通保证，存在叶子反例（§4）。修正为"局部簇内部生成树 + 上层簇代表点生成树"后，**连通性可证**（定理 1）。
2. 计划中的边数界 $|\text{连接边}| \le n-\#\{\text{非空节点}\}$ **是错的**（对 WIKI 实例给出 1,473,212，而实际构造为 1,941,678）。正确结论是 $|E_{\text{conn}}| = n-1$ **恒等成立**（定理 1(ii)）。
3. 维护界成立且可加强为"与 $|S(u)|$ 完全无关"（定理 2）；受影响的 MST 集合被限制在两条根路径上，$\le 2\,\text{depth}+2$ 个节点。
4. 连通性**不**蕴含跳数界与召回界（命题 3，含反例）；但可证一条弱但有价值的性质：scope 常驻入口 + 连通性 ⇒ **非空 scope 不会返回零结果**（命题 4）。
5. 本方案在"静态单绑定"情形下的连通性结论**可由 RNSG 的 structural heredity 直接得到**，不是本方案的贡献（引理 1）。

## 1. 定义

- 点集 $X$，$|X| = n$（WIKI：$n = 1{,}941{,}679$），维度 $d$。
- 目录树 $T = (V, E_T)$，根 $r = 0$，$|V| = m$（WIKI：$m = 468{,}031$，max depth $= 31$）。
- 单绑定 $h : X \to V$，$L(u) = h^{-1}(u)$ 为节点 $u$ 的直接绑定点集；$\mathrm{desc}(u)$ 为 $u$ 及其后代。
- **scope**：$S(u) = \bigcup_{a \in \mathrm{desc}(u)} L(a)$。假设 $S(r) = X$（WIKI 成立）。
- 基础图 $G_0 = (X, E_0)$：$E_0$ 为 HNSW 第 0 层无向边集（定向边按无向处理）。
- 度量：欧氏距离 $\lVert x-y\rVert$（WIKI 向量已归一化，$d=1024$）。所有距离用 $\mathtt{double}$ 计算，平局按全局 ID 升序。
- **定义 1a（局部簇代表点）**：对 $u$ 且 $L(u)\ne\varnothing$，$r_{\text{loc}}(u) = \arg\min_{x \in L(u)} \lVert x - \overline{L(u)}\rVert$，平局取全局 ID 最小者。
- **定义 1b（层级簇代表点，组合式）**：记候选集 $K(u) = \{r_{\text{loc}}(u)\ \text{if}\ L(u)\ne\varnothing\} \cup \{r_{\text{up}}(c) : c \in \mathrm{child}(u),\ S(c)\ne\varnothing\}$，
  $$r_{\text{up}}(u) = \arg\min_{x \in K(u)} \lVert x - \overline{S(u)}\rVert ,$$
  平局取全局 ID 最小者；$K(u)=\varnothing$ 时 $r_{\text{up}}(u) = \bot$。
- **为什么用组合式而不是"整簇 medoid"**（重要修正）：若 $r_{\text{up}}(u)$ 定义为 $\arg\min_{x\in S(u)}$，则 MOVE 后重算它需要扫描整个 $S(u)$，代价 $O(|S(u)|d)$；而 MOVE 会改变根路径上节点的 $S(\cdot)$，路径上节点的 $|S|$ 可达 $10^6$ 量级 ⇒ 定理 2 的"与 $|S(u)|$ 无关"立刻失效。组合式定义使重算只依赖 $u$ 的簇代表，$O(k_u d)$。
- 组合式定义**不削弱定理 1**：连通性证明只要求"每个簇选出的代表点落在该簇内"（簇内部由归纳假设连通，簇内任一点都能到达全簇），$r_{\text{up}}(u)\in K(u)\subseteq S(u)$ 恒成立。
- 建设期代价：$r_{\text{loc}}$ 全量 $O(nd)$，$r_{\text{up}}$ 全量 $O\!\left(\sum_u k_u d\right) = O(nd)$（WIKI：$\sum k_u = 831{,}497 < n$）。
- 三个符号约定：下文用 $r_{\text{up}}(c)$ 充当定理 1 中的 $\mathrm{rep}(S(c))$，用 $r_{\text{loc}}(u)$ 充当 $\mathrm{rep}(L(u))$。
- **定义 2（血统连通）**：图 $G = (X, E)$ 对 $T$ 血统连通，若对任意 $u \in V$，诱导子图 $G[S(u)]$ 连通。
- 记 $Z = \#\{u : L(u) \ne \varnothing\}$，$A = \#\{u : S(u) \ne \varnothing\}$。WIKI 实例：$Z = 363{,}467$（直接绑定非空节点数），$A = 468{,}031$（全部节点均有后代绑定）。

## 2. 引理 1（归约到 RNSG 区间族；**非贡献**）

**引理 1**：对 $T$ 做 DFS 先序（先访问 $u$、再依次访问各子树），令 $\pi : V \to \{0,\dots,m-1\}$ 为先序序号，$y$ 为按 $h(x)$ 的先序序号排序后的点序。则对任意 $u$，$S(u)$ 在 $y$ 中恰为一个连续区间。

*证明*：先序遍历下子树节点集合对应连续的先序号区间（标准性质），$S(u)$ 是子树节点绑定点的并，按 $y$ 排序后仍为连续区间。$\square$

**推论（非贡献声明）**：单绑定 + 静态树 + scope 为子树的查询族，在 DFS 先序下就是**区间族**。RNSG（Zou, Yin, Li, Qin, Dai, Wang, *RNSG: A Range-Aware Graph Index for Efficient Range-Filtered Approximate Nearest Neighbor Search*, arXiv:2603.12913v2；PVLDB 19 页面 https://www.vldb.org/pvldb/vol19/p2839-zou.pdf 已见，DOI/正式刊年未核实）证明其 RRNG 具有 **structural heredity**：任一 range-induced 子图仍是合法 RRNG，从而保持 **monotonic search-ability**。因此在**静态单绑定**场景，本方案的连通性（乃至更强的可搜索性）可由 RNSG 直接得到。**本方案不主张新的连通性定理范式**，可辩护增量只有两条：

- (a) 层级连接边的**共享构造**（每个节点只付 $k_u-1$ 条边，总量恒为 $n-1$）与其**维护代价界**；
- (b) 在真实语义目录树上实测"连接边是否足以恢复可达性与召回"。

附带说明：RNSG 的保证强于本文定理 1（含跳数/可搜索性），本文的 Step 3（`on-shortcut`）只是对"跳数瓶颈"做经验检验；与 RNSG 的**同设置对照尚未做**，属未完成项（见 §8）。

## 3. 反例 1（计划原构造不成立）

计划原构造：对每个内部节点 $u$，簇集合 $\mathcal C(u) = \{L(u) \text{ (若非空)}\} \cup \{S(c) : c \in \mathrm{child}(u)\}$，在**代表点**上建 MST，加 $|\mathcal C(u)|-1$ 条边。

**反例**：取叶子节点 $u$，$L(u) = \{x_1, x_2\}$，$E_0$ 中 $x_1, x_2$ 无任何路径（例如 HNSW 建图把它们分到两个不相连的区域）。此时 $\lvert\mathcal C(u)\rvert = 1$，构造不加任何边，而 $S(u) = L(u)$ 在原构造下不连通，血统连通性失败。

同类反例：任一 $|L(u)| \ge 2$ 的节点，原构造只用 $\mathrm{rep}(L(u))$ 一个点外接，$L(u)$ 内其余 $\lvert L(u)\rvert-1$ 个点**没有任何保证**。

**修正**：为每个 $|L(u)| \ge 2$ 的节点额外加入 $L(u)$ 上的欧氏 MST（"局部簇内部生成树"）。WIKI 上这部分为 $n - Z = 1{,}578{,}212$ 条边（$|L(u)|$ 中位数 2，p99 55），不可省略。

## 4. 定理 1（连通性、边数、度数）

**构造（HLG）**。令
$$
E_{\text{up}}(u) = \mathrm{MST}\Big(\{\mathrm{rep}(L(u))\ \text{if}\ L(u)\ne\varnothing\} \cup \{\mathrm{rep}(S(c)) : c \in \mathrm{child}(u),\ S(c) \ne \varnothing\}\Big),
$$
$\mathrm{MST}(\cdot)$ 取点集上的欧氏最小生成树（$k_u \le 1$ 时为 $\varnothing$）；
$$
E_{\text{loc}} = \bigcup_{u : |L(u)| \ge 2} \mathrm{MST}(L(u)), \qquad
E_{\text{conn}} = E_{\text{loc}} \cup \bigcup_{u \in V} E_{\text{up}}(u), \qquad
G = (X, E_0 \cup E_{\text{conn}}).
$$
其中
$$
k_u = \mathbb 1[L(u)\ne\varnothing] + \#\{c \in \mathrm{child}(u) : S(c) \ne \varnothing\}.
$$

**定理 1**：
(i) $G$ 对 $T$ 血统连通；
(ii) $|E_{\text{conn}}| = n-1$（**恒等**，只要 $S(r)\ne\varnothing$）；
(iii) 对任意点 $x$，其新增度数
$$\deg_{\text{conn}}(x) \le \big(|L(h(x))|-1\big) + \sum_{a \in \mathrm{Anc}(h(x)) \cup \{h(x)\}} k_a ,$$
其中只有 $\mathrm{rep}(S(a)) = x$ 的祖先项非零（$|\{a : \mathrm{rep}(S(a))=x\}| \le \mathrm{depth}(h(x))+1 \le 32$，等号仅在 $x$ 恰好是整条祖先链的代表点时成立）。

*证明* (i)：对树高归纳。归纳假设 $H(u)$：由 $E_0[S(u)] \cup \{e \in E_{\text{conn}} : e \subseteq S(u)\}$ 构成的图在 $S(u)$ 上连通。

- 基例（叶 $u$，$L(u)$ 任意）：$S(u) = L(u)$，$E_{\text{loc}}$ 含 $\mathrm{MST}(L(u))$，故连通。
- 归纳步：设 $u$ 的各子 $c$ 满足 $H(c)$。$S(u) = L(u) \cup \bigcup_c S(c)$ 是若干互不相交"部件"的并。每个非空部件内部连通：$S(c)$ 由 $H(c)$ 提供；$L(u)$ 由 $\mathrm{MST}(L(u))$ 提供。$E_{\text{up}}(u)$ 是在 $\mathcal C(u)$ 的代表点上的生成树，$|\mathcal C(u)| = k_u$，其每条边连接两个**属于不同部件**的代表点，且两个端点都在 $S(u)$ 内（$L(u)\subseteq S(u)$，$\mathrm{rep}(S(c)) \in S(c) \subseteq S(u)$）。生成树在 $k_u$ 个部件之间给出连通骨架，故 $H(u)$ 成立。
- $u=r$ 时 $S(r)=X$，即得结论。$\square$

**关键点**：定理只要求"生成树"，**不要求欧氏最小性**。任何连通生成树都成立（§6 的退化处理依赖这一点）；欧氏 MST 只是让边尽量短的经验选择。

*证明* (ii)：先数上层的边：
$$\sum_{u:k_u \ge 1}(k_u-1) = \sum_{u \in V} k_u - A = \big(Z + (A-1)\big) - A = Z-1,$$
其中 $\sum_u k_u = Z + (A-1)$ 因为"非空子节点"指标对每个非根非空节点恰好计一次；$k_u\ge 1 \iff S(u)\ne\varnothing$，共 $A$ 个。局部边：
$$\sum_{u:|L(u)|\ge 2}(|L(u)|-1) = \sum_{u: L(u)\ne\varnothing}(|L(u)|-1) = n - Z.$$
合计 $(Z-1)+(n-Z) = n-1$。

再证不重复：设边 $\{x,y\}$ 由 $E_{\text{up}}(a)$ 产生，则 $x,y$ 属于 $a$ 的**不同**簇，即 $h(x),h(y)$ 分属 $a$ 的不同子分支（或其一在 $L(a)$）；由树的分支唯一性，这样的 $a$ 唯一。若 $\{x,y\}$ 由 $E_{\text{loc}}(a)$ 产生，则 $h(x)=h(y)=a$，此时对任意祖先 $a'$ 二者同属一个分支簇、对后代二者都不在 scope 内，故 $E_{\text{up}}$ 不可能产生 $\{x,y\}$；而 $E_{\text{loc}}$ 只在 $a$ 处产生它。两条路径互斥，故总数为 $n-1$。$\square$

*证明* (iii)：$\deg_{\text{conn}}(x)$ = $\deg_{E_{\text{loc}}}(x) + \sum_a \deg_{E_{\text{up}}(a)}(x)$。第一项 $\le |L(h(x))|-1$；第二项：$x$ 只能在 $a$ 的簇代表点集合中出现，即 $a = h(x)$（此时 $x = \mathrm{rep}(L(h(x)))$）或 $a$ 是 $h(x)$ 的祖先且 $x=\mathrm{rep}(S(a))$；每个这样的 $a$ 上 $\deg_{E_{\text{up}}(a)}(x) \le k_a - 1$。$\square$

**WIKI 实例实测结构量**（本节数字来自 `wiki_tree.txt` / `wiki_bindings.txt` 直接统计，脚本见 Step 2；非推导）：

| 量 | 值 |
|---|---:|
| 节点数 $m$ / 绑定条目 $n$ | 468,031 / 1,941,679 |
| 非空节点 $A$ / 直接绑定点集非空 $Z$ | 468,031 / 363,467 |
| 最大深度 | 31 |
| 最大出度 | 408 |
| $k_u$ 最大 / 合计 / $\sum k_u^2$ | 408 / 831,497 / 7,294,109 |
| 上层边 $\sum(k_u-1)$ | 363,466 |
| 局部边 $\sum(|L(u)|-1)^+$ | 1,578,212 |
| **$|E_{\text{conn}}|$** | **1,941,678 $=n-1$** |
| 相对 HNSW M=16 基础边数（$16n\approx31.07$M）占比 | 6.25% |
| $|L(u)|$ 中位数 / p99 | 2 / 55 |
| $|S(u)|$ 分位 p50/p90/p99/p999 | 2 / 21 / 305 / 4202 |

$\sum k_u^2 = 7.3\times10^6$ 意味着逐节点朴素 Prim 全量建边约 $7.3\times10^6 \times d \approx 7.5$ GFLOP，代价可接受，因此**建边可用精确欧氏 MST**，无需退化。

**计划边数界已证伪**：$n - A = 1{,}473{,}212 \ne 1{,}941{,}678$。修正后为 $n-1$。

**度数界的实测值**（定理 1(iii) 要求给实测上界）见 `report.md` 的 Step 2 结构节（`max_added_degree` / `mean_added_degree`）；本笔记只给可证上界。

## 5. 定理 2（维护界）

记 $\mathrm{Anc}(x)$ 为 $x$ 的**真**祖先集合。MOVE$(u \to w)$：把 $u$ 从 $\mathrm{parent}(u)=p$ 摘下挂到 $w$（要求 $u \ne r$，$w \notin \mathrm{desc}(u)$，$u \ne w$）。绑定 $h$ **不变**（MOVE 改树，不改 $h$）。

**定理 2**：MOVE$(u\to w)$ 后需要重建的 $E_{\text{up}}$ 所在节点集合
$$
\mathcal A \subseteq \{p, w\} \cup \mathrm{Anc}(p) \cup \mathrm{Anc}(w),
$$
具体为 $\mathcal A = \{p,w\} \cup \{\mathrm{parent}(a) : a \in \Delta\}$，其中
$$
\Delta = \big(\{p,w\} \cup (\mathrm{Anc}(p) \,\triangle\, \mathrm{Anc}(w))\big)\setminus\{r\}.
$$
$E_{\text{loc}}$ 全部不变；$\mathrm{rep}(S(u))$ 不变。总代价
$$
O\Big(\sum_{a \in \mathcal A} k_a^2\, d\Big) \quad(\text{朴素 Prim};\ \text{用堆可到 } O(\textstyle\sum_a k_a \log k_a \cdot d)\ \text{若用近似生成树})，
$$
**与 $|S(u)|$、$|L(u)|$ 均无关**；被改写的连接边数 $\le \sum_{a\in\mathcal A} k_a$。

*证明*：$E_{\text{up}}(a)$ 只依赖 $\mathcal C(a)$ 中簇的**点集**：$\mathrm{rep}$ 是点集的函数（定义 1），而 $\mathcal C(a)$ 的成员由 $a$ 的子节点集合与 $L(a)$ 决定。因此 $E_{\text{up}}(a)$ 变化 $\iff$ (1) $a$ 的子集合变化（$a \in \{p,w\}$），或 (2) 某个 $c\in\mathrm{child}(a)$ 的 $S(c)$ 作为**点集**变化（则 $\mathrm{rep}$ 可能变），或 (3) $L(a)$ 变化（MOVE 不改 $h$，故不发生）。

$S(a)$ 作为点集变化 $\iff$ 子树(a) 的**节点集**变化 $\iff$ 移动跨越了 $a$ 的边界。子树集合在 MOVE 后：$\mathrm{sub}(w) \mathrel{+}= \mathrm{sub}(u)$，$\mathrm{sub}(p) \mathrel{-}= \mathrm{sub}(u)$，其余（含 $r$ 的公共祖先）不变——因为对同时是 $p$ 与 $w$ 祖先的 $a$，$S(u)$ 进出相互抵消，$S(a)$ 作为集合不变。故 $S(\cdot)$ 变化的节点集合为 $\{p,w\}\cup\Delta_0$，其中 $\Delta_0 = (\mathrm{Anc}(p)\triangle\mathrm{Anc}(w))\cup\{p,w\}$。这些节点作为**簇**出现的位置是其父节点，故 $E_{\text{up}}$ 受影响节点为 $\{\mathrm{parent}(a):a\in\Delta_0\}$（父为根时无父，剔除），并上 $\{p,w\}$ 自身（子集合变化）。即得 $\mathcal A$。

$\mathrm{rep}(S(u))$：$S(u)$ 点集不变（$\mathrm{sub}(u)$ 内部结构未变）$\Rightarrow$ 不变。$E_{\text{loc}}$ 只依赖 $L(\cdot)$，$h$ 不变 $\Rightarrow$ 全部不变。每节点 Prim 在 $k_a$ 个代表点上要算 $O(k_a^2)$ 个距离、每个 $d$ 维，故代价如上。$\square$

**为什么"组合式代表点"（定义 1b）是定理 2 的必要条件**：上式 $O(\sum k_a^2 d)$ 把"重算代表点"的代价当成 $O(k_a d)$。若 $r_{\text{up}}(a)$ 取整个 $S(a)$ 上的 medoid（$\arg\min_{x\in S(a)}$），重算要扫描 $S(a)$，代价 $O(|S(a)|d)$；而 $\mathcal A$ 中的节点可以携带 $|S(a)|\approx 10^6$（例如 $a$ 是根的某个大儿子）⇒ 维护代价重新变成"随被移动位置的规模增长"，定理 2 失效。组合式定义把 $r_{\text{up}}(a)$ 的重算限制在 $K(a)$ 的 $k_a$ 个候选点上。这条**是构造的设计约束，不是可选项**。

**MERGE$(u\to w)$ 的差异**（诚实标注）：$L(u)$ 的点迁移到 $w$，$u$ 的子节点重挂到 $w$，$u$ 被删除。此时 $S(a)$ 对 $a\in\mathrm{Anc}(p)\cup\mathrm{Anc}(w)$ 的**真变化**（不相抵消，因为 $L(u)$ 的点换了节点但仍在两条路径的共同祖先内——严格说共同祖先的 $S$ 也不变，但 $L(w)$ 变大、$L(u)$ 清空），并需重建 $E_{\text{loc}}(w)$（$O(|L(w)|\cdot d)$，与 $|L(u)|$ 有关，这是 MERGE 与 MOVE 的本质差别）。计划中"两条根路径"的说法对 MERGE 严格成立、对 MOVE 偏保守（MOVE 只需 $\mathcal A$）。

**退化边界**：若某节点扇出 $F$ 极大，$k_a\approx F$，$k_a^2 d$ 主导。此时用**任意**连通生成树替代欧氏 MST（定理 1 不依赖最小性），例如投影排序 + 窗口近邻候选 + 并查集 + 分量桥接，代价 $O(k_a d\log k_a)$。此为经验选择，不影响可证结论。

## 6. 命题 3（诚实边界：连通 ≠ 有界跳数 / 召回）

**命题 3**：血统连通性既不蕴含贪心束搜索的跳数界，也不蕴含召回界。

*反例（跳数/召回双失败，$k=1$，ef$=1$ 纯贪心）*：令 $m \ge 4$。取 $y_1,\dots,y_m \in \mathbb R^D$ 使 $\lVert y_i - q\rVert$ 沿链先减后增并有第二个更低的极小：$\lVert y_1-q\rVert > \lVert y_2-q\rVert > \dots > \lVert y_j-q\rVert < \lVert y_{j+1}-q\rVert < \dots < \lVert y_m - q\rVert$ 且 $\lVert y_m-q\rVert < \lVert y_j-q\rVert$（$j = \lfloor m/2\rfloor$；例如 $y_i$ 沿一条直线放置、$q$ 偏心于 $y_j$ 一侧，再在 $y_m$ 处放一个离 $q$ 更近的点，边为链边 $\{y_i,y_{i+1}\}$）。取叶子节点 $u$，$L(u)=\{y_1,\dots,y_m\}$，则 $G[S(u)]$ 是连通路径（由 $E_{\text{loc}}$ 保证），入口 $\mathrm{rep}(S(u)) \in S(u)$。从 $y_1$ 出发的 ef$=1$ 贪心在 $y_j$ 停留（两邻居都更远），**永不访问 $y_m$**，故 $\text{recall@1}=0$。连通性完好，召回为 0。

*推论*：定理 1 不能写成召回下界；HLG 的可测预测只能是：**"入口不在 scope 内"这一失败模式消失**，因此小 scope 召回应显著优于同 ef 的过滤 HNSW；若实测不是，则瓶颈不是连通性（按计划转 Step 3 与 `on-shortcut` 消融）。

## 7. 命题 4（可证的实际收益：非空 scope 不会返回零结果）

**命题 4**：设递归查询在 $u$ 上、$S(u)\ne\varnothing$，入口取 $\mathrm{rep}(S(u)) \in S(u)$，遍历只允许两端都在 $S(u)$ 内的边，束宽 $\mathrm{ef}\ge 1$，$k \ge 1$。则（a）返回结果集非空；（b）所有返回 ID 落在 $S(u)$ 内；（c）若 $\mathrm{ef}\ge |S(u)|$，返回的恰是 $S(u)$ 内精确 top-$k$。

*证明*：(a) 入口本身是候选且 $S(u)\ne\varnothing$；束搜索的终止条件需要"最佳未扩展候选劣于当前第 ef 名"，入口在任何时刻都不会被丢弃（它是唯一初始候选），故结果集至少含入口。(b) 由"两端在 scope 内"的遍历规则，访问点全部属于 $S(u)$。(c) $\mathrm{ef}\ge|S(u)|$ 时束不丢候选，配合定理 1 的连通性，访问集合 = $S(u)$。$\square$

这正是 TFANNS（Luo, Qiao, Zuo, Deng, ICDE 2025, pp. 3642–3654, doi:10.1109/ICDE65448.2025.00272）对 StitchedVamana 指出的"某标签无边导致零结果"的失败模式的**结构性消除**；已测事实（过滤 HNSW 在小 scope 上 recall 0.03–0.04）与该模式一致。注意：**命题 4 只覆盖可达性，不覆盖排序质量**，召回仍由 G2 实测决定。

## 8. G0 自检与未完成项

| 检查项 | 状态 |
|---|---|
| 定理 1 连通性证明成立（修正后构造） | 是（§4） |
| 定理 1 边数界成立 | 是，但**修正**为 $n-1$（计划的 $n-A$ 界证伪，反例数字给出） |
| 定理 1(iii) 度数上界 | 可证上界已给；WIKI 连接图实测新增度数 max 66、mean 2.0000 |
| 定理 2 维护界成立 | 是（§5），$\mathcal A \subseteq$ 两条根路径 |
| 反例记录（计划原构造） | 是（§3） |
| 引理 1 "非贡献"声明在文 | 是（§2） |
| 不声称召回下界 | 是（§6） |
| 代表点定义可维护（定理 2 前提） | 是（定义 1b 组合式；整簇 medoid 会使定理 2 失效，已写明） |

未完成/未核实：

- RNSG 与 UNIFY/SIG 的**本地全文未精读**（本地 `papers/papers.jsonl` 未收录这两篇；本节依据 arXiv 摘要与 vldb.org 页面）。UNIFY 的 SIG inclusivity 结论（PVLDB 18(4):1118–1130, doi:10.14778/3717755.3717770）按计划记录，尚未逐项核对原文写法。
- 未与 RNSG 做同设置实验对照（RNSG 要求属性有序 + RRNG 近似构造；本工具未实现）。这使"连接边是否优于直接上 RNSG"在本轮**无证据**。
- G3 已在全量 WIKI 的 1064 个有效 MOVE 上运行并通过预设门槛；G1/G2 尚未运行（用户停止实验后改由其执行命令并回传结果）。
