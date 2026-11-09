# NestedAttrVectorRetrieval：方案说明与观测面板

只描述**当前正在执行的方案**：怎么建索引、怎么合并、怎么查、有哪些开关、跑出什么。不写理论推导、不写文献论证。

## 1. 问题与输出

层级属性 DAG 上的向量 top-k 检索。每个对象有唯一直属属性；查询给定属性集合 $U$ 与向量 $q$，在闭包

$$D=\mathrm{Desc}^{+}(U)$$

的合格对象中返回距离最近的 $k$ 个。$D$ 同时是**结果允许集**：不在 $D$ 的对象永不返回。

## 2. 与 NodeANN 的区别

| | NodeANN | 本方案 |
|---|---|---|
| 图的数量 | 每属性一张 HNSW | 每属性一张 HNSW，再用桥边接成**一张全局对象图** |
| 每查询图搜索次数 | $\lvert D\rvert$ 个非空属性各一次 | **1 次** |
| 搜索状态 | 每属性一份候选/保留集，最后归并 | **一份波束、一份保留集** |
| 归并成本 | $\lvert D\rvert$ 路归并 | 无显式归并 |
| 代价 | 覆盖天然完整 | 依赖桥边把属性接起来 |

一句话：NodeANN 用 $\lvert D\rvert$ 次独立搜索换覆盖；本方案用一次遍历换延迟，代价是必须自己解决跨属性可达性。

## 3. 代码与数据集

```text
codes/
├── NestedAttrVectorRetrieval/     # 本方案
│   ├── build.sh                   # 编译出 build/asi
│   ├── src/
│   │   ├── asi_index.h/.cpp       # 索引：build_index / save_index / load_index
│   │   ├── asi_search.h/.cpp      # 查询：run_queries
│   │   └── asi_main.cpp           # 子命令 build | search
│   └── scripts/                   # build_dataset.py / run_experiments.py / plot_pareto.py
├── NodeANN/                       # 基线：每属性一张 HNSW 再归并
├── ACORN/                         # 基线：单张全局 HNSW + 谓词感知遍历
├── shared/                        # dataset、raw_l2_space、native_graph_audit
└── third_party/hnswlib/           # 固定版本 ca42672
```

不同阶段用不同的进程：索引一次 `build` 落盘，查询网格只 `search`，不重建。

统一数据集目录：

```text
<dataset>/base.bin       float32, n × dim       对象向量
<dataset>/edges.tsv      "child parent"          属性 DAG
<dataset>/objects.txt    每行一个属性 ID          对象归属
<dataset>/queries.bin    float32, m × dim        查询向量
<dataset>/queries.tsv    "<属性个数> <属性>..."   查询谓词
<dataset>/gt_<k>.txt     int32, m × k            FP32 精确 top-k，-1 填充
<dataset>/meta.json      维度、规模、种子、统计
```

## 4. 当前数据集

`datasets/sift1m/ccs-attr`：`--alpha 0.8 --internal-share 0.2 --seed 20261109`。

| 项 | 值 |
|---|---|
| 对象 | 1,000,000（SIFT1M 原始 float32，$d=128$） |
| 属性 DAG | 1,929 属性、2,040 边、1,516 叶子、最长 6 层、无多父、单根 |
| 对象归属 | 80% 挂叶子（Zipf $\alpha{=}0.8$），20% 均匀挂内部属性 |
| 每属性对象数 | 最小 103、中位 300、均值 518、最大 46,400 |
| 查询 | 200 条，按属性深度分层：深度 0–6 各 36/35/35/35/35/20/4 |
| 合格集 $\lvert E(U)\rvert$ | 中位 7,242、均值 44,888、最大 1,000,000 |
| 闭包非空属性数 | 平均 94.4 |
| 真值 | 闭包内精确 FP32 top-$k$ |

对象必须挂一部分在内部属性上：全挂叶子时父属性没有对象，就没有代表点，跨属性边恒为 0，单图会退化成 1,516 个孤立子图。

## 5. 索引构建

### 5.1 第一步：每属性一张 HNSW

对每个有对象的属性 $S_a$（$n_a$ 个对象）：

1. **属性内 HNSW**：按对象 ID 递增插入，种子 `100 + attribute_id`，$M=16$、$efConstruction=200$。
2. **layer 0 导出为有向邻接表**，作为全局图的局部边，**不补无向边、不重剪**。
3. **上层邻接**导出为紧凑数组，只用于 `--entry-mode hnsw` 时定位起点。
4. **代表点不在这一步确定**：代表点由第 5.2 节的合并过程产生，数量为 $\mathrm{round}(\sqrt{N})$，$N$ 是当前合并的累积集规模。属性内 HNSW 只提供局部邻接。

### 5.2 第二步：合并 —— 后序两两合并 + 逐步重聚类

合并只做两件事：**加边、不改边**。属性内 layer-0 邻接在合并前后完全一致。

```text
merge(all attribute graphs):
    for a in reverse topological order(DAG):        # 后序：子先于父
        acc ← nodes(a)                              # 本属性直属对象
        level ← ∅
        if a has no child:
            level ← cluster_reps(acc, round(sqrt(|acc|)))
        for c in children(a):                       # 两两合并：一次并入一个子子树
            acc ← acc ∪ subtree(c)
            next ← cluster_reps(acc, round(sqrt(|acc|)))
            link_each(next, nearest m of (level ∪ reps(c)))   # 双向，构成骨架
            level ← next
        reps(a) ← level
        subtree(a) ← acc
    skeleton_top ← reps(root)
```

`cluster_reps(S, k)`：对集合 $S$ 做 k-means，取 $k$ 个簇；**聚簇中心不是原始向量，因此每个簇取离中心最近的原始向量作为代表点**。代表点始终是真实节点，所以它天然属于某张属性内子图。

三条性质：

1. **代表点数量随规模增长**：$k=\mathrm{round}(\sqrt{N})$，$N$ 为当前累积集规模（自身成员 + 已并入的子树）。
2. **层级由合并步骤产生**：粗层代表点（累积集更大）连向细层代表点（它由哪些集合合并而来），这是唯一的跨属性结构。
3. **保边**：属性内邻接在任何合并参数下都不变。

实测（$M=16$、$efC=200$、$--bridge-receivers 1$）：

| 项 | 值 |
|---|---|
| 聚类次数 | 3,556 |
| 骨架边（无向） | 137,367 |
| 桥边槽位（双向） | 274,734 |
| 顶层代表点 | 1,043（覆盖 612 个属性） |
| 有桥边的节点 | 119,466（11.95%） |
| 桥出度 p99 / max | 5 / 52 |
| 属性内局部边 | 14,142,953（出度均值 14.1） |
| 建索引耗时 | 5 分 22 秒（属性内 HNSW 106 s + 合并） |

聚类在有限样本上训练中心（每步最多 $\max(2048, 16k)$ 个样本），代表点从该样本中取，以保证构建代价为 $O(k\cdot\text{sample})$。

### 5.3 合并的三种规则（对比用）

| `--K-rule` | 代表点 | 跨属性边 | 桥边槽位 |
|---|---|---|---|
| `linear` | 每属性至多 $K_{cap}=8$ 个，farthest-first | 父代表点 → 子属性最近 $m$ 个成员，外加入边为 0 的 repair | 138,928 |
| `sqrt` | 每属性 $\mathrm{round}(\sqrt{n_a})$ 个，farthest-first | 同上 | 275,768 |
| `cluster` | 后序合并逐步重聚类，$\mathrm{round}(\sqrt{N})$ | 层级骨架（当前方案） | 274,734 |

## 6. 查询流程

### 6.1 两个集合

| 集合 | 定义 | 作用 |
|---|---|---|
| $D$ | $\mathrm{Desc}^{+}(U)$，用闭包位图按位或 | 允许**返回** |
| $A$ | $\mathrm{Desc}^{+}(U)\cup\mathrm{Anc}^{+}(U)$ | 允许**遍历** |

$A$ 由 $D$ 的位图与「沿 `parents_of` 向上走」得到的位图按位或得到，不落盘、不改索引格式。

### 6.2 状态

```
keep_heap    容量 ef，只装可返回节点（A ∩ D）  → 结果集
frontier     容量 ef，装所有可导航节点         → 波束
visited/expanded/seed_score   按查询代号打标记，跨查询复用
```

保留集与波束**分开**是必需的：合并后大量祖先节点可遍历但不可返回，若共用一份容量，祖先会挤掉真正的候选。

### 6.3 入口

**从骨架顶层代表点起步**：`skeleton_top` 的 1,043 个节点逐个 `offer`。不在可导航集 $A$ 内的会立刻返回、不算距离。

`--K-rule linear/sqrt` 时没有骨架，退回「根属性入口点单点起步」（`--entry-mode hnsw` 可先用根属性上层做贪心下降）。

### 6.4 逐节点

```
offer(v):
    已带本查询代号 → 返回
    写首次接触代号
    own(v) ∉ A → 返回（不算距离、不入队、不展开）
    d ← 距离（若已算过则复用缓存）
    v ∈ D 时：按 ef 维护 keep_heap
    v ∈ A 时：按 ef 维护 frontier

当 frontier 非空:
    最近未展开候选劣于保留集最差（且保留集已满）→ 停止
    弹出最近节点 → 本查询已展开则跳过
    标记已展开，扫 layer-0 局部边 + 跨属性桥边，对每个目标 offer

输出 ← keep_heap 按距离排序取前 k
```

同一节点只展开一次；入口点与桥接收点的分数在查询内缓存复用。

## 7. 参数

| 参数 | 默认 | 作用 |
|---|---|---|
| `--data` / `--index` | — | 数据集目录 / 索引文件 |
| `--k` | 10 | 返回条数，决定读哪个 `gt_<k>.txt` |
| `--M` / `--efC` | 16 / 200 | 属性内 HNSW 度数上限与构建宽度 |
| `--K` | 8 | 仅 `linear` 规则用的代表点上限 |
| `--K-rule` | `cluster` | `cluster`（后序重聚类）/ `sqrt` / `linear` |
| `--bridge-receivers` | 1 | 骨架中每个新代表点向池中连几个最近点 $m$ |
| `--no-merge` | 关 | 不建桥边，用于测孤立子图 |
| `--efs` | `10,20,40,80,160,320` | 保留集与波束容量，可一次跑多档 |
| `--entry-mode` | `representative` | `hnsw` 时根属性先用上层贪心下降定起点 |
| `--queries` / `--repeat` | 全部 / 1 | 查询数与前缀重复轮数 |
| `--trace-file` | — | 逐查询诊断 JSONL |
| `--rerank-fp32` | 关 | 保留集 FP32 重排（本数据集无量化误差，无收益） |

`ef` 同时是保留集容量与波束容量，两者没有解耦。按约束，$ef\in\{1.5,2.5,3.5,5,8,10\}\times k$。

## 8. 输出字段

| 字段 | 含义 |
|---|---|
| `recall` | 对 FP32 精确 GT 的 ID 命中率，按有效 GT 条目聚合 |
| `qps` | 计时区间内查询数 / 秒，不含建图与 GT 计分 |
| `dists_per_q` | 距离调用 / 查询 |
| `adjacency_slots_per_q` | 扫描的邻接槽位 / 查询（局部 + 桥） |
| `expanded_per_q` | 展开节点数 / 查询，已去重 |
| `reps_scored_per_q` | 代表点评分数 / 查询 |
| `eligible_per_q` / `attrs_in_scope` / `attrs_entered` | 合格对象数 / 闭包非空属性数 / 准入过对象的属性数 |

trace 另给 `returned`、`evaluated`、`admitted`、`seed_nodes`、`expanded_objects`、`entered_attrs`、`touched_attrs`。

## 9. 实测

单进程、单轮、200 查询，$k=10$。QPS 随负载波动，只作量级参考。

### 9.1 三种合并规则，同一停止规则

| ef | `linear` 网关 | `sqrt` 代表点 | `cluster`（现行） |
|---:|---|---|---|
| 40 | 0.1360 / 3,001 | 0.2325 / 3,972 | 0.1660 / 6,334 |
| 160 | 0.2055 / 2,402 | 0.2875 / 2,734 | 0.3130 / 3,277 |
| 320 | 0.2875 / 1,938 | 0.3820 / 2,102 | 0.4295 / 2,247 |
| 640 | 0.4745 / 1,065 | 0.5170 / 1,207 | 0.5550 / 1,025 |
| 1280 | 0.6030 / 649 | 0.6405 / 721 | 0.6395 / 649 |
| 2560 | 0.7040 / 404 | 0.7385 / 441 | 0.7245 / 378 |

（召回 / QPS）

### 9.2 约束区间 $ef\in\{1.5,2.5,3.5,5,8,10\}\times k$

| ef | `cluster` 召回 | QPS | 距离/查询 | 进入属性数 | `sqrt` 召回 |
|---:|---:|---:|---:|---:|---:|
| 15 | 0.1045 | 9,395 | 294 | 11.9 | 0.1460 |
| 25 | 0.1360 | 8,098 | 335 | 15.0 | 0.1930 |
| 35 | 0.1480 | 7,770 | 370 | 17.3 | 0.2290 |
| 50 | 0.1820 | 6,791 | 402 | 19.8 | 0.2480 |
| 80 | 0.2185 | 5,304 | 513 | 22.8 | 0.2690 |
| 100 | 0.2485 | 4,935 | 548 | 24.1 | 0.2780 |

闭包平均 94.4 个属性，`ef=100` 只进入 24.1 个。

### 9.3 基线

| 方法 | 配置 | Recall@10 | QPS | 距离/查询 |
|---|---|---:|---:|---:|
| NodeANN | `N=10, ef=10`（$1\times k$） | **0.9830** | 489 | 15,599 |
| NodeANN | `N=20, ef=20` | 0.9985 | 380 | 13,197 |
| ACORN | `ef=10` | 0.9775 | 62 | 42,691 |

### 9.4 成本构成（`cluster`，`ef=100`）

| 项 | 每次查询 |
|---|---:|
| 距离调用 | 547.6 |
| 其中入口打分（未展开） | **406.9，占 74.3%** |
| 展开节点 | 140.7 |
| 扫描邻接槽位 | 1,861（13.23 / 展开） |

入口成本与 `ef` 无关、恒定：顶层 1,043 个代表点每次查询都要打分（只有落在 $A$ 内的才实际算距离）。`dists ≈ 展开数 × 1.1 + 入口`。

## 10. 现在的瓶颈

| 现象 | 证据 |
|---|---|
| **入口占掉大部分距离** | `ef=100` 时 74.3% 的距离花在 1,043 个顶层代表点的打分上，且这部分不随 `ef` 变小 |
| **展开数只由 `ef` 决定** | 停止规则用「候选劣于保留集最差」，因此 `ef` 直接决定要展开多少节点；`ef` 收在 $10k$ 时只能展开约 140 个节点 |
| **覆盖严重不足** | `ef=100` 进入 24.1 / 94.4 个闭包属性；`ef=2560` 也才 57.4 |
| **召回远低于基线** | 约束区间内最好 0.2780（`sqrt`）/ 0.2485（`cluster`），NodeANN 在 $1\times k$ 就到 0.9830 |
| **属性内遍历太浅** | 每属性中位 300 个对象，实际展开中位个位数 |

单波束要横跨 94 个互不相通的属性区域，而收敛只能靠放大 `ef`；这是方案结构本身的问题，不是调参能解决的。

## 11. 可调方向

| 方向 | 状态 |
|---|---|
| 合并规则 | 已测三种（`linear` / `sqrt` / `cluster`）。`cluster` 在 `ef≤640` 段召回最高，`ef≥1280` 与 `sqrt` 持平 |
| 骨架顶层宽度 | 顶层 1,043 个代表点、入口占 74% 距离；这是当前最大的单项成本 |
| 每属性展开配额 | 未实现 |
| 入口覆盖 | 约束区间内只进入 24/94 个属性，是召回低的主因 |
| 属性内遍历深度 | 每属性展开中位个位数 |
| FP32 重排 | 无量化误差，无收益，默认关 |

## 12. 运行

```bash
./codes/NestedAttrVectorRetrieval/build.sh
D=datasets/sift1m/ccs-attr

# 建索引（约 107 s）
./codes/NestedAttrVectorRetrieval/build/asi build --data $D --index $D/index/asi.bin

# 查询
./codes/NestedAttrVectorRetrieval/build/asi search --data $D --index $D/index/asi.bin \
  --k 20 --efs 30,40,50,60 --queries 200

# 基线
./codes/NodeANN/build/nodeann search --data $D --index-dir $D/idx-nodeann \
  --per-n 20 --ef 30,40,50,60 --queries 200
```

当日实验记录与数据文件见 [2026-11-09-exps/report.md](2026-11-09-exps/report.md)。
