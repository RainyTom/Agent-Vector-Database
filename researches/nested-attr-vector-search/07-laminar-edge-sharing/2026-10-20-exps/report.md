# 研究日志 2026-10-20：层级 Exact/Subtree 检索与 LES

方向：`07-laminar-edge-sharing`（层级属性过滤向量检索）。本文记录当日的实验、数据与结论；长期方案见[研究草案](../README.md)，基线调研见[基线改造](../baseline-adaptation.md)，评测协议见[基准协议](../benchmark-protocol.md)。

## 0. 当日目标

1. 在 SIFT100k 上建立一个可复现的层级过滤 top-k 基准，并实现一个足够强的无查询期谓词基线；
2. 把“逐作用域物化”的工程实现与真正的索引结构区分开，提出可分析的共享邻接方案；
3. 用真实标签分布特征生成 CCS 形状的工作负载，分别测 Subtree 与 Exact；
4. 评估已有过滤 ANN 论文的代码与改造空间，优先接入有开源代码的基线。

## 1. 主基准与逐作用域物化（B4）

固定 SIFT100k、85 节点 4 叉深度 3 层级、每对象 1–3 个直接标签、1000 条混合 Subtree 查询（含 antichain）、k=10、精确 oracle、单线程。基线为“全局 HNSW + 过滤/小范围扫描”。

逐步优化（交错测量；原始表见 [`data/primary-progression.csv`](data/primary-progression.csv)）：

| 阶段 | qps | Recall@10 | 索引 | 说明 |
|---|---:|---:|---:|---|
| 全局过滤 + 扫描 | 3,104 | 0.9987 | 64 MB | 原始基线 |
| 每作用域独立图（float） | 17,683 | 0.9948 | 275 MB | 无查询期过滤 |
| + 作用域分组执行 + uint8 精确距离 | 63,223 | 0.9732 | 115 MB | 缓存局部性 + 带宽 |
| + PC1 内部 ID 布局 | 61,529 | 0.9685 | 115 MB | 地址局部性 |
| + M=24、efC=200 | 64,624 | 0.9643 | 142 MB | 图质量 |
| + 向量前置紧凑块 + 预取 | 83,934 | 0.9609 | 108 MB | 每块 226→178 B |
| + 最近 M 截断、ef=21 | **94,698** | **0.9570** | **83 MB** | 最终 B4 |

这些是**基线的工程效率**，不作为方法贡献。负结果：共享向量、变长邻接、小范围扫描、透明大页、两查询交错、多 seed、按作用域分配 ef、循环展开。

## 2. LES 结构与静态分析

提出 Laminar Edge Sharing：level-0 边按最小激活节点去重，分直属桶 $B_{\text{exact}}$ 与跨子树桶 $B_{\text{cross}}$；DFS 先序使子树成为连续区间，查询只做区间切片。为避免跨作用域合并上界层破坏 HNSW 分层不变量，LES v1 只共享 level-0，上界层与入口按作用域保留。

在 85 个 Subtree 图的 level-0 边上实测（原文：[`data/les-analysis.txt`](data/les-analysis.txt)）：

| 指标 | 数值 |
|---|---:|
| 观测有向边 / 唯一有向边 | 7,602,554 / 3,658,543（重复 51.9%） |
| 直属段 / 跨子树段 / 合计 | 1,966,214 / 1,856,024 / 3,822,238 |
| 激活多重度 | 1.045 |
| 直属成员数 | 115,847（80 节点非空） |
| 根 / 深度 1 切片平均出度 | 36.6 / 25.5–31.8（独立图 13–21） |

空间估算（仅 level-0 条目 + 共享向量）约 33 MB；不包含上界层与 Exact 专属图，因此不能与双语义 B4 的完整空间直接比较。

## 3. CCS 形状工作负载与 Exact 扫描

生成器 `production/nested-attr-vector-search/src/prep_ccs.py`：

- 1365 节点（深度 5、4 叉、1024 叶），每对象 1–5 个直接标签（均值 2.1）；
- 25% 主标签落在中间节点（假设 H1，参数化）；叶类目 Zipf(1.1) 偏斜并与 512 个向量中心相关；
- 1000 条 Subtree + 1000 条 Exact 查询以及各自 oracle；统计见 [`data/ccs-stats.json`](data/ccs-stats.json)。

同机、单线程、u8 精确距离、ef=21：

| 模式 | 方法 | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---|---:|---:|---:|---:|---:|
| Subtree | 逐作用域物化（B4） | **74,706** | 0.9641 | 28.4 µs | 194 MB | 57 s |
| Exact | 直接标签倒排 + 精确扫描（B0） | **191,914** | 1.0000 | 11.3 µs | 51 MB 向量 | 0 |

**结论：** 在该 workload 中 Exact 作用域 p50/p95/max = 126/396/6,973，精确扫描已远快于图索引；为 Exact 物化图没有收益。Subtree 作用域 p50/p95/max = 10,334/49,305/86,699，图搜索仍有价值。LES 的直属桶必须先与扫描比较。

数据：[`data/ccs-results.csv`](data/ccs-results.csv)；统计：[`data/ccs-stats.json`](data/ccs-stats.json)。

## 4. 过滤 ANN 基线的代码与改造评估

结论：Exact/Subtree 都能归约为“每查询可行位图”；ACORN 与 FAISS HNSW 的 `SearchParameters` 接受 `IDSelector*`，`IDSelectorBitmap` 直接可用。因此主要工作是把我们的标签/树转换成位图，而不是改算法。

已核验代码：ACORN、Filtered-DiskANN（本地 Rust）、Filtered-HNSW/NaviX/PathSeer（PathSeer FAISS fork）、iRangeGraph、ELI/LabelANN、RWalks、DLH；LSSG/Curator/Packing/Tag-filtered 未找到公开代码。完整表与优先级见[基线改造](../baseline-adaptation.md)。

已在本机构建 PathSeer 的 FAISS fork（`libfaiss.so`），构建修复与接口记录见 [`experiments/faiss-build.md`](experiments/faiss-build.md)。ACORN/Filtered-HNSW 的驱动尚未完成。

### 4.1 实测：Filtered-HNSW 与 ACORN（同 1000 条 Subtree 查询）

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---:|---:|---:|---:|---:|---:|
| B0 Subtree 精确扫描 | – | 3,008 | 1.0000 | 1.16 ms | 51 MB | 0 |
| B4 逐作用域物化 | 21 | **74,706** | 0.9641 | 28 µs | 194 MB | 57 s |
| Filtered-HNSW | 384 | **1,282** | **0.9524** | 1.07 ms | 75 MB | 4.8 s（多线程） |
| Filtered-HNSW | 512 | 991 | 0.9642 | 1.53 ms | 75 MB | 5.0 s |
| ACORN（占位 metadata） | 1024 | 394 | 0.8479 | 3.85 ms | 70 MB | 0.8 s |
| ACORN（主标签 metadata） | 512 | 620 | 0.8395 | 2.48 ms | 70 MB | 0.9 s |

同召回（约 0.964）下 B4 比 Filtered-HNSW 快约 75×，比精确扫描快约 25×；代价是 2.6× 索引字节。ACORN 的位图改造版无法达到 0.95 召回（最高 0.848），必须标注为未使用层级构图的变体。位图预计算 7–8 µs/query。细节与公平性说明见 [`experiments/pathseer-filtered-ann.md`](experiments/pathseer-filtered-ann.md)。

## 4.2 两项创新验证（2026-10-20）

| 创新 | 结果 | 证据 |
|---|---|---|
| 距离序邻接 + 三角不等式剪枝 | 精确但 ~0% 跳过 | PRUNED 0/5.7M，Recall 不变（0.9967@ef32） |
| Exact 用共享 direct-direct 边 | 不完整：73–90% 覆盖（中高层），漏孤立 direct 对象 | 0.922–0.927 召回；[`experiments/exact-gap.md`](experiments/exact-gap.md) |

**结论**：LES 的贡献应在 Subtree（边共享），Exact 的完整路径是节点集合；Exact 用倒排 + 扫描（191.9k qps，1.0）。

## 5. 当日结论

1. 逐作用域物化是一个强但空间昂贵的基线；紧凑存储与预取把它推到 94.7k qps（85 节点）/ 74.7k qps（1365 节点，Subtree）。
2. Exact 语义在小作用域下不需要图：精确扫描 191.9k qps、召回 1.0。双语义设计必须采用自适应执行（小作用域扫描/大作用域图）。
3. LES 的边去重可减半 level-0 存储，但会增加粗作用域的度数；端到端收益未测。
4. 合并不同作用域的上界层会破坏分层不变量；LES v1 只共享 level-0，这是必须写进论文的正确性边界。
5. 开源过滤 ANN 对照已跑通：Filtered-HNSW 在 0.9524 召回时 1,282 qps；filter-agnostic ACORN 达不到门槛。B4 的优势需要补同内存预算对照。
5. “综述直属中间节点”只是待验证假设；Exact 的定义仅为直接标注，不能反推体裁。
6. LES 原型已实现并测通：Subtree 上 15,694 qps @ 0.9967（ef=32）、索引 104 MB；比 B4 慢约 4.8×，比过滤 HNSW 快约 12×。构建修复：DFS 先序区间与 u16 激活数组。

## 6. 下一步

1. 写 FAISS 驱动，用同一批 Subtree 位图跑 ACORN 与 Filtered-HNSW，和 B4 同 oracle 比较；
2. 完善 LES：按召回曲线对齐 ef、补 Exact 直属桶与小作用域扫描回退、共享/重写上界层的开放问题；
3. 在真实 CCS/SciHTC 标签上复核 §3 的统计与结论；
4. 实现 iRangeGraph/ELI 对照。

## 7. 当日文件

- 代码：`production/nested-attr-vector-search/src/{prep.cpp,prep_ccs.py,baseline.cpp,les_analysis.cpp,eval_ccs.py,harness.py}`
- 数据：[`data/primary-progression.csv`](data/primary-progression.csv)、[`data/les-analysis.txt`](data/les-analysis.txt)、[`data/ccs-stats.json`](data/ccs-stats.json)、[`data/ccs-results.csv`](data/ccs-results.csv)
- 实验记录：[`experiments/faiss-build.md`](experiments/faiss-build.md)
- 长期文档：[研究草案](../README.md)、[基线改造](../baseline-adaptation.md)、[基准协议](../benchmark-protocol.md)

## 8. 术语与任务审计、激活 run 索引复测（追加）

**问题定义修正。** `paper.md` 中任务明确为“给定查询向量、树节点反链、Exact/Subtree 谓词的过滤 top-$k$ 向量 ANN”，非 vector join。精确 oracle 来自原始 float32 SIFT 的平方欧氏距离（`prep_ccs.py:245-254`），搜索将向量舍入到 u8；真实 CCS 数据集未用到。多标签对象的子树可行集不是 laminar family，故删除布尔区间编译定理；MAF、HALA、Hybrid Switch 等旧称谓不视为向量检索既有术语。§4 的边归属命题只证明单节点 Subtree 下有向边端点的资格，不证明搜索复杂度或邻接与每作用域基础图相同。

**设置。** H2 的已构建 1365 节点、SIFT100k、1000 个 Subtree 查询；复用原 `work/index_ccs/` 逐作用域底图。修改 `les_build.cpp` 将逐边 `act.bin` 改为每对象 `run_offsets.bin` 与每 run 的 `run_start.bin`、`run_act.bin`，`les_query.cpp` 在 run 值上二分定位邻接切片。重新编译两个 C++ 程序、构建 `work/les_ccs_rle`，查询 `ef=32,k=10,repeats=3`；旧索引文件为 `work/les_ccs`。当前脚本及绘图代码见 `production/nested-attr-vector-search/src/`；原始统计汇总见 [`data/activation-run-index.csv`](data/activation-run-index.csv)。

| 指标 | 原逐边激活 | run 索引 | 判断 |
|---|---:|---:|---|
| 桶条目 / run | 13,387,704 / 1,015,143 | 相同 | 已测 |
| 激活元数据 | 26,775,408 B | 6,490,862 B | 已测，含对象偏移和 run 起点 |
| 当前完整目录 | 144,313,905 B | 124,029,359 B | 已测，`du -sb`；与旧 CSV“104 MB”不一致 |
| Subtree ef32 QPS / Recall@10 | 历史 15,694 / 0.9967 | 本次 12,898 / 0.9967 | 已测，但不构成同次配对加速比 |
| 本次归并构建 wall time | 历史 4.5 s | 5.42 s | 已测；底图构建复用，完整构建未测 |

**验证。** 在 13,387,704 条目上逐条解码 run 并与旧 `act.bin` 比较，完全相同；本次查询 10,000/10,000 返回 id 作用域合法、每查询无重复，Recall@10=0.9967。原图诊断吞吐的 QPS 单位错误已修正：旧 `exact_edges` 的查询时间 0.02680258 s 对应 37,310 QPS，`exact_strict` 的 0.02218390 s 对应 45,078 QPS，不是 37/45 QPS；B0 Exact 的 191,914 QPS 属独立扫描基线。旧两段桶计数 3,822,238 与同一 `les-analysis.txt` 的 `bucket_entries=3,822,159` 有 79 的口径差，需重新统计。

**局限/下一步。** 对新旧激活索引做交错多次同机计时并测 cache misses；对齐 B4 与 LES 的全部索引文件组成、底图构建与归并构建时间；在多标签真实层级上复核范围归属收益。实现严格 `|F|<k` 扫描补足及 Exact 正确路径前，不能宣称完整过滤 top-$k$ 接口。

## 9. 追加：五个现有向量负载与 Directory-Aware 端到端对照

**选择。** 五个评估点：SIFT 前 25k/50k/100k（同一语料的前缀嵌套子集）、MNIST 前 25k、Fashion-MNIST 前 25k；层级与查询沿用合成的 1365 节点 4 叉深度 5 生成器（seed 20261021），不是真实层级标注。所有向量值为 [0,255] 整数，u8 与 float32 的平方 L2 排序一致。

**设置。** 每点 1000 条 Subtree 查询、k=10、单线程、2 次重复取最好。PSG M=24/efC=200/ef=21；LES ef=32（扫描 8/16/24）；BF-HNSW M=32/efC=200/ef=384（扫描 512/768）；PFS 精确扫描；PathSeer native M_exp=64、ef=384、M_exp_search=16；TRIEHI 目录层在查询计时内解析候选，复用同一份序列化 BF-HNSW 图。

**结果（原始数据 `data/five_l2_20261020T104139Z-68414a0f.csv`、`data/five_l2_sweep_20261020T104139Z-68414a0f.csv`）：**

| 数据点 | PSG ef21 | LES ef32 | BF-HNSW ef384 | PFS | TRIEHI+HNSW |
|---|---|---|---|---|---|
| sift25k | 0.9784 / 113,003 | 0.9992 / 22,569 | 0.9730 / 2,355 | 1.0 / 16,054 | 0.9730 / 2,558 |
| sift50k | 0.9714 / 95,347 | 0.9984 / 21,243 | 0.9654 / 2,191 | 1.0 / 7,829 | 0.9654 / 2,230 |
| sift100k | 0.9564 / 74,304 | 0.9970 / 14,471 | 0.9534 / 1,654 | 1.0 / 2,222 | 0.9534 / 1,644 |
| mnist25k | 0.9882 / 45,944 | 0.9996 / 6,751 | 0.9524 / 1,154 | 1.0 / 2,343 | 0.9524 / 1,112 |
| fashion-mnist25k | 0.9953 / 48,790 | 0.9995 / 7,824 | 0.9442 / 1,375 | 1.0 / 2,002 | 0.9442 / 1,456 |

同召回（五个点）：sift25k PSG ef32 0.9920 / 84,131 vs LES ef16 0.9913 / 51,123（1.65×）；sift50k 0.9878 / 69,750 vs 0.9871 / 39,037（1.79×）；sift100k 0.9801 / 51,904 vs 0.9802 / 18,142（2.86×）；sift100k 高召回侧 0.9912 / 43,096 vs 0.9925 / 13,658（3.16×）；mnist 0.9953 / 39,028 vs 0.9959 / 10,865（3.59×）；fashion 0.9953 / 48,790 vs 0.9953 / 17,811（2.74×）。r95/r98 门槛吞吐见 `data/five_l2_summary_20261020T104139Z-68414a0f.csv`。

度数裁剪诊断（sift100k）：全局 top-24 保留 2.39M 条、Recall 0.33–0.40、仅 710/1000 填满；每桶 top-8/16/24 保留 6.9M/10.6M/12.3M 条，Recall 0.978–0.997；每桶 top-16 在 r95 上 29,973 QPS（无裁剪 25,179，+19%）、索引 104.5 MB，仍为 PSG r95 的 0.40×。原始数据 `data/five_l2_les_degree_20261020T104139Z-68414a0f.csv`。

空间构成：PSG M=6/12/16 的索引为 160.5/173.2/182.0 MB，线性拟合截距约 146.8 MB、斜率 2.2 MB/M，即约 147 MB 是逐作用域重复的 u8 向量；M=6 ef32 只有 0.821 召回。进一步实现共享向量 PSG（`GRAPH_SHARE_VECTORS=1`）：M=16 索引 **62.3 MB**，ef24 0.9494/53,256、ef48 0.9840/36,180、ef64 0.9904/30,031；M=12 索引 53.6 MB，ef48 0.9717/40,321。共享 M16 与重复向量 M16 在 ef24/32 召回完全一致（0.9494/0.9673），验证布局改动不改变语义。相对 LES（124.1 MB，r95 25,179，r98 18,142），共享 M16 只占 0.50× 空间而快 2.12×/1.99×。**这是早期跨 run 比较；后文 LES-v3 的严格同空间对照已修正“被严格支配”的表述。**原始数据 `data/five_l2_psg_space_20261020T104139Z-68414a0f.csv`、`data/five_l2_psg_shared_20261020T104139Z-68414a0f.csv`。

**LES-v2 优化。** 底图 M24→16、删除 `dist16`/`dflag`（约 40 MB；三角剪枝实测 0% 命中）、按桶限 16：索引 124.1→73.6 MB（无桶限）/69.0 MB（桶限）；r95 25,179→35,084（+39%），r98 18,142→23,504（+30%）。仍落后共享 PSG（53,256/36,180，62.3 MB），差距 2.1×→约 1.5×，空间 1.11–1.18×。旧索引回归 0.9729；LES-v2 无违规、全部填满 k。数据 `data/five_l2_les_v2_20261020T104139Z-68414a0f.csv`。

**全部稠密语料（11 个，n=25k，M=16）。** SIFT/MNIST/Fashion 原生 u8 L2；glove25/50/100/200、coco-i2i/t2i、nytimes、lastfm 统一 min–max 量化到 [0,255] 后按平方 L2 评测（非原生度量）；kosarak/movielens 稀疏 jaccard 排除。PSG 与 LES 的 ef 分别扫到 512/192。结果**并非全败**：LES-v2 在 glove100（1.29×，空间 0.94×）、glove200（1.90×，空间 0.77×）反超；nytimes 上 PSG 到 ef=512 也只有 0.891 召回，LES 在 ef=64 达到 r95（10,306 QPS）；lastfm 在 r98 为 1.17×。其余 8 个 r95 落后（0.36–0.78×），**所有原生 L2 语料全败**。反超只出现在量化代理语料，需按维数/选择率定位；不足以支撑 SOTA。数据 `data/all_datasets_20261020.csv`、`data/all_datasets_ext_20261020.csv`、`data/all_datasets_20261020_summary.csv`；图 `figures/fig_all_datasets.png`。

**LES-v3：文献启发的新布局。** alphaXiv 检索到 JAG（2602.10258）、Fiber-Navigable（2604.00102）、Uemura 边标签（2607.24173）、BOA 自适应 beam（2609.16175）。落地：E0（堆复用+入口距离复用，输出逐位不变，fixed ef12 29.1→23.5 ms）、per-run delta-varint（73.6→53.3 MB，全部 run 解码一致）、按作用域基数自适应 beam（ef24 r98 +22%）。严格 7 个交错 block、original 顺序、单线程中位数（**历史诊断集**）：LES r95 40,687、r98 25,589；PSG-M12（53.6 MB）34,506/30,289；PSG-M16（62.3 MB）48,987/37,021。该表曾显示 LES 在 r95 与约 0.963 召回不落后，但**独立 test 推翻了该外推**，见下段。S1 全量邻接字典唯一率 0.985、已统计 82.99 MB（下界 88.16 MB），按空间门槛否决；预取与“小作用域走扫描”混合规划均为负结果。数据 `data/five_l2_strict_blocks_20261020.csv`、`data/five_l2_strict_summary_20261020.csv`。

**第二轮独立 test。** 生成互不重叠 fit/val/test（400/300/300；受 1000 条历史查询限制）。B1 按可行集桶校准并与 PSG 同机制比较，15 个交错 block、original 顺序、batch wall 计时。test 结果：LES policy r95 0.9553/36,712、policy r98 0.9813/24,641；fixed ef12 0.9473（未达 r95）、旧 adaptive 0.9760（未达 r98）。相近空间（53.3 vs 53.6 MB，M12 大 0.6%）下，M12 fixed40 与 LES fixed14 同为 0.958333 召回，QPS 比 1.242；相对 LES policy r95 为 1.129。M12 入库的 fixed64/policy r98 只到 0.979，未达 r98；回传 ef72 的 0.9853/23,029 缺配对记录，且低于 LES policy r98 的 24,641，故**删除“r98 快 1.21×”与“全档支配”**。B1 只在本轮留出集达到 r95/r98，且 LES policy r95 在最大桶（≥10000）召回仅 0.939，不保证每桶或新查询。B1 对 PSG-M16 的 r95 策略 test 仅 0.9453，预算表也会过拟合。数据 `data/b1_blocks_20261020.csv`、`b1_blocks_summary_20261020.json`、`b1_policies/b1_20261020/`、归档 `b1_archive_20261020_manifest.json`。

**结论。** ① 相对**重复向量**的 PSG，LES 索引更小（0.62×–0.18×），但同召回吞吐低 1.65–3.59×；本轮留出集同召回 0.9583 点 M12 快 1.24×，r98 全档支配尚无证据。B1 让 LES 聚合达标但未翻盘。② PFS 精确扫描在 100k 或 784 维降至约 2.0–2.3k QPS，图路径仍必要。③ TRIEHI 目录层与 BF-HNSW 同图同参时 Recall 逐位相同，QPS 变化 −3.7%～+8.6%（每查询时间 −8.0%～+3.8%），目录构建一次性 3.1–13.5 ms，候选解析不是瓶颈；Directory-Aware 与 LES 不在同一层，前者不能替代后者，反之亦然。④ S1 全量字典唯一率 0.985、已统计 82.99 MB（下界 88.16 MB），按门槛否决。

**局限。** 五个点只有三个独立语料；层级/查询合成；只测 Subtree 与静态负载；未对齐同内存预算与构建资源；TRIEHI 为静态解析器（无路径字符串、DSM、OpenViking），PathSeer 为固定 M_exp 配置。详见 [`experiments/five-l2-benchmark.md`](experiments/five-l2-benchmark.md)。

**代码与工具变更。** `prep_ccs.py` 增加 `--dim`；新增 `export_five_l2.py`、`run_five_l2.py`、`run_five_l2_sweep.py`、`plot_five_l2.py`；`faiss_filtered.cpp` 增加 `pathseer`、`triehi-hnsw`、`--index-in/--index-out`、`--ids/--timings`、高精度 `query_seconds`。图：`figures/fig_five_l2.png`（图 11）。

## 10. 研究侧交接审计与下一轮指导（追加；未运行新实验）

本轮目标是为 Git 同步的远端实验侧提供可执行协议，已检查 LES/PSG 查询源码及 v3 CSV；没有在本机或远端运行新性能实验，新增结果数字为“未测”。

审计勘误：历史共享 PSG ef24 recall=0.9494，不能作为严格 r95 达标点，前述 r95 0.50× 应降格为近似工作点比较；LES 34,313 QPS 对应 varint fixed ef12，不是 adaptive。r98 0.66× 仍是历史工作点比值，但查询调度、计时边界和重复统计尚未对齐。

源码显示 PSG 按 scope 分组执行而 LES 按 qi 执行；总耗时取最好轮、逐查询耗时保存最后轮，且两引擎总耗时边界不同。这些混杂因素需要先排除，不能单凭现有 QPS 把差距全部归因于并图扫描。局部 u16 映射也不会减少扫描边数。

已新增 [远端下一轮实验指导](experiments/remote-next-round.md)：P0 测量契约；P1 layout×beam 消融、严格 r95/r98、同顺序交错重复与分桶计数；P2 共享 PSG M12 相近空间对照，以及按证据决定局部编号/anchor。执行、数据回传、论文相关表格统一修订均待远端完成；本轮仅交接文档，不新增实验数据。

## 11. 追加：明确下一版方案改进（设计，未运行）

已补 [LES 改进实现规格](experiments/les-improvement-plan.md)。源码核查表明：构建器丢弃了原始 scope 来源，仅保留 activation；PSG 使用复用的 vector 堆，LES 每 scope 新建 priority_queue；当前 PSG 导出器将 level-0 限为至多 M 条。

据此推荐 E0 复用搜索内存并消除入口重复距离；S1 使用共享有序邻接字典与每 scope 引用，恢复底图逻辑邻接，先估算含 global/local 映射的完整空间再实现。若全量字典不划算，评估有固定预算的少数 scope 覆盖；R1 将并集作为可选独立重跑，A1 小规模多样入口仅按诊断证据启动。协议写明数据结构、tie规则、正确性检查、消融和停止条件。

本节是研究假设与远端开发规格，未实现新代码、未产生性能结果；不能预先宣称节省空间、恢复吞吐或具备新颖性。

## 12. 追加：远端回传审计与B1预算分配交接

**目标与设置。** 核查远端完成的E0、严格测量、S1静态分析和M12扫描。研究侧读取当前源码、strict blocks/summary两份CSV，并用Python标准库重算已有42条记录；未运行新ANN实验。

**离线结果。** 原始block表仅含LES两个配置与M16四个配置，各7轮，未包含M12与约0.963点。LES/固定M16每block比值中位数r95=0.8423、r98=0.7208；中位数之比分别0.8306/0.6912。这只是旧日志统计，仍受两引擎计时边界不同影响。数据见 [strict_evidence_audit_20261020.json](data/strict_evidence_audit_20261020.json)。

**结论修订。** 撤回笼统“严格支配”合理，但同空间1.18×/1.06×优势目前只有汇总证据，需补逐轮记录并对齐计时；不能写成已完成的M12严格配对验证。S1唯一率0.985只否决完整有序列表interning；82.99MB未计lv、upper_off及lookup等，是部署下界。仅lv+upper_off还需5,173,155B，关闭该分支的决定不变。

**下一步。** 新增 [round2指导](experiments/remote-round2-budget-policy.md)：先补M12与相近recall点的原始证据、统一计时和runner留存；再实施B1作用域特征ef预算表，以独立fit/validation/test优化总耗时下的r95/r98，给PSG-M12/M16同等调参机会。优先利用现有图和小型策略表，不将未测的JAG/fiber作为仅剩方向。A1需导航计数支持后再做。

**局限。** 本轮没有新的性能数据、独立测试结论或文献核验；B1尚未实现，不能宣称收益。论文§5.8已追加证据限制，导航与原交接文件已指向新协议。

## 13. 追加：ee90400复核、倍率勘误与收束

**目标/设置。** 快进同步ee90400，检查B1源码、split manifest、summary与225条block记录；使用Python标准库重算每配置中位数与配对block bootstrap（seed20261020，10,000次）。本轮没有运行新ANN实验。

**结果勘误（覆盖前文同类结论）。** M12 fixed40与LES fixed14均为0.958333召回，QPS中位数之比1.242；相对LES policy r95是1.129，而非1.24。M12 fixed64与policy r98都是0.979，未达r98；已入库15配置没有fixed72，回传的0.9853/23,029尚缺配对证据，且QPS低于LES policy r98的24,641。因此撤回“r98快1.21×”及“全档支配”，不是据此改称LES全档领先。

**配对统计。** M12 fixed40/LES fixed14的block比值中位数1.242、95%区间[1.049,1.455]；M12 fixed40/LES policy r95为1.150、[1.035,1.368]；M16 fixed48/LES policy r98为1.460、[1.388,1.551]。这些只反映当前条件下的计时重复，不能替代查询泛化CI。完整离线记录：[b1_closeout_audit_20261020.json](data/b1_closeout_audit_20261020.json)。

**数据边界。** manifest确认400/300/300原始行号互斥，但来自此前调参的历史1000条查询，只是本轮B1留出划分。15次重复不增加查询独立样本。B1只在本次LES上达标，不保证阈值。未有逐查询计数，不能把邻居枚举/图结构当作已证实瓶颈；本地未取得预测ids，recall仍依据远端summary，需远端哈希及逐查询摘要补齐。

**决定与下一步。** 收束当前性能优化，不启动A1/联合构图/更多ef搜索；将共享表示收益与搜索代价整理为限制分析。远端仅需补回ef72/1.21×来源、配置冻结时间和结果哈希，不为追认旧结论继续扫test。若后续重启，先做一次有界、绑核预热的计数诊断。执行规格见 [收束交接](experiments/les-closeout-ee90400.md)；论文、基准记录和导航已同步勘误，既有CSV/JSON保留。

## 14. 最终归档验收：eb13a26（未运行新实验）

研究侧已快进同步eb13a26，核查归档manifest及PROVENANCE。通过Python标准库离线检查：runs/per-query/buckets三份CSV的字节数及SHA256全部匹配；225份入库日志的SHA256全部匹配；225个(block,config)唯一、每配置15轮；4,500条block01逐查询记录的hits与每轮汇总一致，45条分桶汇总的查询数与recall可由逐查询表重算。每配置15轮ids哈希一致，报告的违规/重复/空结果均为0，每轮填满3,000槽。

最大桶（148条）LES policy r95 recall=0.939189、M12 fixed40=0.941216，均未达到该桶r95；聚合目标不能解释为逐桶保证。post-hoc ef72及1.21×的来源已明确隔离，后者来自不同recall的LES ef28与M12 ef80，不进入冻结主表。配置来源记录见 [PROVENANCE.md](experiments/b1/PROVENANCE.md)。

验收范围为归档完整性与摘要内部一致性；ids/timings、索引和二进制留在远端，本地没有重新计算距离或从预测ids复核过滤资格。哈希提供后续核对依据，不等于本地已完成二进制复验。

**本轮收束完成。** 保留已验证机制、失败案例及限制分析，不再新增实验任务；A1、联合构图和新ef扫描均未启动。只有后续明确重启时，才采用收束文档中的有界诊断协议。

## 15. 方案合理性与SOTA判断（研究侧评估，无新实验）

**结论。** LES作为Subtree filtered ANN的共享邻接表示具有合理性，作为已达到SOTA的搜索方法则证据不成立。activation区间编码的是端点资格，不是每作用域最佳导航邻接；共享存储可以成立而吞吐优势不成立。varint的无损压缩及E0执行优化已经验证；它们不能单独证明新算法新颖性。B1仅优化聚合目标，最大桶不达r95且其他方法策略有失败案例，不提供逐桶或泛化召回保证。

**性能依据。** 相近空间下，M12在0.958333同召回点比LES fixed14快1.242×；r98缺少冻结且达标的M12对照，不能宣称被其全档支配，也不能反向认定LES为r98最优。M16用更多空间在r98提供更高吞吐。0.6%的M12空间差及单seed、历史查询重划不足以建立稳健Pareto优势；Exact/antichain、真实层级与1M规模仍未完成。

**外部核对范围。** 本轮仅做定向检索与原始摘要复核，不是全领域最新SOTA普查。ACORN原始摘要明确其研究predicate-agnostic hybrid search与predicate subgraph traversal：[arXiv:2403.04871](https://arxiv.org/abs/2403.04871)。统一FANNS基准摘要强调数据依赖参数、标准化过滤与调参对可比性的影响：[arXiv:2509.07789](https://arxiv.org/abs/2509.07789)。本地未完成这些代表性方法在相同语义/预算/调参协议下的充分评测，不能使用SOTA称谓。

**当前定位。** 层级过滤ANN中的共享邻接压缩原型，以及表示收益、搜索代价和适用边界的实验分析；不是vector join SOTA，也不是已证明的filtered ANN SOTA。新颖性仍未充分排查，限制分析的研究价值不等于已达到新方法论文的贡献门槛。不重启性能路线。

## 16. 用户明确重启：以帕累托前沿为目标的参数扫描

**目标修订。** 用户明确本项目SOTA判据为效率与recall trade-off上的前沿贡献，不要求所有阈值全面领先。因此重新开启系统参数扫描；此前收束是历史状态，不能据少量r95/r98点终止曲线比较。前沿声明仍需明确比较集合、预算和负载。

**本轮已完成。** 新增参数化 `production/nested-attr-vector-search/src/plot_pareto_frontier.py`，将225条冻结B1记录转为15个实测点，按54/64MB预算计算方法前沿、联合前沿和补充三目标标记，输出PNG/PDF及points/frontiers CSV。空间采用归档索引字节加policy文件；未重测RSS。旧post-hoc点和其他会话数据不混入。

**探索结果。** 54MB下联合前沿为M12 fixed40、fixed48、policy r98与LES policy r98；64MB下联合前沿来自M12/M16，现有LES点均被支配。54MB的LES高recall点受M12最高冻结recall仅0.979的覆盖缺口影响，不能据此宣称充分调参后的优势。失败的r95策略按真实recall进入曲线，而不伪装为r95达标点。

**验证。** 运行了旧数据适配与通用CSV两种入口；检查二维支配、严格相等点保留、预算过滤和三目标空间取舍；检查图像布局。竖线为计时IQR，折线只连接离散实测非支配点，不表示中间点已测。图：[PNG](../figures/pareto_b1_observed_20261020.png)、[PDF](../figures/pareto_b1_observed_20261020.pdf)；数据：[points](../figures/pareto_b1_observed_20261020.points.csv)、[frontiers](../figures/pareto_b1_observed_20261020.frontiers.csv)。

**待远端执行。** [扫描规划](experiments/pareto-sweep-plan.md)定义M8/12/16/24/32、efC与查询ef、fixed/adaptive/B1、布局/桶裁剪的分阶段扫描；54/64/80/128MB预算，新查询calibration/validation/test、前沿邻域精化、统一绑核预热15block及多seed验证。随后扩展Filtered-HNSW、ACORN、PathSeer、PFS的公平参数曲线。未运行新性能实验，未启动A1/联合构图。
