# NestedAttrVectorRetrieval：方案说明与观测面板

本文只描述**当前正在执行的方案**：它怎么建、怎么查、有哪些参数和开关、跑出来是什么、下一步能拧哪个旋钮。不写理论推导、不写代码审查、不写文献与新颖性论证。

## 1. 方案与输出

在层级属性 DAG（多父、OR 闭包）上做向量 top-k 检索。对象有唯一直属属性；查询给定属性集合 $U$ 与向量 $q$，在闭包 $D=\mathrm{Desc}^{+}(U)$ 的合格对象中按距离返回 $k$ 个。

与基线 NodeANN 的区别：NodeANN 对 $D$ 中每个非空属性各搜一次再归并；本方案把这些属性图接成**一张全局对象图**，整条查询只用**一份搜索状态、一份结果保留集**。

## 2. 工作负载

| 项 | 值 |
|---|---|
| 数据集 | `datasets/sift1m/`（SIFT1M 原始向量 + 基于 CCS 层级构造的属性） |
| 对象 | 1,000,000（`base/base.bin`） |
| 属性 | 1,929 个 CCS 节点，2,040 条父子边，1,516 个叶子，最长 6 层；无多父 |
| 对象归属 | 每个对象挂到一个叶子属性；叶子权重 Zipf(α=0.8)，叶子规模中位 291、最大 58,116 |
| 查询 | 200 条，按递归深度分层生成（深度 0–6 各 36/35/35/35/35/20/4 条） |
| 查询规模 | 合格对象 $\lvert E(U)\rvert$ 中位 6,844、均值 41,487、最大 1,000,000 |
| 距离 | 用 SIFT 原始 float32（无量化误差）；真值为闭包内精确 FP32 top-10 |
| 参数 | $k=10$，$M=16$，$efC=200$，$K=8$，$p=50$，$ef_2=16$ |

目录约定：原始语料在 `raw/`；任务数据在 `ccs-attr/`（`edges.tsv`、`nodes.tsv`、`objects.txt`、`queries.tsv`、`queries.bin`、`gt_10.txt`、`meta.json`）；向量在 `base/`。程序用 `--dir datasets/sift1m/base` 读向量、`--work datasets/sift1m/ccs-attr` 读任务数据，两者分开，便于其他研究问题复用同一份 1M 向量。

## 3. 索引构建

对每个非空直属属性 $S_a$：

1. **属性内 HNSW**：用 hnswlib 构建完整多层图，插入顺序为对象 ID 递增，随机种子 `100 + concept_id`（与 NodeANN 一致）。$M=16$，$efConstruction=200$。
2. **导出 layer 0** 为有向邻接表，作为全局图的局部边。原样保留，不补无向边。
3. **导出上层邻接**为紧凑数组（节点→行→邻居），仅供 `--entry-mode hnsw` 选入口，查询的展开仍在 layer 0。
4. **代表点与 MST**：farthest-first 选代表点，数量 $K_a=\max(1,\min(K_{cap},\lfloor n_a/p\rfloor+1))$；同一属性的代表点之间加双向 MST 边。
5. **跨属性网关**：按逆拓扑序，把父属性代表点连到子属性中距其最近的对象，边双向冻结；`repair` 给零入边的代表点连一个非空祖先。
6. **闭包位图**：为完整 DAG（含无对象的中间属性）计算后代位图。

关键事实：上层与属性层级是两张不同的图；全局图出度不设硬上限；repair 不保证任意闭包内对象级连通。

## 4. 查询流程（DQS）

```
D ← OR(desc[u] for u in U)
inD ← D 中有直属对象的属性
建 q1.cand/keep、q2.cand/keep 与三个查询内代号数组

offer(v):
    已带本查询代号 → 返回
    写首次接触代号
    own(v) ∉ D → 返回（不算距离、不入队、不展开）
    d ← 种子分数缓存命中 ? 复用 : 计算
    q1: keep 满且 d ≥ 最差 → 丢弃；否则写入 q1.cand 与 q1.keep
    v 是代表点或网关 → q2 同样独立尝试
    任一通道成功 → 记 entered

对 inD 每属性：按 entry-mode 选一个种子（representative: 最近代表点；hnsw: 上层贪心下降），offer 它
当任一 cand 非空：
    弹出两堆中较近的堆头
    该节点本查询已展开 → 继续弹
    标记已展开，扫描 layer0 邻接与跨属性边，对每个目标 offer
输出 ← 合并 q1.keep 与 q2.keep，按对象 ID 去重，按距离排序取 top-k
```

已做的两处保结果工作削减：同一对象只展开一次（弹出后按代号跳过）；代表点/种子评分在查询内缓存复用（不设首次接触标记）。

**已移除第二通道与 `ef2`。** 现在只有一条 q1 通道（容量 `ef`），结果只来自它。代表点走独立队列：闭包内全部代表点评分后取最近 L 个，逐个强制展开，展开得到的邻居再进入 q1。`L=0` 时退回“每个非空属性选一个最近代表点作种子”的旧入口方式。

可选 `--rerank-fp32`：对最后保留的候选按原始 FP32 重排，重排距离计入查询成本；`--rerank-pool` 限制重排条数。默认关闭。

## 5. 参数与开关

| 参数 | 常用值 | 作用 |
|---|---|---|
| `--k` | 10 | 返回条数、真值口径 |
| `--M` / `--efC` | 16 / 200 | 属性内 HNSW 的度数上限与构建宽度 |
| `--K` | 8 | 代表点数量上限 $K_{cap}$ |
| `--p` | 50 | 代表点密度，值越小代表点越多 |
| `--reps-topL` | 32 | 代表点独立队列宽度：闭包内全部代表点取距离最近 L 个，逐个强制展开；0 = 关闭，退回每属性一个种子 |
| `--efs` | `10,40,200,800` | 主通道（唯一通道）keep 容量，可一次跑多档 |
| `--entry-mode` | `representative` | 种子入口：`representative` 或 `hnsw` |
| `--rerank-fp32` / `--rerank-pool` | 关 / 0 | 最终 FP32 重排与重排预算 |
| `--queries` / `--repeat` | 200 / 1–100 | 查询前缀长度与重复轮数 |
| `--dir` / `--work` | `datasets/sift1m/base` / `datasets/sift1m/ccs-attr` | 向量目录 / 任务数据目录 |
| `--vec` | `base.bin` | 搜索向量文件名 |
| `--trace-file` | — | 写逐查询诊断 JSONL |
| `--graph-audit-file` | — | 导出原生 HNSW 拓扑，用于与 NodeANN 逐项对账 |
| `--diag` | 关 | 打印属性级 BFS 可达性 |

注意：`ef` 同时控制**保留集容量**和**队列宽度**，两者目前不能分开调。

## 6. 输出字段

单行 JSON，每档 `ef` 一条：

| 字段 | 含义 |
|---|---|
| `recall` | 对原始 FP32 GT 的严格 ID 命中率，按有效 GT 条目聚合 |
| `qps` | 计时区间内查询数/查询秒（不含建图、GT 计分、参考扫描） |
| `dists_per_q` | 实际距离调用/查询，含种子与高层定位 |
| `adjacency_slots_per_q` | 实际扫描的邻接槽位/查询，含高层 |
| `expanded_per_q` | 实际展开节点数/查询（已去重） |
| `upper_dists_per_q` / `upper_slots_per_q` | 高层入口的花费，是上面两项的子集 |
| `attrs_in_D` / `attrs_entered` / `attrs_touched` | 闭包非空属性数 / 至少准入一个对象的属性数 / 至少算过距离的属性数 |
| `miss_attr_not_entered` 等四个 `miss_*` | 未命中按阶段的操作性归因（互斥、按优先序） |
| `distance_cache_hits_per_q` | 复用种子分数省下的距离 |
| `rerank_calls_per_q` | FP32 重排距离数 |

trace 文件额外给出逐查询的 `returned`、`evaluated`、`admitted`、`seed_evaluated`、`seed_nodes`、`expanded` 与实体计数，可离线复核任意结论。

## 7. 当前实测工作点

单进程、单轮（`--repeat 1`）、CPU 2、$k=10$，未做多进程置信区间：

| Ours 代表点入口 | Recall@10 | QPS | 距离/查询 |
|---|---:|---:|---:|
| `ef=10` | 0.4625 | 2,430 | 1,218 |
| `ef=40` | 0.5850 | — | — |
| `ef=200` | 0.7400 | — | — |
| `ef=800` | 0.8645 | — | — |

| NodeANN | Recall@10 | QPS | 距离/查询 |
|---|---:|---:|---:|
| `N=10` | 0.9665 | 524 | 9,665 |

当前格局（一行结论）：**代表点强制展开把召回从 `ef=200` 的 0.7365 提到 0.8820；仍低于 NodeANN `N=10` 的 0.9665，但距离少 44%（5,418 vs 9,665）。** 剩余损失约一半来自“部分属性完全没被展开”，一半来自“展开了但没遍历到”。

上一轮 168,937 对象的 CCS+SIFT 实验因对象规模与闭包结构不同，结论已作废，不再引用。

## 8. 可调方向与当前判断

| 方向 | 状态 |
|---|---|
| 代表点队列宽度 `L` | 已测：`ef=200` 时 L=0→128 召回 0.7365→0.8820；收益随 `ef` 放大 |
| 代表点选取兼顾属性覆盖 | 未实现；当前纯按距离取 L，导致约一半损失是“属性完全没被展开” |
| 提高 `ef` | 已测 10→200：召回 0.44→0.74（L=0）或 0.58→0.88（L=128） |
| 高层入口 | 已实现，可与代表点队列叠加；新数据集未测 |
| `ef2` / 第二通道 | 已移除，不再使用 |
| 最终 FP32 重排 | 已实现，可选；新数据集无量化误差，重排无意义，默认关闭 |

## 9. 运行与观测

```bash
./codes/NestedAttrVectorRetrieval/build.sh
./codes/NodeANN/build.sh
D=datasets/sift1m
./codes/NestedAttrVectorRetrieval/build/attr_skeleton --dir $D/base --work $D/ccs-attr --vec base.bin \
  --k 10 --efs 10,40,200,800 --repeat 1
./codes/NodeANN/build/nodeann_measured --dir $D/base --work $D/ccs-attr --vec base.bin \
  --k 10 --ef 10 --N 10
```

数据集由 `codes/NestedAttrVectorRetrieval/scripts/build_ccs_sift_dataset.py` 生成（`--alpha 0.8 --seed 20261109`），参数与统计见 `datasets/sift1m/ccs-attr/meta.json`。当日数字见 [2026-11-09-exps/report.md](2026-11-09-exps/report.md)。