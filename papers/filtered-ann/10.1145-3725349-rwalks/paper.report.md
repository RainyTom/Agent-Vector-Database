# RWalks: Random Walks as Attribute Diffusers for Filtered Vector Search

核查日期：2026-10-19（Asia/Shanghai）。B 类可迁移借鉴；原问题单查询 filtered top-k，不是 vector join。

## 书目与版本

Anas Ait Aomar; Karima Echihabi; Marco Arnaboldi; Ioannis Alagiannis; Damien Hilloulin; Manal Cherkaoui。Proceedings of the ACM on Management of Data 3(3), Article 212 (2025); SIGMOD 2025。DOI：https://doi.org/10.1145/3725349 。

作者公开仓库 26 页稿，首页正式 DOI，第三作者拼写与出版元数据有差异；与最终版差异未逐项核对。独立预印本身份未核实，不猜测 arXiv ID。

## 输入输出与方法

二进制多属性 filtered ANN；AND 要求所有激活查询属性出现，亦定义 OR/NOT。输出为对应过滤集合内近似 top-k 对象。

- 基础向量图保持不变，后处理 random-walk attribute diffusion
- hybrid distance 引导导航
- twin priority queue 保留被挤出的合格对象
- 启发式 attribute pruning 减少距离计算，输出按原始属性校验

## 理论与边界

§4 单二进制属性/i.i.d. 距离等模型下的局部有效访问比例命题，不是端到端 recall 下界。

原文在固定参数下给平均 HNSW O(nlogn) 构建/O(logn) 查询与 O(n) 属性扩散；属性维数与活跃属性数变化时不可忽略，稀疏 map 成本另计。

§1 Scope 明确 retrieval-quality theoretical guarantees 不在范围内；启发式阈值可能导致 recall saturation。

- 标签空间/扩散后活跃属性数会带来属性表示开销
- 高剪枝阈值影响高召回，困难数据可关闭剪枝
- 当前固定 L2；range/其他距离列未来工作
- 作者稿第三作者 Arnobaldi 与正式 Crossref Arnaboldi 拼写不同

## 实验与项目联系

数据：Deep、SIFT、YFCC、arXiv。基线：HNSW search-then-filter、HNSW inline、IVF、HQANN、Filtered-DiskANN、ACORN-1、ACORN-γ。指标：Recall@k、QPS/延迟、空间、构建时间。

§5 Recall@10–QPS、空间/建图及消融；支持有/无过滤与复合 AND；摘要的加速属于原文各实验，未复现；未独立复现。

- 适合评估保留基础向量图时的属性导航
- 导航融合距离不能改变原向量排序定义
- 局部有效率改善不直接等于 top-k召回改善

## 证据

[原始 PDF](paper.pdf)、[全文提取](paper.md)；章节：1 Scope, 2.1 Definitions, 3 Algorithms 1–2, 4 analysis, 5 experiments, 6 future work；PDF 页：3–4, 7–16, 22。全文提取为 PyMuPDF，非 MinerU。

完整方案动机与关联见[包含过滤文档](../../../researches/containment-filtered-vector-search/index.md)。
