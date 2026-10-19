# Filtered-DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with Filters

核查日期：2026-10-19（Asia/Shanghai）。B 类可迁移借鉴；原问题单查询 filtered top-k，不是 vector join。

## 书目与版本

Siddharth Gollapudi; Neel Karia; Varun Sivashankar; Ravishankar Krishnaswamy; Nikit Begwani; Swapnil Raz; Yiyong Lin; Yin Zhang; Neelam Mahapatro; Premkumar Srinivasan; Amit Singh; Harsha Vardhan Simhadri。Proceedings of the ACM Web Conference 2023 (WWW 2023), 3406–3416。DOI：https://doi.org/10.1145/3543507.3583552 。

作者公开 11 页稿；首页正式 DOI，ISBN 等有占位符；与最终版差异未逐项核对。独立预印本身份未核实，不猜测 arXiv ID。

## 输入输出与方法

原文主要单标签 f∈Li；§3.1 多查询标签是 OR；§2 将 AND 列 future work。输出为对应过滤集合内近似 top-k 对象。

- FilteredGreedySearch 与每标签入口
- FilteredVamana 逐点增量构建
- StitchedVamana 叠加每标签 Vamana 图再压缩
- FilteredRobustPrune 在距离剪枝外维护共同标签覆盖

## 理论与边界

未提供任意多标签 AND 的端到端保证；原文 §2 明确 conjunction 为未来工作。

单点最大出度由 R 或 Rstitched 限制；图边量 O(nR) 为据算法的结构上界，不含标签/向量/构建临时图；一般查询复杂度未核实。

ANN 启发式召回；原文单标签实验不构成任意 conjunction 召回证书。

- 所读主问题为单标签；OR 搜索不等同于任意多标签 AND
- 有界出度仍可能使低频标签导航弱
- 每标签图暂存与标签元数据不能只用最终出度估总内存

## 实验与项目联系

数据：Turing、Prep、DANN、Audio、SIFT1M、Paper、Msong、GIST。基线：post-processing、inline-processing、FAISS IVF-PQ、NHQ、Milvus。指标：Recall@k、QPS/延迟、空间、构建时间。

§5 按标签 specificity 比较 QPS–Recall@10；含 SSD 测试，48 线程，不与其他硬件论文直接横比；未独立复现。

- 作为过滤构图前序与 singleton 基线
- 如适配 AND，需要覆盖集合+完整属性检查或谓词标签物化，明确新增成本
- 保留原文与 LSSG Table 2 对能力概括的差异

## 证据

[原始 PDF](paper.pdf)、[全文提取](paper.md)；章节：1.1, 2, 3 Algorithms 1–4, 4 Algorithm 5, 5；PDF 页：1–8, 10–11。全文提取为 PyMuPDF，非 MinerU。

完整方案动机与关联见[包含过滤文档](../../../researches/containment-filtered-vector-search/index.md)。
