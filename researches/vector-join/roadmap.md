# Vector Join 学习与实验路线

## 第一阶段：建立共同问题定义

1. 阅读 SimJoin（DOI `10.1145/3725403`）第 2–6 节，掌握 join window、window sliding 与 MST order selection。
2. 阅读 `2603.16360` 第 1–4 节，理解 hard/soft work sharing、merged index 与 OOD failure，相对 SimJoin 逐项记录改动。
3. 阅读 `1510.02824` 的问题定义与主要定理，理解“高维 + 强近似保证”为什么难以普遍次二次化。
4. 为后续实验固定输出语义：距离/相似度、阈值边界、是否排除 self-pair、是否去除对称重复、近似质量指标。

## 第二阶段：复现实验基线

按由简到难的顺序实现或接入：

1. CPU blocked brute-force，作为 exact ground truth。
2. GPU matrix/tiled brute-force，分离 distance computation 与 result materialization。
3. per-query ANN/range-search nested loop，作为“把 ANN 直接用于 join”的基线。
4. SimJoin：先实现 join-window reuse，再实现 MST order selection；这是后续图式方法的主基线。
5. Xling 风格 query filter。
6. soft work sharing / merged index，并逐项对 SimJoin 消融。
7. SSD bucket scheduling。

每个基线统一记录 build time、peak memory、distance computations、candidate pairs、verified pairs、output pairs、pair recall、wall time 和 I/O bytes。

## 第三阶段：先做估计器，再做优化器

第一版估计器输入建议为：数据规模、维度、阈值、抽样距离分布、局部密度直方图、query/data 分布偏移和内存预算；输出为 join size、候选规模与执行代价的区间估计。

先只在现有 SIFT 子集上验证从 25K、50K、100K 到 1M 的缩放趋势，再补充至少一个文本 cosine embedding 数据集和一个 OOD query 数据集。仅使用 SIFT 难以验证 Xling 的迁移假设或 merged-index 的 OOD 行为。

## 第四阶段：形成研究问题

建议用以下消融矩阵约束课题：

| 维度 | 取值 |
|---|---|
| Join 类型 | self / cross |
| 度量 | L2 / cosine / inner product |
| 输出密度 | sparse / medium / dense |
| 数据关系 | ID / shifted / OOD |
| 存储 | RAM / GPU / SSD |
| 质量目标 | exact / 0.90 / 0.95 / 0.99 pair recall |
| 更新 | static / append-only / insert-delete |

成功标准不是单一 workload 上最快，而是在给定内存和 recall 约束下，计划选择的 regret 接近 oracle，并能解释错误来自基数估计、候选生成还是结果物化。

