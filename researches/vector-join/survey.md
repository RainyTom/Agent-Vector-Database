# Vector Join：问题、代表工作与研究机会

> 深度入口：参见 [八篇论文深度综合：方案来源、演化、展望与缺陷](deep-analysis.md)。各论文目录另有与 `paper.md` 对应的 `paper.report.md`。
>
> 复现入口：参见 [SimJoin / Fast AVJ 第一阶段复现记录](reproduction-2026-10-10.md) 与 [`codes/vector-join-reproduction/`](../../codes/vector-join-reproduction/)。

## 概述

向量阈值连接（vector threshold join）给定两个向量集合 $X$ 和 $Y$、距离函数 $d$ 与阈值 $\theta$，返回

$$
J_\theta(X,Y)=\{(x,y)\mid x\in X, y\in Y, d(x,y)\leq\theta\}.
$$

使用相似度时，不等式方向相反，例如内积或余弦相似度满足 $s(x,y)\geq\tau$。当 $X=Y$ 时称 self-join。它与 top-$k$ ANN 的核心差别是：输出由阈值决定、每个查询的结果数未知，而且必须处理整批查询；因此候选规模、输出规模和跨查询工作复用往往比单次查询延迟更关键。

既有检索与本轮相关正文核对显示，现代高维 vector join 正从“为每个向量重复做 range search”转向三类系统化优化：离线—在线工作迁移、批处理中的工作共享、以及针对内存层次和数据分布的自适应过滤/调度。

## 研究版图

| 方向 | 代表论文 | 核心思想 | 主要边界 |
|---|---|---|---|
| 图上的批量工作共享 | SimJoin (`10.1145/3725403`) | 按 MST 顺序处理 query，以已有 join window 为多起点滑动到下一个 window | 只复用最终窗口；依赖 proximity graph 的邻接近似质量 |
| 内存近似跨集合 join | Fast Approximate Vector Joins (`2603.16360`) | 把同一 embedding 空间中的 query/data 合入图索引；复用中间搜索状态；对 OOD 查询启用混合遍历 | 假设共享 encoder/几何空间；当前聚焦内存图索引 |
| 单机外存 self-join | DiskJoin (`2508.18494`) | SSD 分桶、bucket graph、依赖感知调度、动态缓存与概率剪枝 | 静态批任务；重点是 self-join 和 SSD I/O |
| 学习型前置过滤 | Xling (`2402.13397`) | 学习邻居基数并跳过低产出查询，可作为 loop-based join 插件 | 允许召回损失；跨数据集迁移依赖分布相近 |
| GPU 精确 self-join | GPU-Join (`1809.09930`) | 非空网格索引、维度重排、批量查询与 CPU/GPU 流水线 | 全内存；实验维度最高 90，与现代 embedding 维度有差距 |
| 复杂性边界 | Inner Product Similarity Join (`1510.02824`) | 给出内积 join 的条件下界，并分析近似因子与次二次时间的关系 | 理论模型与真实系统代价之间仍需桥接 |
| 输出大小估计 | SJPC (`1806.03313`)、LSH-SS (`1104.3212`) | 在执行前估计结果规模，服务于资源和计划选择 | 数据模型并非现代稠密 embedding；需要重新验证高维分布下的有效性 |

## 关键论文

### 1. Fast Approximate Similarity Join in Vector Databases / SimJoin

SimJoin 是现代图式 approximate vector join 的核心基线。它首先把每个 $x_i\in X$ 的阈值结果 $J_i\subseteq Y$ 定义为 join window，然后不再独立执行每个 range query，而是从已处理 query 的结果窗口出发，在 $Y$ 的 proximity graph 上滑动到下一窗口。它还把 query 处理顺序形式化为加权图上的最小生成树问题：在 proximity graph 是连通 $\epsilon$NG 的条件下，WindowOrder 返回其成本模型下的最优窗口顺序 [`10.1145/3725403`，第 2、3、13、15 页]。

实现上，SimJoin 同时为 $X$ 和 $Y$ 建 proximity graph：$X$ 侧图用于确定窗口复用顺序，$Y$ 侧图用于搜索；若父窗口与当前窗口相交，就以已有窗口作为多起点，否则在“复用父窗口”和“从固定入口开始”之间选择。论文还把机制扩展到 $k$-similarity join，并用跨新旧数据的 $k$NN 结果维护 proximity graph 的插入和批量删除 [`10.1145/3725403`，第 8、15–17 页]。

实验覆盖 6 个真实和 2 个合成数据集，规模最高为 SIFT 10M 和 Higgs 11M。相对 VBASE，作者报告 self-join 最高 17.8 倍、cross-join 最高 14.7 倍加速；相对精确 FGF-Hilbert，最高超过两个数量级且平均 join-result 损失低于 1%。所有方法的最终 pair 都经过精确距离验证，因此 precision 为 1；主要质量指标是按 query 平均的 recall [`10.1145/3725403`，第 2、17 页及第 8 节]。

它也是 `2603.16360` 的直接起点：后者把 SimJoin 复用最终 in-range window 的策略称为 **hard work sharing**，再扩展为复用遍历中间状态的 soft work sharing。SimJoin 的主要边界包括：窗口复用只保留最终结果；图中断开或阈值内区域不连通时会漏召回；HNSW 较长边会降低其“近似邻接图”质量；当前为单机内存实现，论文把 distributed join 留作未来工作。其 query-average recall 与 Xling 的 global pair recall 不可直接横比。论文说明实现为 C++11，但截至本轮整理未在作者主页或作者 GitHub 发现与论文绑定的公开代码。

### 2. Fast Approximate Vector Joins via Offline-Online Co-Design

论文把主要瓶颈拆为三个互补问题：相似查询之间重复遍历、分离的 query/data 索引反复发现跨集合几何关系，以及 OOD 查询中不连通的阈值内区域导致召回下降。对应方案是 encoder-centric merged index、复用“当前最近点”等中间状态的 soft work sharing，以及只对预测为 OOD 的查询启用桥接式 hybrid search。作者在 8 个视觉、文本和多模态数据集、两类图索引上评测，并扩展到 10M 向量；摘要报告合并索引最高降低延迟 44.1 倍、软共享相对硬共享最高 3 倍、OOD 数据上召回最高提高 43% [`2603.16360`，第 1、9 页]。

v2 §6 还提出跨集合 top-1 witness 的 NN-Map、局部维护、map 选择/组合与 join statistics。其 Witness Property 以离线 ANN accuracy 为前提：候选 witness 超出阈值并不能无条件证明 exact-empty。相关设计与已充分验证的实验应分开记录。

这篇工作最值得复用的不是某个专用索引，而是“把 join 当作跨查询、跨重复任务的整体工作负载”。其限制也清晰：当前目标是内存中的高维距离计算，disk/distributed/parallel 扩展被列为未来工作；合并索引的最大收益依赖两个集合处于同一 encoder 的空间 [`2603.16360`，第 3、16 页]。

### 3. DiskJoin

DiskJoin 面向数据远大于内存的静态 similarity self-join。它按中心向量组织 disk buckets，以 bucket graph 发现候选桶，用依赖感知调度提升数据复用，以内存作为动态缓存，并通过基于目标召回率的概率剪枝减少候选。索引组织与距离阈值、召回目标解耦，可以一次构建后复用；最低内存需求报告为数据规模的约 0.2%。论文在单机 NVMe SSD 上处理到十亿级向量，相对替代方案报告 50–1000 倍加速 [`2508.18494`，第 1、9、10 页]。

它说明不能把 SSD 上的 top-$k$ 索引简单套成 nested-loop join：逐向量随机读会造成读放大与重复访问。当前模型把任务视作专用服务器上的静态端到端批处理，更新、跨集合 join，以及与在线查询并存时的资源治理仍是开放问题 [`2508.18494`，第 2、14 页]。

### 4. Xling / XJoin

Xling 使用回归模型估计某查询在阈值 $\epsilon$ 下的邻居数，再根据 Xling Decision Threshold 决定是否执行真正的 range search。它把传统“是否存在邻居”的过滤扩展为 filtering-by-counting，也可插入 LSH、KMeansTree、IVFPQ 等循环式 join。论文报告 XJoin 相对基线最高 17 倍加速，并展示在相近分布的新数据上不重训迁移 [`2402.13397`，第 1、2、7、8 页]。

这里的“quality guarantee”需要谨慎理解：过滤阈值控制经验性的速度—召回权衡，并不意味着任意分布漂移下都能保持确定性零漏报。对本项目而言，Xling 更适合作为优化器中的可选前置算子，并配合漂移检测、置信校准和 fallback，而不是独立承担正确性。

### 5. On the Complexity of Inner Product Similarity Join

这篇论文解释了为何高维内积 join 很难普遍获得强次二次算法：在 Orthogonal Vectors Conjecture 等假设下，精确或高精度近似的改进受到条件下界约束；其输出重点是每个 query 至少一个合格 witness，而不是全部 pairs 枚举。论文同时给出基于非对称嵌入和 LSH 的上界 [`1510.02824`，第 1、4、5、12–15 页]。工程含义是：系统必须明确接受哪一种近似、数据假设或硬件/预处理成本，不能只追求一个与分布无关的“通用快速 join”。

### 6. GPU-Join

GPU-Join 是精确 Euclidean self-join：只索引非空网格单元，选择部分维度建索引，进行维度重排，并把主机—设备传输、kernel 和结果物化流水化。实验覆盖 5 个真实和 3 个合成数据集，最大真实数据为 500 万点，使用 NVIDIA GP100；多数场景优于并行 CPU 基线 [`1809.09930`，第 1、10、11 页]。

它仍是重要的 exact baseline，但实验维度为 16–90，结果通过批处理存到 host，不能把论文描述成所有结果必须同时驻留 GPU 显存。§5.2 排除数据加载/建索引，包含 join 与 host 结果存储；其计时与 DiskJoin 的含 build 端到端时间不同，也不能直接代表 128–1536 维现代 embedding 或超大输出场景 [`1809.09930`，第 4、10 页]。

### 7. Join-size Estimation

SJPC 在流式、多属性相似度模型中用与记录数无关的计数器数量做单遍估计，给出无偏性、高概率误差界和 $O(\log n)$ 位计数器空间，并在大规模实验中比随机采样更可扩展 [`1806.03313`，第 1、2、9、12–14 页]。LSH-SS 则把真实对分为同桶与跨桶两部分，对两部分采用不同抽样策略，并针对高/低阈值给出概率误差保证 [`1104.3212`，第 3、6–8、11–12 页]。

两者提供了“先估输出量，再决定 join 计划”的方法基础，但输入模型应分开：SJPC 是多字段 equality/Hamming；LSH-SS 直接研究实向量 similarity join size，主要采用 cosine/TF-IDF，并非仅集合/Jaccard [LSH-SS §1 Definition 1、§6](https://www.vldb.org/pvldb/vol4/p338-lee.pdf)。后者应归为 A 类基数估计前序；在现代 dense embedding、OOD 和极端阈值下的适用性仍待实验验证。

## 综合判断

这些工作分别强调图遍历、SSD I/O、query filtering 与统计。[Fast AVJ v2](https://arxiv.org/abs/2603.16360) §6 已提出 NN-Map、局部维护、map selection/composition 与 join statistics 的优化器用途；LSH-SS 和早期高维 join 也已有基数/成本研究。因此不能把“自适应优化器或基数估计无人研究”作为结论。可进一步验证的方向是把输出物化、离线摊销和端到端召回不确定性纳入多个计划的联合校准；这属于本项目综合假设，新颖性尚未充分排查。

以下为基于文献综合推导的候选研究问题，尚未充分排查新颖性；其中维护/统计需区别于 NN-Map 已有设计：

1. **高维 join 基数与成本联合估计。** 不只估计 $|J_\theta|$，还要估计候选数、图遍历距离计算数、SSD 读量和结果物化成本，并输出置信区间。
2. **异构执行计划选择。** 在 learned filter、graph traversal、GPU brute-force、SSD bucket join 之间按阈值和目标召回动态选择或组合。
3. **动态数据与索引复用。** 支持插入、删除、encoder 版本变化和重复 join，量化增量维护相对重建的收益边界。
4. **可解释的质量保证。** 将过滤器误差、ANN 漏报、概率剪枝和最终 pair recall 组合成端到端保证，而非只报告单个组件 recall。
5. **统一 benchmark。** 固定 cross/self-join、ID/OOD、不同输出密度、内存预算和硬件配置，并同时报告 build、update、join、verification 和 materialization 成本。

## 建议课题

一个需要实验和新颖性排查的候选问题是（不能据此断言已有论文空间）：

> **面向分层存储的自适应高维向量阈值连接：用带置信区间的基数/成本估计，在 learned filtering、图搜索、GPU 验证和 SSD 分桶之间生成满足目标召回率的执行计划。**

最小可行版本可先限定为静态 self-join、L2/cosine 两种度量和单机 CPU+GPU+SSD；随后增加 cross-join 与增量更新。完整问题、已有工作的重叠、可检验假设、最小实验与反例见 [research-questions.md](research-questions.md)。

## 引用列表

- `10.1145/3725403` — Fast Approximate Similarity Join in Vector Databases
- `2603.16360` — Fast Approximate Vector Joins via Offline-Online Co-Design
- `2508.18494` — DiskJoin: Large-scale Vector Similarity Join with SSD
- `2402.13397` — Xling: A Learned Filter Framework for Accelerating High-Dimensional Approximate Similarity Join
- `1510.02824` — On the Complexity of Inner Product Similarity Join
- `1809.09930` — GPU Accelerated Similarity Self-Join for Multi-Dimensional Data
- `1806.03313` — Similarity Join and Similarity Self-Join Size Estimation in a Streaming Environment
- `1104.3212` — Similarity Join Size Estimation using Locality Sensitive Hashing

alphaXiv 的原始检索结果、结构化报告和逐页证据保存在 [`alphaxiv/`](alphaxiv/)；正文材料保存在 [`papers/vector-join/`](../../papers/vector-join/)。
