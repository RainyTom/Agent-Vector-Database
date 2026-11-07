# 方案：属性 DAG 上的代表点骨架 + 双队列图搜索

> 覆盖式维护。问题口径见 `problem_def.md`，数据见 `2026-11-05-exps/`。
> 术语约定：文中一律用**属性**（attribute）指代概念节点，不再用 "concept"。

## 1. 数据：CCS 分类体系

数据取自 ACM CCS（Computing Classification System），是一个**带多父的层级属性 DAG**：

| 统计量 | 值 |
|---|---|
| 属性节点 | 1,929 |
| 父子边 | 2,040 |
| 多父属性（≥2 个父） | 103 个（5.3%），最多 3 个父 |
| 深度 | 0–6 |
| 有直属对象的属性 | 768 |
| 对象总数 | 168,937 |
| 每属性对象数 | 均值 220、中位 66、最大 9,823 |

每个对象**只属于一个属性**（记作 $own(o)$）。向量 128 维，用 PQ(m=16,k=256) 压到 16 字节，搜索在还原向量上做，因此召回上限是 **0.9132**（不是 1.0）。

```svg
<svg viewBox="0 0 900 300" width="100%" font-family="sans-serif" font-size="12">
  <text x="16" y="18" fill="var(--muted)">属性 DAG 的真实形态：单根、深度 0–6、103 个属性有多个父</text>
  <g stroke="var(--border)" stroke-width="1.3" fill="none">
    <line x1="250" y1="46" x2="170" y2="92"/><line x1="250" y1="46" x2="330" y2="92"/>
    <line x1="170" y1="112" x2="120" y2="158"/><line x1="170" y1="112" x2="215" y2="158"/>
    <line x1="330" y1="112" x2="285" y2="158"/>
    <line x1="215" y1="158" x2="250" y2="200" stroke="var(--warning)" stroke-width="1.8"/>
    <line x1="285" y1="158" x2="250" y2="200" stroke="var(--warning)" stroke-width="1.8"/>
  </g>
  <g font-size="11" text-anchor="middle">
    <rect x="222" y="30" width="56" height="24" rx="5" fill="var(--surface)" stroke="var(--border)"/><text x="250" y="46" fill="var(--fg)">根</text>
    <rect x="142" y="90" width="56" height="24" rx="5" fill="var(--surface)" stroke="var(--border)"/><text x="170" y="106" fill="var(--fg)">A</text>
    <rect x="302" y="90" width="56" height="24" rx="5" fill="var(--surface)" stroke="var(--border)"/><text x="330" y="106" fill="var(--fg)">B</text>
    <rect x="92" y="156" width="56" height="24" rx="5" fill="var(--surface)" stroke="var(--border)"/><text x="120" y="172" fill="var(--fg)">C</text>
    <rect x="187" y="156" width="56" height="24" rx="5" fill="var(--warning)" fill-opacity="0.18" stroke="var(--warning)"/><text x="215" y="172" fill="var(--fg)">F</text>
    <rect x="257" y="156" width="56" height="24" rx="5" fill="var(--surface)" stroke="var(--border)"/><text x="285" y="172" fill="var(--fg)">G</text>
    <rect x="222" y="198" width="56" height="24" rx="5" fill="var(--warning)" fill-opacity="0.18" stroke="var(--warning)"/><text x="250" y="214" fill="var(--fg)">H</text>
  </g>
  <text x="60" y="250" fill="var(--warning)" font-size="11">F、H 各有两个父 —— 结构上是 DAG，不能压成树</text>
  <text x="60" y="272" fill="var(--muted)" font-size="11">多父使"一个属性一个位置"不成立，必须显式处理跨父的跳转</text>
</svg>
```

## 2. 查询定义

给定 $(q, U, k=50)$：

- $U\subseteq C$ 是查询指定的属性集合（OR 语义）；
- 合格集 $E(U)=\{o: own(o)\in Desc^{+}(U)\}$，即 $U$ 的**后代闭包**内全部对象；
- 返回 $E(U)$ 中距 $q$ 最近的 50 个。

本负载：200 条查询，平均 $|E(U)|=1{,}140$（选择率 0.675%），平均 $|D|=4.2$ 个属性。

## 3. 动机

**在全局图索引中，让同属性（同子树）的对象能被快速导航到，并且跨属性不靠逐层线性下潜。**

三条现有路线的问题（都可实测）：

| 路线 | 问题 |
|---|---|
| 全库一张图 + 谓词过滤（ACORN 式） | 谓词把图切碎，低选择率下搜索游不到目标子树；$ef$ 加大也不改善 |
| 每属性一张图各自搜索（NodeANN 式） | 能到上限，但每张图都从自己的入口重新爬、且每张都搜满 $ef$：实测 **1,448 次距离/查询**（比精确扫描的 1,140 还多 27%） |
| 精确扫描合格集（Pre-filter） | 成本等于 $|E(U)|$，是成本下限，但不是效率下限 |

目标是**用远少于 $|E(U)|$ 的距离调用**拿到接近上限的召回。

## 4. 索引结构

### 4.1 每个属性的局部图

对属性 $a$ 的直属对象集合建一张**单层图**：

1. 增量插入：新点从入口出发做构建期 beam（预算 $efC$）取候选；
2. **RNG 选边**：候选按距离升序，保留 $u$ 当且仅当对所有已保留的 $w$ 有 $d(u,w)>d(u,q)$；
3. **反向边收缩**：新点加入邻居表后若超度上界 $M$，对目标表重跑同一 RNG 规则；
4. 孤立点补最近邻、连通分量互连；
5. 邻接扁平化为「偏移数组 + 连续 id 数组」。

参数：$M=16$、$efC=200$。单层而非多层：属性内对象少（中位 66），多层只多几个节点，实测召回无差别。

### 4.2 代表点（骨架层）

每个属性按 **$1/p$**（默认 $p=50$）选代表点，上限 $K_{\max}=8$，用 **farthest-first**：

$$r_1=e_a,\qquad r_{i+1}=\arg\max_{x}\min_{j\le i} d(x,r_j)$$

含义是"每次都挑离已选代表最远的点"，使代表点覆盖该属性的各个方向。代表点之间建 **MST**，MST 边并入局部图，使任意两个代表点之间只需几跳。

实测：168,937 个对象中 **2,727 个代表节点**（1.6%）。

### 4.3 跨属性的跳转边

```svg
<svg viewBox="0 0 900 260" width="100%" font-family="sans-serif" font-size="12">
  <text x="16" y="18" fill="var(--muted)">只有父→子直连时只能线性下潜；加跳转边后可直接抵达非直接子孙</text>

  <g stroke="var(--muted)" fill="none" stroke-width="1.3">
    <line x1="120" y1="70" x2="120" y2="110"/><line x1="120" y1="130" x2="120" y2="170"/>
    <line x1="120" y1="190" x2="120" y2="230"/>
  </g>
  <g font-size="11" text-anchor="middle">
    <rect x="86" y="50" width="68" height="20" rx="4" fill="var(--surface)" stroke="var(--border)"/><text x="120" y="64" fill="var(--fg)">A（根）</text>
    <rect x="86" y="110" width="68" height="20" rx="4" fill="var(--surface)" stroke="var(--border)"/><text x="120" y="124" fill="var(--fg)">B</text>
    <rect x="86" y="170" width="68" height="20" rx="4" fill="var(--surface)" stroke="var(--border)"/><text x="120" y="184" fill="var(--fg)">C</text>
    <rect x="86" y="230" width="68" height="20" rx="4" fill="var(--surface)" stroke="var(--border)"/><text x="120" y="244" fill="var(--fg)">D</text>
  </g>
  <text x="20" y="160" fill="var(--danger)" font-size="11">线性下潜 3 跳</text>

  <path d="M160 60 C 280 60, 280 240, 166 240" stroke="var(--accent)" stroke-width="2" fill="none" stroke-dasharray="6 4" marker-end="url(#sk)"/>
  <defs><marker id="sk" markerWidth="10" markerHeight="10" refX="8" refY="3" orient="auto">
    <path d="M0,0 L0,6 L9,3 z" fill="var(--accent)"/></marker></defs>
  <text x="230" y="150" fill="var(--accent)" font-size="11">跳转边：A 的代表点 → D 的入口</text>

  <text x="430" y="70" fill="var(--fg)">规则：</text>
  <text x="430" y="92" fill="var(--muted)">· 父代表点 → 直接子属性入口（门户桥）</text>
  <text x="430" y="112" fill="var(--muted)">· 祖先代表点 → 距离 ≥2 跳的后代入口（跳转边）</text>
  <text x="430" y="132" fill="var(--muted)">· 全部**单向**（祖先 → 后代），保证可达集不越出子树</text>
  <text x="430" y="160" fill="var(--muted)">实测：7,181 条跳转边</text>
  <text x="430" y="186" fill="var(--muted)">没有跳转边时，从根进入只能线性下潜，</text>
  <text x="430" y="204" fill="var(--muted)">深层属性永远轮不到（实测覆盖 2.31/4.20）</text>
</svg>
```

## 5. 查询：q1 / q2 双队列

### 5.1 结构

| 队列 | 容量 | 收谁 | 作用 |
|---|---|---|---|
| **q1** | $ef$ | 所有节点 | 主搜索 |
| **q2** | $ef2$（$ef2<ef$） | **仅代表节点** | 骨架层漫游，跨子树跳转 |

每个队列两个堆：`cand`（最近优先，决定展开顺序）+ `keep`（容量上限，决定淘汰阈值）。

### 5.2 规则

```
对每个被访问的邻居 w：
  若已访问 -> 跳过
  计算 d = dist(q, w)
  若 w 能进 q1（q1 未满 或 d < q1 内最差）  -> 入 q1
  若 w 是代表节点，且能进 q2（同理）        -> 入 q2（即使进不了 q1 也可以进）
  两者都进不去 -> 丢弃        ← 淘汰规则对 q1、q2 完全一致

展开：每次从 q1、q2 的候选堆中取**更近**的那个点，访问其邻居
```

### 5.3 为什么需要 q2

单队列时，第一个属性的近点会迅速填满 $ef$，之后其他属性的入口因为距离更远而全被拒。实测：只有 q1 时，从根进入只能覆盖 **2.31/4.20** 个属性。

q2 让代表节点拥有一条**不受 q1 阈值限制**的通道，于是骨架层可以继续跨子树漫游。加上跳转边后覆盖达到 **3.91/4.20**。

### 5.4 闭包判定用位图

每个属性一张后代闭包位图（1,929 位 = 31 个 64 位字，总计 0.5 MB）：

$$D=\bigvee_{u\in U}\mathrm{desc}(u)$$

- 查询期 $O(|U|\times31)$ 次字或即可得到 D，无需逐查询 DFS；
- 对象是否可进结果：查其归属属性的位是否置位（$O(1)$）；
- 跳转边的目标是否在范围内：同一位图判定。

**实现要点**：位图必须建在**全部属性**上（含没有直属对象的中间属性），否则闭包传播会断链——漏掉中间节点，其后代整支丢失。

```svg
<svg viewBox="0 0 900 200" width="100%" font-family="sans-serif" font-size="12">
  <text x="16" y="18" fill="var(--muted)">位图：1,929 位 = 31 个 64 位字，总计 0.5 MB</text>
  <g font-family="monospace" font-size="10">
    <rect x="20" y="34" width="380" height="24" fill="var(--surface)" stroke="var(--border)"/><text x="26" y="51" fill="var(--fg)">desc(A) : 1111 1111 0000 0000 ...</text>
    <rect x="20" y="64" width="380" height="24" fill="var(--surface)" stroke="var(--border)"/><text x="26" y="81" fill="var(--fg)">desc(B) : 0000 0110 0000 0000 ...</text>
  </g>
  <text x="420" y="52" fill="var(--muted)">查询 U={A}：D = desc(A)，按置位枚举得到参与属性</text>
  <text x="420" y="82" fill="var(--muted)">查询 U={A,B}：D = desc(A) OR desc(B)</text>
  <text x="20" y="120" fill="var(--muted)">总开销：|U| × 31 次字或 + 置位枚举，相对每查询数百次距离计算可忽略。</text>
  <text x="20" y="146" fill="var(--muted)">用途：闭包求解、结果资格判定、跳转目标判定 —— 三者共用同一张位图。</text>
  <text x="20" y="172" fill="var(--muted)">必须覆盖全部属性（含无对象的中间属性），否则闭包断链。</text>
</svg>
```

## 6. 代价

| 项 | 数值 |
|---|---|
| 索引内存 | 每属性 $2M\cdot n_a$ 条边（32 位）+ 2,727 个代表点 + 7,181 条跳转边 + 位图 0.5 MB |
| 构建时间 | 168,937 对象、768 个非空属性：约 37 s（单线程） |
| 每查询距离调用 | $ef=50, ef2=16$：**258 次**（合格集的 22.6%） |

## 7. 实验协议

单线程，`taskset -c 2` 绑定逻辑 CPU 2（非独占；WSL 未暴露调频控制，频率未锁定）。每点 2–3 轮取平均。三条臂共用同一 `RawL2Space` 距离实现、同一 GT（原始 FP32 上算，k=50，ties-aware）、同一 PQ 还原向量、`M=16`、`efC=200`。

## 8. 结果

![recall–QPS](2026-11-05-exps/figures/pareto-nodeann-vs-ours.svg)

| ef | ef2 | recall | QPS | 距离/查询 | 覆盖属性 |
|---:|---:|---:|---:|---:|---:|
| 50 | 16 | 0.773 | **25,423** | **258** | 3.75/4.20 |
| 100 | 16 | 0.782 | 20,087 | 313 | 3.91/4.20 |
| 200 | 16 | 0.804 | 15,258 | 386 | 3.91/4.20 |
| 400 | 16 | 0.808 | 11,517 | 473 | 3.92/4.20 |
| 800 | 16 | 0.811 | 8,756 | 583 | 3.95/4.20 |

**对照（同协议）**

| 方案 | recall | QPS | 距离/查询 |
|---|---:|---:|---:|
| NodeANN（N=60） | **0.9132** | 10,437 | 1,448 |
| Pre-filter（精确扫描） | 0.9132 | 10,544 | 1,140 |
| 本方案（ef=50, ef2=16） | 0.773 | **25,423** | **258** |
| 本方案（ef=800, ef2=16） | 0.811 | 8,756 | 583 |

**读数**：

1. 在 0.77–0.81 召回区间，本方案的 QPS 是 NodeANN 的 **2.4×**（25,423 vs 10,437），距离调用是其 **18%**（258 vs 1,448）。
2. 但本方案的召回上限目前只有 **0.811**，距离 NodeANN 的 0.9132 还有 10 个百分点。
3. 覆盖属性数 3.91/4.20：仍有约 7% 的属性进不去，这是召回上不去的主因。

## 9. 已知问题与下一步

1. **召回上限 0.811**：$ef$ 从 50 加到 800 只涨 0.04，说明瓶颈是覆盖而非搜索深度。$ef2$ 与代表点密度 $p$ 都已扫过（$ef2\in\{16,32,64\}$、$p\in\{10,20,50\}$），召回只从 0.804 动到 0.808——**普通节点被 q1 拒绝后没有第二次机会**，而 q2 只收代表节点。
2. 待解决：如何让普通节点也能在"本属性已满"时获得一条不受全局阈值限制的通道，而不退化成"每属性独立搜索"。
3. 1M 规模未测。
