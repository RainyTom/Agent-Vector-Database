# 层级属性向量检索：Exact/Subtree 双语义与 Laminar Edge Sharing（研究进展，2026-10-20）

## 0. 论文与图表

- 系统稿：[paper.md](paper.md)（7 节、8+2 图、全部数值标注 已测/估算/待测）。
- 审稿记录：[paper_review.md](paper_review.md)（术语、定理、实验主张的逐项审计）。
- 图：[figures/](figures/)；生成脚本 `production/nested-attr-vector-search/src/plot_paper_figures.py`、`plot_architecture.py`、`measure_act_delta.py`、`measure_subtree_radius.py`。
- 新增实测：激活数组游程编码 8.8×（`data/act-delta-stats.txt`）；子树覆盖半径比（`data/subtree-radius.csv`）；查询延迟随可行集规模（`data/latency-vs-scope.csv`）；Exact 图搜索对照（37/45 QPS vs 扫描 191,914，违规率 7.4%→0）。

本目录是本方向的研究摘要与导航。它描述当前问题定义、Laminar Edge Sharing（LES）方案、已测结果与局限，并链接到逐日实验记录、基线调研与评测协议。

## 1. 问题定义

对象带多个层级标签（如 ACM CCS），标签沿祖先继承。查询有两种模式：

- **Subtree（递归）**：在指定节点 $u$ 的整棵子树中检索，可行集 $\mathcal D_u=\{i\mid L_i\cap\operatorname{Desc}^{+}(u)\neq\varnothing\}$；
- **Exact（非递归）**：只检索直接标注于 $u$ 的对象，可行集 $\mathcal B_u=\{i\mid u\in L_i\}$。

在直接标签表示下，两者都是 overlap 过滤（Subtree 用后代标签集，Exact 用单标签），因此本文不主张“多标签过滤无法表达层级语义”。真正的问题在执行：直接标签编码让 Subtree 需要展开后代标签并做多索引合并；祖先闭包编码让 Exact 退化为大规模差集；过滤图搜索仍需在遍历中判定谓词；双语义逐作用域物化使索引数量与空间翻倍。研究问题：能否用一套共享邻接同时服务两种模式，在保持过滤精确的同时降低空间与执行开销，并给出可验证的分层搜索语义。

## 2. 方案：Laminar Edge Sharing（LES）

LES 的目标是用一套按最小激活节点去重的 level-0 邻接结构，同时服务 Exact 与 Subtree 两种查询，避免双语义逐作用域物化的空间翻倍与多索引合并。

### 2.1 动机与观察

逐作用域物化（B4）对每个非空作用域维护独立图，空间随作用域数量与标签重复线性增长；过滤 HNSW 保留单一图但遍历中判定谓词，Subtree 需要展开后代标签并合并多个搜索；直接标签与祖先闭包两种编码各有偏科。我们的观察是：**层级是层状（laminar）的，同一条边被多个作用域重复使用时，其“最小激活节点”在层级上有明确归属**。因此可以把边按归属存一次，查询按归属区间取回。

### 2.2 最小激活节点

对一条无向边 $(i,j)$，定义其最小激活节点为对象 $i,j$ 激活标签集的交集中的极小元：

- Exact 语义下，激活是共同直接标签 $L_i\cap L_j$，极小元是其中不被其他共同直接标签祖先覆盖的标签；
- Subtree 语义下，激活是共同可行祖先 $(\mathrm{Anc}^{+}(L_i)\cap\mathrm{Anc}^{+}(L_j))$ 的极小元。

由于层级是树/森林，每条边的最小激活节点唯一（或在 DAG 下取 DFS 序最小的极小元）。**一条边只存一次**，归属到其最小激活节点。

### 2.3 两段式桶

对象的 level-0 邻接表按 DFS 先序排序，同一节点下分两个桶：

- **直属桶 $B_{\text{exact}}(v)$**：最小激活节点 $=v$ 的边；Exact$(v)$ 只读这段；
- **跨子树桶 $B_{\text{cross}}(v)$**：最小激活节点 $=$ $v$ 的某个后代的边；Subtree$(v)$ 读整段 $[\mathrm{tin}(v),\mathrm{tout}(v)]$（含所有后代的 $B_{\text{exact}}$ 与 $B_{\text{cross}}$）。

DFS 先序把子树映射为连续区间，Subtree 查询变为一次区间切片，热路径无需谓词判定。antichain 查询按节点分别执行后合并。

### 2.4 上界层与搜索语义

**LES v1 只共享 level-0 边**。上界层与入口按作用域保留：搜索在作用域自己的上界结构中下降到 level-0，再在共享 level-0 并图上按区间切片搜索。这样避免跨图合并层的非法问题（不同图的高层层高分布不一致，直接拼接会破坏 HNSW 的分层不变量）。

### 2.5 构建与查询执行

**构建**：对每个作用域构建 HNSW（或任一图），收集 level-0 边，按最小激活节点去重，按 DFS 序组织成两段桶，写回共享文件。重复边只保留一份。

**查询**：
- Exact$(u)$：定位 $u$ 的直属段，在该段上运行标准 best-first 搜索（HNSW level-0 搜索）；
- Subtree$(u)$：定位 $[\mathrm{tin}(u),\mathrm{tout}(u)]$ 区间，在该区间上运行 best-first；
- antichain：对每个节点分别执行后合并，跨搜索用 generation 计数实现 O(1) visited。

### 2.6 正确性

两条引理与一条命题：

1. **直属段切片 = Exact 作用域边集**：$v$ 的 $B_{\text{exact}}$ 恰好是 Exact$(v)$ 所需的 level-0 边；
2. **子树区间切片 = Subtree 作用域边集**：$[\mathrm{tin}(v),\mathrm{tout}(v)]$ 恰好是 Subtree$(v)$ 所需的 level-0 边；
3. **切片包含 B4 的全部 level-0 边**：任何作用域的 level-0 边都归属到其最小激活节点，因此切片不遗漏。

### 2.7 空间与度数分析

85 节点合成层级下：

- 逐作用域物化 level-0 有 7,602,554 条观测有向边；LES 去重后 3,658,543 条唯一边（重复 51.9%）；
- 两段桶条目 3,822,238（1,966,214 直属 + 1,856,024 跨子树），多重度 1.045；
- 仅 level-0 条目与共享向量估算约 33 MB；
- 代价：根作用域平均切片出度从 19.4 升到 36.6（并图更密）。

### 2.8 与最接近前序的差异

LES 的候选增量是“两段桶 + 区间切片 + level-0 共享”的组合：iRangeGraph 是单调二维标签的范围图，未处理层级双模式；UNG/LSSG 按标签建图并保留完整图结构，未按最小激活节点去重；JAG/相邻工作按标签共享部分边但未区分直属/跨子树，也未给出前序区间执行。**该组合是否有独立贡献尚未确认**，需要逐机制对照与外部检索后才能下结论。

## 3. 已测结果（当前配置，非算法定论）

### 3.1 主基准：Subtree 合成工作负载（85 节点）

| 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|
| B0 精确扫描（Subtree） | ≈1,400 | 1.0 | ms 级 | 64 MB | 0 |
| B1/B2 全局 HNSW + 过滤/扫描 | 3,104 | 0.9987 | 0.83 ms | 64 MB | 16 s |
| **B4 逐作用域物化（仅 Subtree）** | **94,698** | 0.9570 | 20.9 µs | 83 MB | 30.8 s |

### 3.2 CCS 形状工作负载（1365 节点，Subtree + Exact）

| 模式 | 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---|---:|---:|---:|---:|---:|
| Subtree | B4 逐作用域物化 | **74,706** | 0.9641 | 28.4 µs | 194 MB | 57 s |
| Exact | B0 直接标签倒排 + 精确扫描 | **191,914** | 1.0000 | 11.3 µs | 51 MB | 0 |

Exact 作用域很小（p50/p95/max = 126/396/6,973），**为 Exact 物化图在此 workload 上没有收益**；LES 的直属桶必须先与扫描比较。

### 3.3 Filtered-HNSW / ACORN 对照（同 1000 条 Subtree 查询）

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|---:|
| B0 Subtree 精确扫描 | – | 3,008 | 1.0000 | 1.16 ms | 51 MB | 0 |
| B4 逐作用域物化 | 21 | **74,706** | 0.9641 | 28 µs | 194 MB | 57 s（单线程） |
| Filtered-HNSW（FAISS HNSW + 位图 selector） | 384 | **1,282** | **0.9524** | 1.07 ms | 75 MB | 4.8 s（多线程） |
| Filtered-HNSW | 512 | 991 | 0.9642 | 1.53 ms | 75 MB | 5.0 s |
| ACORN（占位 metadata，filter-agnostic） | 1024 | 394 | 0.8479 | 3.85 ms | 70 MB | 0.8 s |
| ACORN（主标签 metadata，单标签编码） | 512 | 620 | 0.8395 | 2.48 ms | 70 MB | 0.9 s |

同召回下 B4 约为 Filtered-HNSW 的 75×、精确扫描的 25×，代价是 2.6× 索引字节；构建线程与内存预算尚未完全对齐，ACORN 变体未达到目标召回，故为**当前配置下的基线结果**。

### 3.4 LES 原型（仅 Subtree）

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|---:|
| LES 原型 | 32 | 15,694 | 0.9967 | 0.17 ms | 104 MB | 4.5 s |
| LES 原型 | 48 | 11,908 | 0.9992 | 0.23 ms | 104 MB | 4.5 s |
| LES 原型 | 64 | 9,940 | 0.9999 | 0.23 ms | 104 MB | 4.5 s |

同召回下 LES 约为 B4 的 0.21× 吞吐、Filtered-HNSW 的 8–12×；并图度数更高、召回更高，吞吐/召回曲线需按各自 ef 分别对齐后再比较。

### 3.5 两项创新的实测负结果

- **距离序邻接 + 三角不等式剪枝**：精确（Recall 不变），但 `PRUNED 0/5.7M = 0.00%`；HNSW 图的邻居贴近 source，界几乎不触发。
- **Exact 用共享 direct-direct 边**：中高层（深度 1–2）direct 对象只有 **73–90%** 能被 direct-direct 边连到；beam 漏掉孤立 direct 对象。**Exact 是节点集合问题，共享边不能替代 direct 标签倒排**。

### 3.6 五个补充负载：LES 省空间但不省时间

SIFT 25k/50k/100k、MNIST 25k、Fashion-MNIST 25k（合成 1365 节点层级，统一 1000 条 Subtree 查询）：

- LES 目录为 PSG 的 **0.62×–0.18×**，但同召回下 PSG 快 **1.65–3.59×**（sift100k：0.980 召回，PSG 51,904 QPS vs LES 18,142 QPS）；按 r95 门槛，sift100k PSG 74,304 vs LES 25,179。
- 度数裁剪：全局 top-24 把 Recall 打到 0.40（710/1000 填满）；每桶 top-16 保住 0.989 召回、r95 提到 29,973（+19%），仍只有 PSG 的 0.40×。差距来自跨作用域并图的局部性，不只是度数。
- 空间构成：PSG 索引约 147 MB 是逐作用域重复的 u8 向量。共享向量 PSG（M=16）为 **62.3 MB**；同会话重测 r95 69,244 QPS、r98 41,907 QPS（早先单独运行得到 53,256/36,180，跨 run 波动约 30%，比值只用同会话配对）。
- LES-v3（E0 + per-run varint + 自适应 beam）：索引 **53.3 MB**；严格 7-block 中位数 r95 40,687、r98 25,589。同空间 PSG-M12（53.6 MB）r95 34,506、r98 30,289；约 0.963 召回 LES 34,699 vs PSG 32,707。**LES 在中等召回/r95 门槛不落后，r98 慢 1.18×，不再是被严格支配的方案。** 全量 S1 字典 82.99 MB、唯一率 0.985，按门槛否决。
- 全部稠密语料（11 个，n=25k，统一 M=16）：LES-v2 在 **glove100（1.29×，空间 0.94×）、glove200（1.90×，空间 0.77×）** 反超；nytimes 上 PSG 到 ef=512 也只有 0.891 召回，LES 达标；lastfm 在 r98 为 1.17×。其余 8 个落后，**所有原生 L2 语料全败**。8 个非原生度量语料为 min–max 量化代理，不是原生任务。
- TRIEHI 目录层 + **同一** BF-HNSW 图：Recall 逐位相同，QPS 变化 −3.7%～+8.6%，元数据 4.27–17.07 MB。候选解析不是瓶颈，Directory-Aware 与 LES 作用于不同层。

原始数据：`2026-10-20-exps/data/five_l2_20261020T104139Z-68414a0f.csv` 与 `five_l2_sweep_20261020T104139Z-68414a0f.csv`；记录见 [five-l2-benchmark.md](2026-10-20-exps/experiments/five-l2-benchmark.md)。

## 4. 局限与开放问题

- **分层语义未完全解决**：LES v1 只共享 level-0；共享上界层需要 S1（全局层高）、S2（作用域层级资格）或 S3（重新定义共享图）之一并重新证明不变量。
- **双语义未完成评测**：Exact 工作负载、空/小作用域回退、两段式查询与 LES 原型尚未全部实现；33 MB 只是 level-0 条目估算，不能与双语义 B4 直接比较。
- **假设 H1 未验证**：标签位置与论文体裁的关系未知；Exact 的定义仅为直接标注，不能反推综述。
- **新颖性未确认**：LES 与 iRangeGraph、UNG、LSSG、JAG 在目标上相近；需逐机制对照后才能判断“两段桶 + 区间切片 + level-0 共享”是否有独立贡献。
- **规模与更新**：仅 100k、静态、单线程；1M 与目录调整的重建成本未测；距离限平方 L2。

## 5. 导航

当前下一轮：[同空间证据补齐与作用域 ef 预算策略](2026-10-20-exps/experiments/remote-round2-budget-policy.md)。E0已由远端实现，S1全量列表字典关闭；M12尚缺已入库的配对逐轮记录，计时边界仍需统一。优先验证B1预算表，anchor按诊断条件启动。

下一轮远端执行入口：[LES-v3 实验指导](2026-10-20-exps/experiments/remote-next-round.md)。状态：待执行；先校正严格 recall 门槛、查询调度与计时口径，再做成本归因和相近空间对照。

方案改进：[实现规格与取舍](2026-10-20-exps/experiments/les-improvement-plan.md)。优先搜索内存复用与保留作用域邻接的共享表示；按条件验证并集补充搜索或多入口，均为待实现假设。

| 内容 | 位置 |
|---|---|
| 逐日实验记录与数据 | [2026-10-20-exps/](2026-10-20-exps/) |
| 基线改造评估（ACORN / Filtered-HNSW / LSSG / iRangeGraph / PathSeer） | [baseline-adaptation.md](baseline-adaptation.md) |
| 评测协议 | [benchmark-protocol.md](benchmark-protocol.md) |
| Directory-Aware / TrieHI 基线评估 | [2026-10-20-exps/experiments/directory-aware-baseline.md](2026-10-20-exps/experiments/directory-aware-baseline.md) |
| Directory-Aware DSQ/DSM 复现记录 | [2026-10-20-exps/experiments/directory-aware-reproduction.md](2026-10-20-exps/experiments/directory-aware-reproduction.md) |
| LES 原型实现记录 | [2026-10-20-exps/experiments/les-prototype.md](2026-10-20-exps/experiments/les-prototype.md) |
| Exact 缺口与两项创新验证 | [2026-10-20-exps/experiments/exact-gap.md](2026-10-20-exps/experiments/exact-gap.md) |
| 五个补充负载（固定工作点与同召回） | [2026-10-20-exps/experiments/five-l2-benchmark.md](2026-10-20-exps/experiments/five-l2-benchmark.md) |

## 6. 复现

```bash
bash autoresearch.sh
# LES 静态分析
g++ -O2 -std=c++17 production/nested-attr-vector-search/src/les_analysis.cpp -o /tmp/les_analysis
/tmp/les_analysis --meta production/nested-attr-vector-search/work/fixtures/meta.txt \
  --tree production/nested-attr-vector-search/work/fixtures/tree.txt \
  --labels production/nested-attr-vector-search/work/fixtures/labels.bin \
  --index-dir production/nested-attr-vector-search/work/index
```

代码：`production/nested-attr-vector-search/src/{prep.cpp,prep_ccs.py,export_five_l2.py,baseline.cpp,les_analysis.cpp,les_build.cpp,les_query.cpp,dir_representations.cpp,dir_dsm.cpp,faiss_filtered.cpp,to_pathseer.py,run_five_l2.py,run_five_l2_sweep.py,analyze_five_l2.py,plot_five_l2.py,shrink_psg_vectors.py,eval_ccs.py,harness.py}`。
