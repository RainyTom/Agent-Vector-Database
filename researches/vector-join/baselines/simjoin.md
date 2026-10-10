# SimJoin 基线笔记

## 书目信息

- Jiadong Xie, Jeffrey Xu Yu, Yingfan Liu
- *Fast Approximate Similarity Join in Vector Databases*
- SIGMOD / Proceedings of the ACM on Management of Data, 2025
- DOI: `10.1145/3725403`
- 本地论文：[`../../../papers/vector-join/10.1145-3725403-simjoin/paper.md`](../../../papers/vector-join/10.1145-3725403-simjoin/paper.md)

## 为什么是主基线

SimJoin 明确从“把每个 query 当作独立 range search”的 selection-based 范式，转向利用 join 批处理内部的结果相关性。2026 年的 offline-online co-design 工作直接把它视为 hard work sharing 基线：SimJoin 复用最终 join window，后续方法进一步复用搜索路径上的中间状态。

因此，图式 vector join 的实验至少应包含：

1. 独立 range search / index nested loop；
2. SimJoin hard window reuse；
3. SimJoin + MST order；
4. soft work sharing；
5. merged index；
6. OOD-aware hybrid traversal。

## 问题与指标

对两个 $d$ 维集合 $X,Y$，$\epsilon$-similarity join 输出所有满足 $\delta(x_i,y_j)\leq\epsilon$ 的 pair。对每个 $x_i$，其精确 join window 为

$$J_i=\{y\in Y\mid\delta(x_i,y)\leq\epsilon\}.$$

论文使用 per-query average recall 和 average precision，而非只使用全局 pair recall。由于候选最终经过精确距离验证，实验中的 precision 为 1。复现时应同时报告 per-query average recall 与 global pair recall，防止高基数 query 掩盖低基数 query 的失败。

## 算法拆解

### Join window sliding

- 在 $Y$ 上建立 proximity graph $G_Y$，把它当作高维 Delaunay/adjacency graph 的近似。
- 已知父 query $\kappa_i$ 的窗口 $J_{\kappa_i}$ 后，用其中节点作为多起点搜索 $x_i$ 的 $J_i$。
- 若两个窗口可能不相交，在复用父窗口与从固定入口 $y_0$ 开始之间选择更有希望的起点。
- 发现阈值内节点后继续扩展图邻居；最终仍以精确距离决定是否输出 pair。

### Join window order

- 在 $X$ 上建立 proximity graph $G_X$。
- 用 query 间距离近似窗口滑动成本。
- 总 join cost 对应加入固定入口 $y_0$ 后的加权图 MST 成本。
- 离线计算 $G_X$ 的 MST，在线插入 $y_0$ 相关边并更新 MST，再以 DFS 顺序执行窗口滑动。
- 论文定理的最优性条件是：给定图为连通的 $\epsilon$NG，且使用论文定义的成本模型；它不是任意近似图、任意硬件代价下的无条件全局最优。

### $k$-similarity join 与维护

$k$-join 复用相同顺序，但滑动阶段用大小上限为 $w$ 的优先队列维护候选并返回前 $k$ 个。对于增量插入，先对 delta 集合 $X$ 与主集合 $Y$ 做 $k$-similarity join，补齐跨集合候选边后再执行目标图索引的 pruning；删除采用 lazy marking 和批量修复受影响入邻居。

## 实验基准

- 数据：Rand、Gauss、Msong、GIST、GloVe、Crawl、SIFT 10M、Higgs 11M。
- 方法：XJoin、VBASE、FGF-Hilbert、SimJoin。
- 图：主要使用 NSG，并比较 HNSW、Vamana。
- 环境：单线程 join，C++11，`g++ 11.4.1 -O3`，Xeon Silver 4215，128 GB RAM。
- 代表结果：相对 VBASE，self-join 最高 17.8×、cross-join 最高 14.7×；相对精确 FGF-Hilbert，最高超过 100×且平均漏失低于 1%。

## 复现风险

- 截至 2026-10-10，本轮在作者主页和作者 GitHub 未发现论文绑定的公开实现；同名 `smartdatalake/simjoin` 是集合/模糊集合 join 项目，并非本论文实现。
- MinerU 对希腊字母有少量乱码，公式与算法应以 PDF 为准。
- 论文排除了可复用索引的构建时间，却计入 FGF-Hilbert 的阈值相关预处理；复现时应分别报告 cold-start 与 amortized cost。
- HNSW 在论文实验中因长边而表现较差；不能假设任意 ANN 图都能同样近似 adjacency graph。
- join window order 的理论最优性依赖图连通性和距离代理成本，真实 distance-computation/cache cost 需要单独测量。

## 实现顺序

1. 从现有 NSG/图搜索代码抽象 `range_search(seeds, query, epsilon)`。
2. 加入 exact nested-loop ground truth 和 independent range-search baseline。
3. 实现上一窗口作为多起点的 `JoinSlide`，暂用输入顺序。
4. 在 $X$ 图上实现 MST+DFS order，并做 `reuse off/on`、`order random/NN/MST` 消融。
5. 复现 Table 3 的 SIFT 子集版本，再扩展到 1M/10M。
6. 最后对接 soft work sharing，以同一图、阈值和 recall 进行公平比较。

