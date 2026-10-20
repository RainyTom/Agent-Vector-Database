# 研究日志 2026-10-20：层级 Exact/Subtree 检索与 LES

方向：`07-laminar-edge-sharing`（层级属性过滤向量检索）。本文记录当日的实验、数据与结论；长期方案见[研究草案](../../README.md)，基线调研见[基线改造](../../baseline-adaptation.md)，评测协议见[基准协议](../../benchmark-protocol.md)。

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

已核验代码：ACORN、Filtered-DiskANN（本地 Rust）、Filtered-HNSW/NaviX/PathSeer（PathSeer FAISS fork）、iRangeGraph、ELI/LabelANN、RWalks、DLH；LSSG/Curator/Packing/Tag-filtered 未找到公开代码。完整表与优先级见[基线改造](../../baseline-adaptation.md)。

已在本机构建 PathSeer 的 FAISS fork（`libfaiss.so`），构建修复与接口记录见 [`experiments/faiss-build.md`](experiments/faiss-build.md)。ACORN/Filtered-HNSW 的驱动尚未完成。

## 5. 当日结论

1. 逐作用域物化是一个强但空间昂贵的基线；紧凑存储与预取把它推到 94.7k qps（85 节点）/ 74.7k qps（1365 节点，Subtree）。
2. Exact 语义在小作用域下不需要图：精确扫描 191.9k qps、召回 1.0。双语义设计必须采用自适应执行（小作用域扫描/大作用域图）。
3. LES 的边去重可减半 level-0 存储，但会增加粗作用域的度数；端到端收益未测。
4. 合并不同作用域的上界层会破坏分层不变量；LES v1 只共享 level-0，这是必须写进论文的正确性边界。
5. “综述直属中间节点”只是待验证假设；Exact 的定义仅为直接标注，不能反推体裁。

## 6. 下一步

1. 写 FAISS 驱动，用同一批 Subtree 位图跑 ACORN 与 Filtered-HNSW，和 B4 同 oracle 比较；
2. 实现 LES 构建与查询原型（含小作用域扫描回退），验证空间/吞吐边界；
3. 在真实 CCS/SciHTC 标签上复核 §3 的统计与结论；
4. 实现 iRangeGraph/ELI 对照。

## 7. 当日文件

- 代码：`production/nested-attr-vector-search/src/{prep.cpp,prep_ccs.py,baseline.cpp,les_analysis.cpp,eval_ccs.py,harness.py}`
- 数据：[`data/primary-progression.csv`](data/primary-progression.csv)、[`data/les-analysis.txt`](data/les-analysis.txt)、[`data/ccs-stats.json`](data/ccs-stats.json)、[`data/ccs-results.csv`](data/ccs-results.csv)
- 实验记录：[`experiments/faiss-build.md`](experiments/faiss-build.md)
- 长期文档：[研究草案](../../README.md)、[基线改造](../../baseline-adaptation.md)、[基准协议](../../benchmark-protocol.md)
