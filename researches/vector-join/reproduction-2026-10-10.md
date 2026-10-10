# SimJoin / Fast AVJ 第一阶段复现记录（2026-10-10）

## 范围

本阶段完成 clean-room 核心机制复现，不宣称复现论文原始绝对时间。实现位于 [`codes/vector-join-reproduction/`](../../codes/vector-join-reproduction/)，使用 hnswlib level-0 graph 作为统一 proximity graph，使五个方法共享距离函数、图参数、验证逻辑和计时边界。

实现的对照组为：独立图 range search、SimJoin hard window sharing、Fast AVJ soft work sharing、merged index、merged index + adaptive hybrid。所有输出 pair 均经过 exact squared-L2 验证。

## 上游版本与方法证据

- hnswlib：`ca426729609b3221563047ceb269cc1213e0d376`。
- BriskFlow：`89d4aa5b387fc603346a7ce3f349fc9cc393ce26`，用于审计流式接入点；当前没有直接改动上游。
- Fast AVJ：alphaXiv 提取的 Algorithms 1–8、默认 greedy/hybrid beam 256、convergence threshold 10、OOD 规则 $d_1/d_2>1.5$。
- SimJoin：本地 PDF/Markdown 中的 WindowOrder、JoinSlide 与 MST 机制；原作者代码未公开，因此只能 clean-room 重建。

## 冒烟实验

配置：SIFT25K base、前 1000 个 queries、128 维、HNSW `M=16`、`efConstruction=200`、beam 256、单进程；ground truth 对前 100 个 queries 暴力计算。以下为单次运行，不提供统计显著性，只验证机制方向与正确性。

### squared-L2 threshold = 70,000

| 方法 | online join ms | 总距离计算 | greedy 距离计算 | pair recall | precision |
|---|---:|---:|---:|---:|---:|
| independent | 133.90 | 568,718 | 206,614 | 0.9422 | 1.0 |
| hard | 99.29 | 534,312 | 205,855 | 0.9891 | 1.0 |
| soft | 95.13 | 485,753 | 99,776 | 0.9828 | 1.0 |
| merged index | 84.19 | 397,122 | 22,373 | 0.9950 | 1.0 |

### squared-L2 threshold = 90,000

| 方法 | online join ms | 总距离计算 | greedy 距离计算 | pair recall | precision |
|---|---:|---:|---:|---:|---:|
| independent | 417.38 | 1,423,190 | 148,574 | 0.9603 | 1.0 |
| hard | 344.18 | 1,541,779 | 481,780 | 0.9969 | 1.0 |
| soft | 300.45 | 1,390,417 | 34,217 | 0.9936 | 1.0 |
| merged index | 254.30 | 1,330,726 | 22,373 | 0.9976 | 1.0 |
| merged + adaptive | 301.85 | 1,330,726 | 22,373 | 0.9976 | 1.0 |

SIFT 被 OOD 判定器视作 ID，`mi-adapt` 的 hybrid query 数为 0，因此它和 `mi` 返回完全相同的 pair；多出的时间来自当前未优化的 OOD 特征计算。这与论文“ID 数据上 adaptive 应退回普通 BFS”的机制一致，但提示实现应把 OOD 特征离线缓存，才能达到论文所说的近零在线开销。

## 初步解释

1. hard sharing 提高 recall 和时延，但在较密阈值下需要对整个父窗口重新计算新 query 距离，greedy 计算反而超过 independent。
2. soft sharing 只复用一个最接近的 visited data node，把 threshold 90,000 下 greedy 计算从 hard 的 481,780 降至 34,217，方向上验证了“复用搜索努力而非只复用结果”的动机。
3. merged index 把 query-data 邻接关系移到构图期，两组阈值的 greedy 计算都只有 22,373；当前实现的收益主要来自 entry discovery，而不是 expansion。
4. 阈值变大后 expansion 与结果枚举迅速占主导，和两篇论文的收益边界一致。

## 尚未完成，不能过度解释

- 目前只有 SIFT ID 数据；adaptive hybrid 的 OOD recall 收益尚未验证。
- 后端是 HNSW，不是论文主实验的 NSG；绝对时间和图连通性不能直接对表。
- 当前为单次运行，尚未做 5 次重复、置信区间、CPU pinning 与 warm-up。
- merged-index OOD 特征仍在线计算，需改为 index metadata。
- 还未复现实验中的 8 个数据集、7 档阈值和 10M scaling。
- BriskFlow 只完成接口审计；其单记录 eager API 需要扩展 micro-batch 生命周期后才能保留 MST ordering 与跨 query cache。

## 下一阶段

1. 增加 COCO/ImageNet/LAION 式 ID/OOD split，验证 $d_1/d_2$ 和 hybrid bridge。
2. 对每组运行至少 5 次，输出均值、标准差及 p50/p95 per-query latency。
3. 缓存 OOD bit 与 merged witness，将 feature computation 从 online 移至 offline。
4. 加入 NSG backend 或导入论文同参数图，区分“算法收益”和“HNSW 图性质”。
5. 在 BriskFlow 中新增 batch lifecycle，而不是把方法塞入 `ExecuteEager` 单记录接口。
