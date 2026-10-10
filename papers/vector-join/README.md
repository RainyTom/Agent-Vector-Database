# Vector Join Paper Catalog

论文按“核心执行工作”和“理论/相邻工作”分组。每个目录均包含 PDF、MinerU Markdown、图片、书目元数据，以及逐篇批判性分析 `paper.report.md`。跨论文综合见 [方案来源、演化、展望与缺陷](../../researches/vector-join/deep-analysis.md)。

## 核心执行工作

| 年份 | 论文 | 关注点 | 本地材料 |
|---|---|---|---|
| 2026 | Fast Approximate Vector Joins via Offline-Online Co-Design (`2603.16360`) | 内存、跨集合、近似阈值 join；合并图索引、软工作共享、OOD 自适应搜索 | [目录](2603.16360-fast-approximate-vector-joins/) |
| 2025 | Fast Approximate Similarity Join in Vector Databases / SimJoin (`10.1145/3725403`) | 核心基线；join-window sliding、MST 执行顺序、$k$-similarity join 与图索引维护 | [目录](10.1145-3725403-simjoin/) |
| 2025 | DiskJoin: Large-scale Vector Similarity Join with SSD (`2508.18494`) | 单机 SSD、超大规模 self-join；分桶、I/O 调度、缓存和概率剪枝 | [目录](2508.18494-diskjoin/) |
| 2024 | Xling: A Learned Filter Framework for Accelerating High-Dimensional Approximate Similarity Join (`2402.13397`) | 学习型基数过滤器；按查询跳过无效或低产出的 range search | [目录](2402.13397-xling/) |

## 理论与相邻工作

| 年份 | 论文 | 关注点 | 本地材料 |
|---|---|---|---|
| 2018 | GPU Accelerated Similarity Self-Join for Multi-Dimensional Data (`1809.09930`) | GPU 上的精确欧氏距离 self-join；网格索引和流水线 | [目录](1809.09930-gpu-similarity-self-join/) |
| 2015 | On the Complexity of Inner Product Similarity Join (`1510.02824`) | 内积 similarity join 的条件下界和近似算法边界 | [目录](1510.02824-inner-product-similarity-join-complexity/) |
| 2018 | Similarity Join and Similarity Self-Join Size Estimation in a Streaming Environment (`1806.03313`) | 流式、多属性相似连接大小估计；并非稠密 embedding join 执行算法 | [目录](1806.03313-streaming-join-size-estimation/) |
| 2011 | Similarity Join Size Estimation using Locality Sensitive Hashing (`1104.3212`) | 基于 LSH 的集合相似连接大小估计；现代向量 join 的方法学前身 | [目录](1104.3212-lsh-join-size-estimation/) |

## 完整性

当前收录 8 篇论文。所有论文均已通过 MinerU `vlm` 模型转换，原始解析结果保存在各目录的 `mineru/` 中；研究时用 `paper.report.md` 建立结构化认识，以 `paper.md` 核对正文证据，需要核对版式或页码时回看 `paper.pdf`。

