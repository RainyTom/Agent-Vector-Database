# NestedAttrVectorRetrieval：方案说明与观测面板

只描述**当前正在执行的方案**：怎么建索引、怎么查、有哪些参数和开关、跑出什么、下一步拧哪个旋钮。不写理论推导、不写代码审查、不写文献论证。

## 1. 方案与输出

在层级属性 DAG（OR 闭包）上做向量 top-k 检索。每个对象有唯一直属属性；查询给定属性集合 $U$ 与向量 $q$，在闭包 $D=\mathrm{Desc}^{+}(U)$ 的合格对象中返回距离最近的 $k$ 个。

与基线 NodeANN 的区别：NodeANN 对 $D$ 中每个非空属性各搜一次再归并；本方案把属性内图接成**一张全局对象图**，整条查询只用**一份搜索状态、一份保留集**。

## 2. 代码与数据集组织

```text
codes/
├── NestedAttrVectorRetrieval/     # 本方案
│   ├── build.sh                   # 编译出 build/asi
│   ├── src/
│   │   ├── asi_index.h/.cpp       # 索引结构；build_index / save_index / load_index
│   │   ├── asi_search.h/.cpp      # 查询：run_queries
│   │   └── asi_main.cpp           # 子命令 build | search
│   └── scripts/
│       ├── build_dataset.py       # 任意向量集 + 任意层级 → 统一数据集
│       ├── run_experiments.py     # 建索引(缺则建) + 跑网格 → measurements.jsonl
│       └── plot_pareto.py         # measurements.jsonl → Pareto 图
├── NodeANN/                       # 基线：每属性一张 HNSW 再归并（build|search 同样分离）
├── ACORN/                         # 基线：单张全局 HNSW + 谓词感知遍历
├── shared/                        # dataset.h/.cpp（统一加载器）、raw_l2_space.h、native_graph_audit.h
└── third_party/hnswlib/           # 固定版本 hnswlib
```

两条约定：

- **索引构建与查询分离。** 每个方案都是 `build` + `search` 两次独立进程，中间落盘一个索引文件；查询网格不需要重建索引。
- **数据集格式统一。** 任何数据集都是同一个扁平目录，`--data DIR` 一个参数，不存在按数据集分支的代码：

```text
<dataset>/base.bin       float32, n × dim       对象向量
<dataset>/edges.tsv      "child parent"          属性层级
<dataset>/objects.txt    每行一个属性 ID          对象归属
<dataset>/queries.bin    float32, m × dim        查询向量
<dataset>/queries.tsv    "<属性个数> <属性>..."   查询谓词
<dataset>/gt_<k>.txt     int32, m × k            FP32 精确 top-k
<dataset>/meta.json      维度、规模、种子、统计
```

## 3. 当前数据集

`datasets/sift1m/ccs-attr`，由 `scripts/build_dataset.py` 生成（`--alpha 0.8 --internal-share 0.2 --seed 20261109`）：

| 项 | 值 |
|---|---|
| 对象 | 1,000,000（SIFT1M 原始 float32） |
| 属性 | CCS 层级：1,929 属性、2,040 边、1,516 叶子、最长 6 层、无多父 |
| 对象归属 | 80% 挂叶子（Zipf α=0.8），20% 均匀挂内部属性 |
| 叶子规模 | 中位 234，最大 46,400 |
| 查询 | 200 条，按属性递归深度分层：深度 0–6 各 36/35/35/35/35/20/4 条 |
| 合格集 $\lvert E(U)\rvert$ | 中位 7,242、均值 44,888、最大 1,000,000 |
| 闭包非空属性数 | 平均 94.4 |
| 距离 / 真值 | 建图与搜索用 SIFT 原始 float32，真值为闭包内精确 FP32 top-10 |

对象挂在内部属性上是必需的：若全部挂在叶子上，父属性没有对象就没有代表点，跨属性网关边会恒为 0，单图退化成 1,516 个孤立子图。

## 4. 索引结构

对每个有对象的属性 $S_a$：

1. **属性内 HNSW**：hnswlib 多层图，按对象 ID 递增插入，种子 `100 + attribute_id`。$M=16$，$efConstruction=200$。
2. **layer 0** 导出为有向邻接表，作为全局图的局部边；不补无向边。
3. **上层邻接**导出为紧凑数组，仅供 `--entry-mode hnsw` 选入口。
4. **代表点**：farthest-first，数量 $\max(1,\min(K_{cap},\lfloor n_a/p\rfloor+1))$；同属性代表点之间加双向 MST 边。
5. **跨属性网关**：自底向上遍历属性；对每个属性和它有成员的祖先，把祖先的每个代表点连到该属性中距其最近的对象，双向、冻结、不剪枝。
6. **repair**：没有任何跨属性入边的属性，把它的代表点与最近有成员祖先的代表点两两相连。
7. **闭包位图**：为完整 DAG（含无对象的中间属性）计算后代位图。

### 4.1 合并后的实际形状（当前索引）

| 项 | 值 |
|---|---|
| 有对象的属性 | 1,929 / 1,929 |
| 每属性对象数 | 最小 103、中位 300、均值 518、最大 46,400 |
| 代表点 | 合计 11,756；每属性最少 3、中位 7、最多 8 |
| 属性内出度 | 均值 14.1、中位 13、p95 27、最大 36 |
| 跨属性边 | 138,928 个槽位 = 107,576 个 rep→rep（53,788 条无向 repair）+ 15,676 个 rep→object 与 15,676 个反向（7,838 条无向网关） |
| 跨属性出度 | 均值 0.14，最大 91；只有 25,355 个节点（2.5%）有任何跨属性边 |
| 网关节点 | 22,883 个对象被标为网关 |
| 属性级跨入度 | 中位 40、均值 55.8、最大 601，无零入边属性 |

两点要注意：网关边指向的是“父代表点最近的**单个对象**”，那个对象极少本身是代表点，所以**rep→rep 的连通主要来自 repair**（53,788 条），不是网关；跨属性边整体极稀疏，导航绝大部分时间发生在属性内部。

建索引约 107 s。

## 5. 查询流程

```
D ← OR(desc[u] for u in U)
inD ← D 中有直属对象的属性
建唯一通道的 candidate_heap 与 keep_heap（容量 ef），清查询代号

offer(v):
    已带本查询代号 → 返回
    写首次接触代号
    own(v) ∉ D → 返回（不算距离、不入队、不展开）
    d ← 种子分数缓存命中 ? 复用 : 计算
    keep_heap 满且 d ≥ 最差 → 丢弃；否则写入 candidate_heap 与 keep_heap

代表点队列（默认，--reps-topL L）：
    对 inD 的全部代表点评分
    取距离最近的 L 个放进独立队列
    队列里每个代表点都强制展开一次（不看 keep 阈值）
对照入口（--reps-topL 0）：
    inD 每个属性选一个代表点作种子，offer 后进入普通 beam

当 candidate_heap 非空：
    弹出最近节点
    本查询已展开 → 继续弹
    标记已展开，扫描 layer-0 邻接 + 跨属性边，对每个目标 offer

输出 ← keep_heap 按距离排序取前 k（可选 --rerank-fp32 重排保留集）
```

两处保结果的工作削减：同一对象只展开一次；代表点/种子评分在查询内缓存复用。

**第二通道与 `ef2` 已移除**：只有一条通道，结果只来自它的 keep。

## 6. 参数与开关

| 参数 | 常用值 | 作用 |
|---|---|---|
| `--data` | 数据集目录 | 统一数据集路径 |
| `--index` | 索引文件 | build 写出、search 读取 |
| `--k` | 10 | 返回条数、真值口径 |
| `--M` / `--efC` | 16 / 200 | 属性内 HNSW 度数上限与构建宽度 |
| `--K` | 8 | 代表点数量上限 $K_{cap}$ |
| `--p` | 50 | 代表点密度，值越小代表点越多 |
| `--efs` | `10,20,40,80,160,320` | 唯一通道 keep 容量，可一次跑多档 |
| `--reps-topL` | 32 | 代表点独立队列宽度；0 关闭，退回每属性一个种子 |
| `--entry-mode` | `representative` | `--reps-topL 0` 时的入口：`representative` 或 `hnsw` |
| `--queries` / `--repeat` | 200 / 1 | 查询数与前缀重复轮数 |
| `--trace-file` | — | 逐查询诊断 JSONL |
| `--rerank-fp32` / `--rerank-pool` | 关 / 0 | 保留集 FP32 重排（本数据集无量化误差，默认关） |

注意：`ef` 同时控制**保留集容量**和**队列宽度**。

## 7. 输出字段

单行 JSON，每档 `ef` 一条：

| 字段 | 含义 |
|---|---|
| `recall` | 对 FP32 GT 的严格 ID 命中率，按有效 GT 条目聚合 |
| `qps` | 计时区间内查询数/查询秒（不含建图、GT 计分） |
| `dists_per_q` | 实际距离调用/查询，含代表点评分 |
| `adjacency_slots_per_q` | 实际扫描的邻接槽位/查询 |
| `expanded_per_q` | 实际展开节点数/查询（已去重） |
| `reps_scored_per_q` / `reps_expanded_per_q` | 代表点评分数 / 强制展开数 |
| `eligible_per_q` / `attrs_in_scope` / `attrs_entered` | 合格对象数 / 闭包非空属性数 / 至少准入一个对象的属性数 |
| `distance_cache_hits_per_q` | 复用种子分数省下的距离 |

trace 文件另给逐查询的 `returned`、`evaluated`、`admitted`、`seed_nodes`、`expanded_objects`、`entered_attrs`。

## 8. 当前实测工作点

单进程、单轮、200 查询、$k=10$；QPS 随机器负载波动，只作量级参考。三方案同一数据集、同一预算口径。

| 方法 | 配置 | Recall@10 | QPS | 距离/查询 |
|---|---|---:|---:|---:|
| Ours | `ef=10, L=32` | 0.5710 | 2,225 | 1,282 |
| Ours | `ef=40, L=32` | 0.6930 | 1,258 | 1,962 |
| Ours | `ef=160, L=32` | 0.7790 | 718 | 3,673 |
| Ours | `ef=10, L=128` | 0.6025 | 1,394 | 2,005 |
| Ours | `ef=160, L=128` | 0.8660 | 620 | 4,564 |
| Ours | `ef=320, L=128` | 0.9105 | 457 | 6,281 |
| Ours | `ef=640, L=128` | 0.9380 | 311 | 8,630 |
| Ours | `ef=1280, L=128` | 0.9530 | 229 | 11,454 |
| NodeANN | `N=10` | 0.9830 | 489 | 9,868 |
| NodeANN | `N=20` | 0.9985 | 380 | 13,197 |
| NodeANN | `N=40` | 1.0000 | 286 | 17,476 |
| ACORN | `ef=10` | 0.9775 | 62 | 42,691 |
| ACORN | `ef=320` | 1.0000 | 1 | 296,693 |

同样的 `ef` 下，扩大代表点队列宽度（`L`）的收益与代价：

| ef | L=32 | L=128 | L=全部（581.6 个/查询） |
|---:|---|---|---|
| 10 | 0.5710 / 2,225 | 0.6025 / 1,394 | 0.6205 / 317 |
| 160 | 0.7790 / 718 | 0.8660 / 620 | 0.9025 / 286 |
| 320 | 0.8120 / 481 | 0.9105 / 457 | 0.9485 / 253 |
| 640 | 0.9380 / 311 | — | 0.9730 / 196 |
| 1280 | 0.9530 / 229 | — | 0.9840 / 164 |

L 取全部时，闭包 94.4 个属性**全部被展开**，属性层损失归零，`ef=320` 的剩余损失 100% 是“属性已展开但目标对象未被算距离”，每属性展开中位仅 9 个。也就是说**入口覆盖已不是瓶颈，属性内遍历深度才是**。

Pareto 前沿见 [figures/pareto.svg](2026-11-09-exps/figures/pareto.svg)：Ours `0.5710/2,225` → `0.9530/229`；NodeANN `0.9830/489` → `1.0000/286`；ACORN `0.9775/62` → `1.0000/6`。

当前格局：**NodeANN 最强**（0.983 时 QPS 489，并支配 ACORN 全部点）。本方案只在低召回段更快（0.571 时 2,225 QPS），要到 `ef=1280` 才到 0.953 且只剩 229 QPS。合格集均值 44,888：NodeANN 用 22%、本方案用 10–26% 的距离完成检索，**都没有退化成 pre-filter**；ACORN 在 `ef=10` 已用掉 95% 合格集距离，属退化。

## 9. 可调方向与当前判断

| 方向 | 状态 |
|---|---|
| 代表点队列宽度 `L` | 已测：同等 `ef` 下 L=128 比 L=32 召回 +3–13pp，代价是距离约 1.5 倍 |
| `ef` 与保留集解耦 | 未实现；`ef` 同时放大队列与保留集，高召回段成本陡增，是追上 NodeANN 的关键 |
| 入口覆盖 | L 取全部时 94/94 属性全展开、属性层损失归零（`ef=320` 召回 0.9485），但 QPS 掉到 253；覆盖已不是瓶颈 |
| 属性内遍历深度 | 现在是唯一剩余瓶颈：每属性中位 300 个对象，只展开约 9 个；需要把“每属性展开次数”从 `ef` 解耦 |
| 跨属性网关接入质量 | 现在按“父代表点最近的单个子对象”接入，每属性只有一个入口 |
| 高层入口 | 已实现，未在新数据集测试 |
| FP32 重排 | 本数据集无量化误差，重排无收益，默认关闭 |
| ACORN 对照 | 在本工作负载退化（visited 常超过合格集），只能作为“未退化失败”的负例 |

## 10. 运行

```bash
./codes/NestedAttrVectorRetrieval/build.sh
D=datasets/sift1m/ccs-attr
./codes/NestedAttrVectorRetrieval/build/asi build  --data $D --index $D/index/asi.bin
./codes/NestedAttrVectorRetrieval/build/asi search --data $D --index $D/index/asi.bin \
  --efs 10,40,160,320 --reps-topL 32

python3 codes/NestedAttrVectorRetrieval/scripts/run_experiments.py --data $D --out researches/nested-attr-vector-search/2026-11-09-exps --methods asi,nodeann,acorn
python3 codes/NestedAttrVectorRetrieval/scripts/plot_pareto.py \
  --measurements researches/nested-attr-vector-search/2026-11-09-exps/data/measurements.jsonl \
  --out researches/nested-attr-vector-search/2026-11-09-exps/figures/pareto.svg
```

当日数字与数据文件见 [2026-11-09-exps/report.md](2026-11-09-exps/report.md)。
