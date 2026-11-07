# ASI：属性骨架索引——面向层级属性 DAG 的近邻检索

> 覆盖式维护。问题口径见 `problem_def.md`；实验数据见 `2026-11-05-exps/`。
> 术语：**属性**（attribute）= 层级分类体系中的节点；"子树"= 该属性的后代闭包。

---

## 1. 问题与动机

### 1.1 场景

数据集 = 一个层级属性 DAG + 一批带向量的对象。每个对象挂在**唯一属性**上。查询给定向向量 $q$ 与属性集合 $U$，返回 **$U$ 后代闭包内**与 $q$ 最近的 $k$ 个对象。

与常规 ANN 的差别：候选集由**结构**（属性层级）确定，每次查询都不同，且通常只占全库极小部分（本数据 0.675%）。

### 1.2 三条朴素路线的问题

```svg
<svg viewBox="0 0 900 260" width="100%" font-family="sans-serif" font-size="12">
  <text x="16" y="18" fill="var(--muted)">低选择率下的成本结构</text>

  <rect x="16" y="34" width="270" height="200" rx="8" fill="none" stroke="var(--border)"/>
  <text x="30" y="56" fill="var(--fg)">A. 全库一张图 + 谓词过滤</text>
  <g stroke="var(--muted)" fill="none">
    <circle cx="60" cy="110" r="3"/><circle cx="100" cy="94" r="3"/><circle cx="140" cy="116" r="3"/>
    <circle cx="86" cy="146" r="3"/><circle cx="132" cy="160" r="3"/><circle cx="176" cy="134" r="3"/>
    <line x1="60" y1="110" x2="100" y2="94"/><line x1="100" y1="94" x2="140" y2="116"/>
    <line x1="86" y1="146" x2="132" y2="160"/><line x1="132" y1="160" x2="176" y2="134"/>
  </g>
  <text x="30" y="192" fill="var(--muted)">谓词把图切成互不相通的分块，</text>
  <text x="30" y="208" fill="var(--muted)">搜索在"不可达"里消耗预算：</text>
  <text x="30" y="226" fill="var(--danger)">ef 从 50 加到 800，召回不变</text>

  <rect x="304" y="34" width="270" height="200" rx="8" fill="none" stroke="var(--border)"/>
  <text x="318" y="56" fill="var(--fg)">B. 每属性一张图各查各的</text>
  <g stroke="var(--muted)" fill="none">
    <circle cx="336" cy="104" r="3"/><circle cx="362" cy="92" r="3"/><circle cx="388" cy="112" r="3"/>
    <line x1="336" y1="104" x2="362" y2="92"/><line x1="362" y1="92" x2="388" y2="112"/>
    <circle cx="428" cy="104" r="3"/><circle cx="454" cy="92" r="3"/><circle cx="480" cy="112" r="3"/>
    <line x1="428" y1="104" x2="454" y2="92"/><line x1="454" y1="92" x2="480" y2="112"/>
  </g>
  <text x="318" y="196" fill="var(--muted)">结构上跳过了无关属性，召回能到上限；</text>
  <text x="318" y="212" fill="var(--muted)">但每张图都各自从入口重爬、各自搜满 ef：</text>
  <text x="318" y="228" fill="var(--danger)">1,448 次距离/查询（比全扫的 1,140 还多）</text>

  <rect x="592" y="34" width="292" height="200" rx="8" fill="none" stroke="var(--accent)"/>
  <text x="606" y="56" fill="var(--fg)">C. 本文：合并成一体的骨架图</text>
  <g stroke="var(--accent)" fill="none">
    <line x1="700" y1="80" x2="660" y2="112"/><line x1="700" y1="80" x2="740" y2="112"/>
    <line x1="660" y1="112" x2="636" y2="150"/><line x1="660" y1="112" x2="690" y2="150"/>
    <line x1="740" y1="112" x2="770" y2="150"/>
  </g>
  <g fill="var(--accent)">
    <circle cx="700" cy="80" r="5"/><circle cx="660" cy="112" r="5"/><circle cx="740" cy="112" r="5"/>
    <circle cx="636" cy="150" r="5"/><circle cx="690" cy="150" r="5"/><circle cx="770" cy="150" r="5"/>
  </g>
  <text x="606" y="196" fill="var(--muted)">合并时用上层节点更新下层代表点的邻居，</text>
  <text x="606" y="212" fill="var(--muted)">整棵子树成为一张可导航的图；</text>
  <text x="606" y="228" fill="var(--success)">272 次距离/查询（0.77 召回）</text>
</svg>
```

---

## 2. 预备

| 符号 | 含义 |
|---|---|
| $C$ | 属性集合，$|C|=1{,}929$ |
| $G=(C,E_p)$ | 属性 DAG，$(p,c)\in E_p$ 表示 $c$ 是 $p$ 的子属性 |
| $\mathrm{Desc}^{+}(U)$ | $U$ 的后代闭包 |
| $O$ | 对象集合，$|O|=168{,}937$；$own:O\to C$ 单值归属 |
| $E(U)$ | 合格集 $\{o: own(o)\in \mathrm{Desc}^{+}(U)\}$ |
| $k$ | 返回条数，50 |

**度量**：平方 L2；搜索在 PQ 还原向量上，召回上限 **0.9132**（量化误差）。

---

## 3. 索引：ASI（Attribute-Skeleton Index）

```svg
<svg viewBox="0 0 900 400" width="100%" font-family="sans-serif" font-size="12">
  <text x="16" y="18" fill="var(--muted)">ASI 三层：L1 属性内局部图　L2 骨架（合并）　L3 闭包位图</text>

  <rect x="16" y="34" width="420" height="150" rx="8" fill="none" stroke="var(--border)"/>
  <text x="30" y="54" fill="var(--fg)">L1　属性内局部图 ALG</text>
  <g stroke="var(--muted)" fill="none">
    <circle cx="70" cy="100" r="3"/><circle cx="104" cy="86" r="3"/><circle cx="138" cy="104" r="3"/>
    <circle cx="96" cy="136" r="3"/><circle cx="140" cy="150" r="3"/><circle cx="180" cy="120" r="3"/>
    <line x1="70" y1="100" x2="104" y2="86"/><line x1="104" y1="86" x2="138" y2="104"/>
    <line x1="96" y1="136" x2="140" y2="150"/><line x1="140" y1="150" x2="180" y2="120"/>
    <line x1="70" y1="100" x2="96" y2="136"/><line x1="138" y1="104" x2="180" y2="120"/>
  </g>
  <text x="228" y="98" fill="var(--muted)">每属性一张单层图</text>
  <text x="228" y="116" fill="var(--muted)">RNG 选边 + 反向边收缩</text>
  <text x="228" y="134" fill="var(--muted)">孤立点补边、分量互连</text>
  <text x="30" y="172" fill="var(--muted)">作用：同属性对象在图上互为近邻</text>

  <rect x="452" y="34" width="432" height="150" rx="8" fill="none" stroke="var(--accent)"/>
  <text x="466" y="54" fill="var(--fg)">L2　代表点骨架 RS（合并产物）</text>
  <g stroke="var(--accent)" fill="none" stroke-width="2">
    <line x1="500" y1="110" x2="540" y2="92"/><line x1="540" y1="92" x2="580" y2="118"/>
    <line x1="500" y1="110" x2="520" y2="146"/>
    <line x1="580" y1="118" x2="620" y2="100" stroke-dasharray="5 3"/>
  </g>
  <g fill="var(--accent)"><circle cx="500" cy="110" r="6"/><circle cx="540" cy="92" r="6"/><circle cx="580" cy="118" r="6"/><circle cx="520" cy="146" r="6"/><circle cx="620" cy="100" r="6"/></g>
  <text x="644" y="98" fill="var(--muted)">farthest-first 选代表点</text>
  <text x="644" y="116" fill="var(--muted)">概念内 MST</text>
  <text x="466" y="172" fill="var(--muted)">作用：多点接入 + 按拓扑序合并，使子树成为连通图</text>

  <rect x="16" y="200" width="868" height="180" rx="8" fill="none" stroke="var(--border)"/>
  <text x="30" y="220" fill="var(--fg)">L3　闭包位图 CL（1,929 位 = 31 个 64 位字，共 0.46 MB）</text>
  <g font-family="monospace" font-size="10">
    <rect x="30" y="232" width="360" height="24" fill="var(--surface)" stroke="var(--border)"/><text x="36" y="249" fill="var(--fg)">desc(A) : 1111 1111 0000 0000 ...</text>
    <rect x="30" y="262" width="360" height="24" fill="var(--surface)" stroke="var(--border)"/><text x="36" y="279" fill="var(--fg)">desc(B) : 0000 0110 0000 0000 ...</text>
  </g>
  <text x="410" y="248" fill="var(--muted)">闭包：D = ⋁ desc(u)，u ∈ U</text>
  <text x="410" y="270" fill="var(--muted)">资格：对象所属属性的位是否置位</text>
  <text x="410" y="292" fill="var(--muted)">越界中断：位未置位的节点不展开邻居</text>
  <text x="30" y="322" fill="var(--muted)">必须覆盖全部属性（含无对象的中间属性），否则闭包传播断链</text>
  <text x="30" y="348" fill="var(--muted)">用途：闭包求解、资格判定、展开中断 —— 三者共用同一张位图</text>
</svg>
```

### 3.1 L1：属性内局部图（ALG）

对属性 $a$ 的直属对象集合 $S_a$（$n_a$ 个）建单层图，度上界 $M=16$：

**算法 1　构造属性内局部图**
```
输入：S_a, M, efC
1  初始化：取一个入口 e
2  for i = 2 .. n_a-1:
3      C ← 从 e 出发的 beam 搜索（预算 efC）       // 找到近邻候选
4      按 d(·, i) 升序扫描 C，按 RNG 规则选出 ≤ M 条
5      双向加入 i 与所选邻居
6      若邻居表超 M：对该表重跑 RNG 收缩            // 反向边不破坏度上界
7  孤立点补最近邻；连通分量之间用代表点互连
```

**RNG 规则**：保留边 $(i,u)$ 当且仅当不存在已选 $w$ 使 $d(u,w)<d(u,q)$。它保证邻域多样，是图可导航性的来源。

**为什么单层**：$n_a$ 中位数 66，多层结构的上层只有几个节点；实测召回与多层无差别（0.9101 vs 0.9132），但参数与实现更简单。

### 3.2 L2：代表点与**拓扑序合并**

这一层不是"额外加一类边"，而是**构造过程本身**。

**(a) 代表点**：farthest-first，数量 $K_a=\min\{K_{\max},\lceil n_a/p\rceil\}$，默认 $p=50,K_{\max}=8$：

$$r_1=e_a,\qquad r_{i+1}=\arg\max_{x\in S_a}\min_{j\le i} d(x,r_j)$$

每次挑"离已选代表最远"的点，使代表覆盖属性内各方向。查询时取距 $q$ 最近者为该属性的接入点——等价于 HNSW 用上层图选下降起点，代价仅 $K_a$ 次距离。

**(b) 属性内 MST**：在代表点上建最小生成树，边并入 L1，使任意两个代表之间只需几跳。

**(c) 合并：用上层节点更新下层代表点的邻居**

按**属性 DAG 的逆拓扑序**（先处理深属性，再处理其祖先）逐个合并：

**算法 2　拓扑序合并**
```
输入：属性 DAG 的逆拓扑序 topo、各属性代表点 reps[a]、度上界 Rdeg
1  for c ∈ topo:                      // 从叶子往上
2      for p ∈ parents(c):
3          for rp ∈ reps[p], rc ∈ reps[c]:
4              gbr[rp].push(rc); gbr[rc].push(rp)      // 双向连接
5          对 rp 的邻居表做 RNG 调整到 ≤ Rdeg
6          对 rc 的邻居表做 RNG 调整到 ≤ Rdeg          // 下层代表点被更新，不冻结
```

关键在于**第 6 行**：合并时下层（子属性）的代表点**不是建完就不动了**，它每次被上层合并时都要重新更新邻居并做 RNG 调整。这样每一次合并都在既有子图上叠加一层可达性，最终整棵子树形成一张连通图，而不是若干被"桥"勉强串起来的孤岛。

合并产生的跨属性边独立存放（不计入 L1 的度），度上界 $Rdeg=16$。实测：**12,722 条跨属性代表点边**。

```svg
<svg viewBox="0 0 900 250" width="100%" font-family="sans-serif" font-size="12">
  <text x="16" y="18" fill="var(--muted)">按拓扑序自底向上合并：每合并一层，双方的邻居表都被更新</text>

  <g stroke="var(--muted)" stroke-width="1.3" fill="none">
    <line x1="110" y1="64" x2="110" y2="104"/><line x1="110" y1="124" x2="110" y2="164"/>
  </g>
  <g font-size="11" text-anchor="middle">
    <rect x="76" y="44" width="68" height="20" rx="4" fill="var(--surface)" stroke="var(--border)"/><text x="110" y="58" fill="var(--fg)">父 A</text>
    <rect x="76" y="104" width="68" height="20" rx="4" fill="var(--surface)" stroke="var(--border)"/><text x="110" y="118" fill="var(--fg)">子 B</text>
    <rect x="76" y="164" width="68" height="20" rx="4" fill="var(--surface)" stroke="var(--border)"/><text x="110" y="178" fill="var(--fg)">孙 C</text>
  </g>

  <text x="180" y="80" fill="var(--muted)" font-size="11">第 1 步：合并 C 进 B —— B、C 的代表点互连并 RNG 调整</text>
  <text x="180" y="140" fill="var(--muted)" font-size="11">第 2 步：合并 B 进 A —— A、B 的代表点互连并 RNG 调整</text>
  <text x="180" y="200" fill="var(--fg)" font-size="11">结果：C 的代表点在两次合并中都被更新过，</text>
  <text x="180" y="218" fill="var(--muted)" font-size="11">因此从 A 的代表点出发可以经 B 抵达 C，且每一跳的邻居都是"近的"</text>

  <text x="620" y="80" fill="var(--muted)" font-size="11">实测：12,722 条</text>
  <text x="620" y="98" fill="var(--muted)" font-size="11">跨属性代表点边</text>
  <text x="620" y="126" fill="var(--danger)" font-size="11">去掉合并边：</text>
  <text x="620" y="144" fill="var(--muted)" font-size="11">只能进入 0.91/4.20 个属性，</text>
  <text x="620" y="162" fill="var(--muted)" font-size="11">召回 0.638</text>
</svg>
```

### 3.3 L3：闭包位图（CL）

对每个属性 $c$ 建 $\mathrm{desc}(c)$，按逆拓扑序自底向上求：

$$\mathrm{desc}(c)=\{c\}\cup\bigcup_{c'\in children(c)}\mathrm{desc}(c')$$

查询期 $D=\bigvee_{u\in U}\mathrm{desc}(u)$，用于三件事：枚举参与属性、判定对象资格、**判定是否继续展开某节点**。

**实现要点**：位图必须建在全部属性上（含无对象的中间属性），否则闭包传播在中间节点断链。实测：只给有对象的属性建位图时，属性覆盖从 4.20 掉到 3.50、召回从 0.9127 掉到 0.8881。

### 3.4 复杂度与内存

| 项 | 量级 |
|---|---|
| 属性内边 | $O(n_a M)$，共 1,712,597 条（10.1 条/节点） |
| 代表点 | 2,393 个 |
| 跨属性边 | 12,722 条（双向，RNG 调整至度 16） |
| 位图 | 0.46 MB |
| 构建 | 168,937 对象、768 个非空属性：约 37 s（单线程） |

---

## 4. 检索：DQS（Dual-Queue Search）

### 4.1 两条队列

| 队列 | 容量 | 收谁 | 职责 |
|---|---|---|---|
| $q_1$ | $ef$ | 所有节点 | 主搜索，收集结果 |
| $q_2$ | $ef_2<ef$ | **仅代表节点** | 骨架漫游，跨子树跳转 |

每条队列两个堆：`cand`（最近优先，定展开顺序）、`keep`（容量上限，定淘汰阈值）。

### 4.2 算法

**算法 3　DQS**
```
输入：q, U, ef, ef2
1  D ← ⋁ desc(u) for u ∈ U
2  for c ∈ U: offer(entry(c))                       // 种子
3  while q1 或 q2 非空:
4      u ← 两队列 cand 堆头部中距离更近者
5      if own(u) ∉ D: continue                      // 越界即中断，不展开邻居
6      for w ∈ neighbors(u) ∪ crossEdges(u):
7          d ← dist(q, w)
8          if 能进 q1（未满 或 d < q1.keep 最差）: q1.offer(w)
9          if isRep(w) 且能进 q2（同规则）:        q2.offer(w)
10 结果 ← q1.keep ∪ q2.keep 中 own ∈ D 者，按距离取前 k
```

### 4.3 两个设计点

**越界即中断（第 5 行）**：位图逐节点判定，节点所属属性不在 $D$ 内时**不展开其邻居**。这是"结构跳过"在查询期的落地——不是"访问了但不收进结果"，而是根本不进入。

因为合并边是**双向**的（父↔子），只有配合这条中断，才能保证可达集不越出子树。去掉中断时实测：进入 49.8 个属性（$D$ 只有 4.2），距离调用飙到 3,109。

**用 $q_2$ 给代表点一条独立通道**：单队列时第一个属性的近点会填满 $ef$，其他属性的入口因距离更远被拒、不再展开。$q_2$ 让代表节点拥有一条不受 $q_1$ 阈值限制的通道，骨架层得以继续跨子树漫游。二者共享 visited 与距离计算，不产生额外索引。

---

## 5. 实验设置

| 项 | 设置 |
|---|---|
| 数据 | 属性 DAG：1,929 属性 / 2,040 边 / 103 个多父；对象 168,937；128 维 SIFT |
| 查询 | 200 条，每条指定 1 个属性；$|E(U)|$ 均值 1,140（选择率 0.675%） |
| GT | 原始 FP32 精确计算，k=50，ties-aware |
| 距离 | 项目统一实现 `raw_l2_space.h`；编译 `-O3 -fno-tree-vectorize` |
| 计时 | 单线程，`taskset -c 2`；每点 3 轮取平均；构建不计入 QPS |
| 参数 | $M{=}16$、$efC{=}200$、$p{=}50$、$ef_2{=}64$ |

**基线**：NodeANN——每属性一张完整 HNSW（$M{=}16,efC{=}200$），查询对各图各搜 top-$N$ 后归并。

---

## 6. 结果

### 6.1 帕累托前沿

![recall–QPS Pareto](2026-11-05-exps/figures/pareto-nodeann-vs-ours.svg)

| ef | recall | QPS | 距离/查询 | 属性覆盖 |
|---:|---:|---:|---:|---:|
| 50 | 0.7701 | **24,536** | **272** | 3.90/4.20 |
| 100 | 0.7826 | 18,272 | 320 | 3.90/4.20 |
| 150 | 0.7848 | 15,821 | 354 | 3.90/4.20 |
| 200 | 0.7861 | 14,595 | 382 | 3.90/4.20 |
| 300 | 0.7885 | 12,315 | 427 | 3.90/4.20 |
| 400 | 0.7885 | 10,232 | 459 | 3.90/4.20 |
| 600 | 0.7900 | 9,834 | 514 | 3.90/4.20 |
| 800 | 0.7900 | 8,780 | 558 | 3.88/4.20 |

| NodeANN | recall | QPS | 距离/查询 |
|---|---:|---:|---:|
| N=20 | 0.5713 | 16,531 | — |
| N=30 | 0.7217 | 15,024 | — |
| N=40 | 0.8372 | 12,513 | — |
| N=60 | **0.9132** | 11,042 | **1,448** |

**读数**：

1. 在 recall≈0.78 处，ASI 的 QPS 为 14.6k–18.3k，NodeANN 在同召回约 13.3k（插值）——**约 1.1–1.4×**；距离调用为 320–382，是 NodeANN 的 **22–26%**。
2. ASI 当前可达召回上限 **0.7900**，低于 NodeANN 的 0.9132。上限由属性覆盖（3.90/4.20）与属性内覆盖共同决定。
3. 距离维度优势明显：0.77 召回只需 272 次距离（精确扫描需 1,140，NodeANN 需 1,448）。

### 6.2 消融：$ef_2$

![ablation ef2](2026-11-05-exps/figures/ablation-ef2.svg)

| $ef_2$ | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|
| recall | 0.7736 | 0.7804 | 0.7835 | 0.7861 | **0.7878** |
| QPS | 16,839 | 17,506 | 14,298 | 15,944 | 15,490 |
| 属性覆盖 | 3.08 | 3.35 | 3.67 | 3.90 | **4.05** |

$ef_2$ 通过**属性覆盖**影响召回：$ef_2$ 越大，骨架层能漫游到的子树越多；到 128 时覆盖接近饱和（4.05/4.20）。

### 6.3 消融：代表点密度 $p$

![ablation p](2026-11-05-exps/figures/ablation-p.svg)

| $p$ | 10 | 25 | 50 | 100 |
|---|---:|---:|---:|---:|
| recall | **0.7912** | 0.7890 | 0.7861 | 0.7827 |
| 属性覆盖 | 4.04 | 4.01 | 3.90 | 3.96 |

$p$ 越小（代表点越密）覆盖越好、召回越高，但收益递减：$p$ 从 100 降到 50 提升 0.34 个百分点，50 降到 10 再提升 0.5 个百分点。默认 $p=50$ 是收益/开销的折中。

### 6.4 消融：拓扑合并

| 配置 | recall | QPS | 距离/查询 | 属性覆盖 |
|---|---:|---:|---:|---:|
| 有合并（默认） | **0.7861** | 14,595 | 382 | **3.90/4.20** |
| 无合并（各属性独立图） | 0.6382 | 25,415 | 206 | **0.91/4.20** |

去掉按拓扑序的合并后，**只能进入 0.91 个属性**——查询被完全困在种子属性内，跨子树无法导航。这直接印证了"合并 + 邻居更新"是跨属性可达性的唯一来源。

---

## 7. 已知限制

1. **召回上限 0.7900**：属性覆盖 3.90/4.20，约 7% 的属性未被访问。$ef$、$ef_2$、$p$ 均已扫到饱和（$ef_2=128$ 覆盖 4.05 仍有 0.7878），说明瓶颈在**进入机制**而非预算。
2. **$ef_2$ 与 $p$ 的分工**：$p$ 决定属性内接入点密度，$ef_2$ 决定骨架层漫游范围；两者都在影响"覆盖"，对"属性内搜多深"影响有限。
3. **跨属性边度数**：合并边双向且按 RNG 调整到 16，但多父属性的合并次数更多，度数分布未统计。
4. **多属性查询**：实验查询均指定 1 个属性；$|U|>1$ 未单独测量。
5. **规模**：仅 168,937 对象，1M 未测。
6. **计时**：绑核不等于独占 CPU；WSL 未暴露调频控制，频率未锁定。
