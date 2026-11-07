# NodeANN+：属性 DAG 上的局部图索引与共享预算搜索

> **覆盖式文档**：方案一旦变更，整篇重写，不保留历史版本（历史见 Git）。问题口径见 `problem_def.md`；当日实验数据见 `YYYY-MM-DD-exps/`；定稿后再写 `experiment.md`。

---

## 1. 问题与记号

令对象集合为 $O$，概念集合为 $C$，二者构成属性 DAG $G_C=(C,E_C)$，边 $A\to B\in E_C$ 表示 $B$ 是 $A$ 的子概念（允许多父）。每个对象有唯一直属概念
$$\mathrm{own}: O\to C .$$

对概念集合 $U\subseteq C$，记其**后代闭包**
$$\mathrm{Desc}^{+}(U)=\{\,c\in C : \exists u\in U,\; u\rightsquigarrow c\,\},$$
合格集定义为
$$E(U)=\{\,o\in O : \mathrm{own}(o)\in \mathrm{Desc}^{+}(U)\,\}.$$

给定查询向量 $q$（128 维）与整数 $k$，任务为
$$\mathrm{TopK}(q,U,k)=\arg\min_{S\subseteq E(U),\,|S|=\min(k,|E(U)|)}\sum_{o\in S} d(q, v(o)),\qquad d=\text{平方 }L_2 .$$

选择率（selectivity）记为 $s(U)=|E(U)|/|O|$。主实验 200 条查询的均值 $s=0.675\%$，平均 $|E(U)|=1{,}140$，范围 0.01%–19%。

**量化参照**：索引在 PQ$(m{=}16,k{=}256)$ 重建的 float32 上搜索，Ground Truth 在原始 FP32 上计算。重建空间的精确扫描在该工作负载上得到 recall 0.9132；它是重建距离排序的参照值，并非任意近似算法相对原始 GT 的严格上界。并列距离及两种排序之间的差异均可能使近似结果略高于该值。

---

## 2. 设计目标与非目标

**目标**

1. （G1）**结构跳过**：不在 $E(U)$ 中的概念及其对象**不访问、不计算距离**；这必须是构造性质，而不是查询期的逐点判断。
2. （G2）**单队列调度**：将合格概念的候选放入同一待展开队列；实际可达性及门户效果需要验证。
3. （G3）**概念入口**：每个概念从该概念的入口与代表点中选择距查询最近的点启动；它与 HNSW 上层下降不同，没有多层导航。
4. （G4）**预算可控**：为每个概念设置明确的候选容量，并允许满足全局阈值的候选继续扩展；不保证召回随预算单调。

**非目标（明确排除）**

- 物理层优化（SIMD、预取、连续排布的重排）不作为贡献；本文列出的三项实现优化仅用于对齐基线实现水平。
- 不支持"一个对象属于多个概念"；当前数据满足单 owner，多 owner 属于另一设定。
- 不做动态更新（插入/删除/概念 MOVE）的完整协议；仅在 §11 说明影响面。

**前提**

- （A1）实验包含 168,937 个对象和 1,929 个概念；非空概念图为 768 张。
- （A2）查询是概念集合的 OR；跨层嵌套由 $\mathrm{Desc}^+$ 吸收。
- （A3）**查询之间不共享任何状态**：除只读索引外，上一查询产生的缓存、访问标记、候选表都必须在下一查询前失效。这一条在本实现里由每查询 token 强制（见 §5.3）。

---

## 3. 索引结构

### 3.1 定义

对每个非空概念 $c$，建立

$$
\mathcal{I}(c)=\big\langle G_c,\; \mathrm{entry}(c),\; \mathrm{rep}(c),\; \mathrm{MST}(c),\; \mathrm{portal}(c)\big\rangle
$$

| 分量 | 定义 |
|---|---|
| $G_c=(V_c,E_c^{G})$ | $V_c=\{o:\mathrm{own}(o)=c\}$；$E_c^G$ 为**单层**有向邻接（对称化后近似无向），边只在 $V_c$ 内 |
| $\mathrm{entry}(c)\in V_c$ | 入口点，默认 $=$ 代表点之一 |
| $\mathrm{rep}(c)\subseteq V_c$，$|\mathrm{rep}|=\min(K,|V_c|)$ | farthest-first 选出的代表点，本实验 $K=8$ |
| $\mathrm{MST}(c)\subseteq \mathrm{rep}\times\mathrm{rep}$ | 代表点上的最小生成树，其边同时加入 $E_c^G$ |
| $\mathrm{portal}(c):V_c\rightharpoonup 2^{C}$ | 门户桥：把 $V_c$ 中少数节点映射到子概念 id |

门户桥在构造期建立：对每条 DAG 边 $c\to c'$，
$$
u^\*=\arg\min_{u\in \mathrm{rep}(c)\cup\{\mathrm{entry}(c)\}} d\big(v(\mathrm{entry}(c')),v(u)\big),\qquad
c'\in \mathrm{portal}(c)[u^\*],\quad c\in \mathrm{portal}(c')[\mathrm{entry}(c')].
$$

即：**父侧选距子入口最近的代表点**作为桥端点，子侧以入口回指父概念。

### 3.2 不变量

| 编号 | 内容 | 违反后果 |
|---|---|---|
| I1 | 一次查询只有一个待展开队列（递归模式）；"每概念 top-k 归并"只作对照基线 | 退化为 NodeANN，失去 G2 |
| I2 | 任一概念内搜索只从 $\mathrm{entry}(c)$ 与 $\mathrm{rep}(c)$ 出发 | 入口不确定，召回不可复现 |
| I3 | 结构跳过与谓词跳过分离：$c\notin D$ 的概念其对象**不进入**队列也不计算距离 | 距离浪费，且破坏 G1 |
| I4 | 查询间零共享：所有访问标记、入口缓存、候选表按查询重置 | 实验不可复现，测得的 QPS 偏高 |
| I5 | 计数永不低估：无法同步时标 dirty 并停止剪枝（本实现未启用剪枝，故恒满足） | 丢答案 |
| I6 | 门户桥只允许进入 $D$ 内的概念 | 越界访问，破坏 G1 |

---

## 4. 构造：Algorithm 1

```
Algorithm 1  BuildIndex(O, C, G_C; M, efC, K)
 1: for each c ∈ C with V_c ≠ ∅ do
 2:     G_c ← ∅ ;  insert V_c[0], V_c[1] with a mutual edge ;  entry(c) ← V_c[0]
 3:     for i = 2 .. |V_c|-1 do
 4:         C_i ← BeamInsert(V_c[i], G_c, efC)        ▹ 贪心 beam，候选上限 efC
 5:         N_i ← RNGSelect(C_i, M)                   ▹ 见式 (1)
 6:         E_c^G ← E_c^G ∪ {(V_c[i], u) : u ∈ N_i}
 7:         for u ∈ N_i do
 8:             E_c^G ← E_c^G ∪ {(u, V_c[i])}
 9:             if |Adj_c(u)| > M then Adj_c(u) ← RNGSelect(Adj_c(u), M)
10:     RepairConnectivity(G_c)                      ▹ 修补空邻接点，并以双向边连接遍历得到的组代表
11:     rep(c) ← FarthestFirst(V_c, K)
12:     for (a,b) ∈ Prim(rep(c)) do E_c^G ← E_c^G ∪ {(a,b),(b,a)}
13:     Flatten(G_c)                                 ▹ 邻接压成 offset + data
14: for each (c → c') ∈ E_C with V_c, V_c' ≠ ∅ do
15:     u* ← NearestIn(rep(c) ∪ {entry(c)}, entry(c'))
16:     portal(c)[u*] ← portal(c)[u*] ∪ {c'} ;  portal(c')[entry(c')] ← portal(c')[entry(c')] ∪ {c}
```

其中 RNG 选边规则（与 hnswlib 的 `getNeighborsByHeuristic2` 同族）：候选按到基点距离升序扫描，保留 $u$ 当且仅当
$$
 \forall w\in S_{\text{kept}}:\quad d(u,w) \ge d(u,\text{base}). \tag{1}
$$

**复杂度**：记单概念对象数为 $n_c$、维度为 $d$。构图 beam 的访问量取决于图结构，`efC` 不是距离调用数的严格上界。选边和反向 shrink 还涉及排序与候选间距离计算，因此不能把总成本简化为 $O(n_c\,efC\,d)$。空邻接修补最坏需 $O(n_c^2d)$；farthest-first 需 $O(Kn_cd)$，代表点 Prim 需 $O(K^2d)$。遍历分组和补边操作依赖实际邻接规模。构建时间和空间以运行记录为准。

---

## 5. 查询

记 $D=\mathrm{Desc}^{+}(U)\cap\{c:V_c\neq\varnothing\}$，$|D|$ 在主实验里平均 4.20。

公共子过程：

$$
\mathrm{EntrySel}(c,q)=\arg\min_{r\in \mathrm{rep}(c)\cup\{\mathrm{entry}(c)\}} d(q,v(r)). \tag{2}
$$

$\mathrm{EntrySel}$ 对代表点执行粗选，代价 $O(Kd)$，其质量与 HNSW 多层下降没有等价保证。每个查询内每个概念最多计算一次（按查询 token 缓存，I4）；当前实现先计算 entry，随后遍历包含 entry 的代表点，因此会重复计算一次入口距离。

### 5.1 非递归模式（local-only，对照）

```
Algorithm 2  Query-NR(q, U, k; ef, N)
 1: D ← Desc+(U) ∩ {c : V_c ≠ ∅}
 2: R ← ∅
 3: for c ∈ D do
 4:     s ← EntrySel(c, q)
 5:     B_c ← Beam(G_c, s, q, ef)          ▹ 只在 G_c 内扩展
 6:     R ← R ∪ TopN(B_c, N)
 7: return TopK(R, k)
```

该模式与 NodeANN 都按概念独立搜索后归并，但图构造和入口选择不同，不能视为实现等价。

### 5.2 递归模式（单队列 + 每概念预算 + 门户桥）

```
Algorithm 3  Query-R(q, U, k; ef)
 1: D ← Desc+(U) ∩ {c : V_c ≠ ∅}
 2: ef_c ← max(8, ⌊ef / |D|⌋)
 3: Q ← ∅ ;  H_c ← ∅ (∀c ∈ D) ;  W ← ∅ (全局最差 ef 名)
 4: for c ∈ D do  push(c, EntrySel(c,q), force = true)     ▹ 结构性播种，保证覆盖
 5: while Q ≠ ∅ do
 6:     (u,c) ← ExtractMin(Q)
 7:     if |H_c| ≥ ef_c ∧ d(u) > max H_c ∧
           not (overflow ∧ W ≠ ∅ ∧ d(u) ≤ max W) then continue
 8:     for v ∈ Adj_c(u) do push(c, v)
 9:     for c' ∈ portal(c)[u] with c' ∈ D do push(c', EntrySel(c',q))
10: return TopK( ⋃_{c∈D} H_c , k )

procedure push(c, v, force = false)
11:    if visited_c(v) = token then return
12:    visited_c(v) ← token ;  c_dists ← c_dists + 1
13:    update H_c with (d(q,v), v) capped at ef_c
14:    slack ← (overflow ∧ |W| = ef ∧ d(q,v) ≤ max W)
15:    if force ∨ kept ∨ slack then Q.insert(d(q,v), (c,v)) ; update W
```

三条与基线不同的语义：

1. **结构性播种**（第 4 行）：$D$ 内每个非空概念都播一次入口。实验中平均覆盖 4.20/4.20 个概念，见 `nodeann-plus-v4.jsonl`。
2. **预算按概念切分**：$ef_c$ 限制各概念结果堆容量，但不是展开次数或距离调用次数的硬上限。
3. **有限超额**：概念堆满后，符合全局阈值的候选仍可入队；全局堆只记录已接受入队的候选。

由于入口在搜索前均已访问，现有门户跳转再次调用同一目标入口会被 visited 检查拒绝。因此不能把该实现描述为门户提供了有效跨概念导航，门户贡献需独立消融验证。

### 5.3 查询间隔离（I4）

每个查询开始时对 $c$ 维度自增一个 `token`：

- 局部访问标记 `visited_c(v)` 用 `token` 判定，旧查询的标记自然失效；
- 入口缓存 `EntrySel` 用同一 `token` 判定；
- `H_c`、`Q`、`W` 在查询开始时清空。

因此**不存在跨查询复用**：同一查询重复执行两次，距离计数与结果完全一致（可在 `dists_per_q` 上验证）。

---

## 6. 正确性与近似分解

**命题 1（过滤正确性）** 在唯一 owner 假设下，合格集恰为 $\bigcup_{c\in D} V_c$。查询仅对 $D$ 中概念播种，扩展概念内部邻接，并只允许门户进入 $D$，因此返回对象均满足概念过滤。

**覆盖与召回**：为每个非空合格概念播种不意味着遍历其全部对象。构造时的邻接是有向的，遍历分组再加双向桥的过程并未证明任一入口可到达所有对象；有限 beam 和结果堆也可能丢失合格近邻。当前实现未证明完整可达性、随 ef 单调或收敛至精确排序。

近似来源包括图遍历、代表点入口、每概念候选容量及 PQ 重建误差。提高参数可能改善召回，但不保证逐查询单调。实测 NR 在 ef=50 得到 0.912719、ef=800 得到 0.913193；R 在 ef=800 得到 0.913430，高于重建空间精确扫描的 0.913193，不能据此认定后者是所有算法的上界。结果按实现中 GT ID 的微平均匹配计算。

---

## 7. 代价模型

令 $p_c$ 为概念 $c$ 中实际展开节点数、$A_c$ 为这些节点实际扫描的邻接槽位数。距离调用由入口选择和访问新节点共同构成，且入口点可能重复计算：
$$T_{\rm dist}=\sum_{c\in D}(1+|\mathrm{rep}(c)|)+N_{\rm first\text{-}visit}.$$
其中 $N_{\rm first\text{-}visit}$ 包括播种入口与图扩展中首次访问的节点。$A_c$ 和 $p_c$ 由运行时决定，不能以 $ef_c$ 作为展开次数上界；修补边与 MST 边也可能使节点出度超过 $M$。

| 方案 | 查询工作 | 备注 |
|---|---|---|
| pre-filter / 精确扫描 | 每个合格对象一次距离 | 主工作负载均值 1,140 |
| NodeANN | 每个合格概念独立图搜索并归并 | 具体访问量取决于图和预算 |
| ACORN-γ | 单图谓词遍历 | 依参数和选择率而变 |
| 本方案 | 入口距离 + 首次访问节点距离 + 邻接扫描 | 单候选队列，每概念结果堆 |

实测 R、ef=50 时平均 281 次距离调用；该数字包含入口选择和播种，不可直接将 $8|D|$ 相减当作已测图扩展次数。

---

## 8. 参数语义

| 参数 | 阶段 | 调大意味着 | 主要代价 | 本实验取值 |
|---|---|---|---|---|
| $M$ | 构造 | 图更连通、召回更稳 | 邻接内存、构建时间、每次展开扫描槽位 ↑ | 16 |
| $efC$ | 构造 | 建边候选更充分 | 构建时间 | 200 |
| $K$ | 构造 / 查询 | 候选入口增多 | 每概念至多 $K+1$ 次入口距离及 $O(K^2)$ MST | 8 |
| $ef$ | 查询 | 增大候选堆容量 | 访问和 QPS 变化须实测 | 20–800 |
| $ef_c$ | 查询 | 每概念结果堆容量增加 | 与 $|D|$ 耦合：$ef_c=\max(8,\lfloor ef/|D|\rfloor)$ | 下限 8，具体值随查询变化 |
| $N$ | 查询（NR） | 每概念保留更多 | 归并规模 | 50 |
| overflow | 查询 | 预算更软 | 队列变长 | 开 |

**$ef_c$ 的取舍**：$|D|$ 越大，单概念结果堆容量可能越小；这可能影响高召回端，实际原因需用消融和逐查询统计确认。

---

## 9. 实验

### 9.1 设置

168,937 对象 / 1,929 概念 / 200 查询 / k=50 / PQ 还原向量 / 标量平方 L2（`-O2 -fno-tree-vectorize -fno-tree-slp-vectorize`）/ 单线程 / 建索引时间不计入 QPS。

### 9.2 主结果（v4）

| 臂 | ef=20 | 50 | 100 | 200 | 400 | 800 |
|---|---|---|---|---|---|---|
| NR recall | 0.5708 | 0.9127 | 0.9131 | 0.9132 | 0.9132 | 0.9132 |
| NR QPS | 14,870 | 11,296 | 8,198 | 6,364 | 4,828 | 3,805 |
| R recall | 0.4729 | 0.8086 | 0.8818 | 0.9055 | 0.9109 | 0.9134 |
| R QPS | 18,828 | 16,671 | 12,943 | 10,568 | 8,199 | 6,244 |
| R 距离/查询 | 233 | 281 | 330 | 396 | 479 | 578 |

图：`2026-11-05-exps/figures/recursive-vs-baselines.*`、`nonrecursive-vs-baselines.*`；原始数据 `2026-11-05-exps/data/nodeann-plus-v4.jsonl`。

### 9.3 基线参照

NodeANN 在 recall 0.913193 时记录 11,062 QPS；NR 在 recall 0.912719 时记录 11,296 QPS。二者召回接近但不完全相同，且时序和重复次数不同，不能据此认定严格等召回优越。ACORN 扫描配置的最高记录为 0.549384；这是本次参数扫描范围内的结果，包含选择率回退的完整方法应另行比较，见 `baselines-frozen.md`。

### 9.4 不同实现版本的测量

以下数字来自不同实现版本的逐步修改，未必是单因素、同条件重复实验，不应作为独立机制的因果消融。对应记录见 `data/nodeann-plus-modes.csv` 与 `data/nodeann-plus-v4.jsonl`。

| 配置 | R recall | QPS |
|---|---:|---:|
| v2，ef=50 | 0.808622 | 13,859 |
| v4，ef=50 | 0.808622 | 16,671 |
| v2，ef=100 | 0.881810 | 10,825 |
| v4，ef=100 | 0.881810 | 12,943 |

---

## 10. 相关工作与定位

| 工作 | 机制 | 其假设 | 与本文的关系 |
|---|---|---|---|
| **[Filtered-DiskANN / StitchedVamana](https://doi.org/10.1145/3543507.3583552)**（WWW 2023，已发表） | 标签图拼接与剪边 | 标签过滤 | 本实现构建概念内局部图，反向邻接会 shrink，不能称为全程保边 |
| **[ACORN](https://arxiv.org/abs/2403.04871)**（PACMMOD 2(3), SIGMOD 2024） | 谓词感知邻域扩展 | 图搜索与过滤谓词 | 当前扫描中最高 recall 为 0.549384；完整方法的低选择率回退需另计 |
| **UNG**（Cai et al., 2024；发表状态未确认） | label-set 图及组间导航 | 谓词与分组方式不同 | 本实现的 MST 仅连接概念内部代表点，不能称为组间 navigating graph |
| **iRangeGraph**（SIGMOD 2025；正式书目信息待核） | 面向范围谓词的图共享 | 一维范围 | DAG 后代集未必能表示为单一连续区间 |
| **NodeANN / TrieHI**（项目基线族） | 每目录局部图与归并 | 项目中 TrieHI 适配使用 canonical tree | 本实验将概念候选放入单队列，仍维护每概念结果堆 |

相关工作比较限于已核查材料，尚未完成穷尽新颖性排查。当前实验没有隔离证明门户桥的作用。

---

## 11. 局限与失败模式

1. **高召回尾段**：R 在 recall 0.905495 时为 10,568 QPS；与 NodeANN 的比较依赖插值和测量条件，原因尚未由消融确定。
2. **图可达性**：当前修补步骤未证明有向可达性，不能据此保证所有合格对象可被访问。
3. **门户效果未证实**：所有概念入口已预先播种，当前门户访问会命中 visited；概念内 MST 也不是跨概念骨架。
4. **单 owner 假设**：多 owner 需要重新定义去重、过滤与索引组织，尚未实现。
5. **规模外推未验证**：所有结论限于 168,937 / 选择率均值 0.675%。1M 数据上 pre-filter 变贵（605 QPS），本方案是否仍占优需重测。
6. **动态性未覆盖**：概念 MOVE/合并会改变 $\mathrm{Desc}^+$，门户桥与 MST 需重算；影响面未量化。

---

## 12. 复现

```bash
# 构建（单线程构建，多线程的 FAISS 基线另行标注）
g++ -std=c++17 -O2 -fno-tree-vectorize -fno-tree-slp-vectorize \
    -o work/portal_bench codes/dag-ann-search/src/portal_bench.cpp

# 两种查询模式 + 参数扫描
./work/portal_bench --dir work/dag-ann --work work/dag-scope/workload-run1 \
    --vec recon.bin --k 50 --M 16 --efC 200 --reps 8 \
    --modes nr,r --efs 20,50,100,200,400,800 --queries 200

# 基线（冻结口径）
./work/nodeann_scalar --dir work/dag-ann --work work/dag-scope/workload-run1 \
    --mode nodeann --vec recon.bin --nosimd --k 50 --ef 50 --N 60 --queries 200 \
    --index-dir work/dag-ann/idx
./work/acorn_official --dir work/dag-ann --work work/dag-scope/workload-run1 \
    --vec recon.bin --mode containment --k 50 --M 32 --gammas 1 --mbetas 64 \
    --efs 10,50,200 --queries 200
```

产物：`2026-11-05-exps/data/*.jsonl|csv`、`2026-11-05-exps/figures/*.png|svg`、基线口径 `2026-11-05-exps/baselines-frozen.md`。
