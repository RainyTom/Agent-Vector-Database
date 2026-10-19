# Tag-Filtered Approximate Nearest Neighbor Search

核查日期：2026-10-19（Asia/Shanghai）。B 类可迁移借鉴；原问题单查询 filtered top-k，不是 vector join。

## 书目与版本

Jiarui Luo; Miao Qiao; Chaoji Zuo; Dong Deng。2025 IEEE 41st International Conference on Data Engineering (ICDE), 3642–3654。DOI：https://doi.org/10.1109/ICDE65448.2025.00272 。

作者公开 13 页稿；Crossref 与 IEEE 元数据核对；正式版差异未逐项核对。独立预印本身份未核实，不猜测 arXiv ID。

## 输入输出与方法

查询标签 t(q)⊆t(p)，原生 conjunctive multi-tag ANN。输出为对应过滤集合内近似 top-k 对象。

- Local 逐节点按标签频率分配边
- Global 逐标签图与全局预算
- Packing 块内共享出度并合并高频 mask tag
- Algorithm 3 选择最稀有查询标签作覆盖范围，估交集基数后扫描或图搜索，完整标签校验

## 理论与边界

§IV Lemma 1 给 Global 图边空间；不是完整 Packing 召回保证。

Global 图边空间 O(nM0)；Packing 块内度预算与高频 mask；本轮未核独立完整时间上界。

未提供任意数据下的端到端 Recall@k 下界；导航覆盖单标签不保证交集所有近邻。

- 需要标签频率、估计、阈值及度分配参数
- 原文基数估计不应小于最稀有 posting 的文字与交集关系相反，需核实现
- 中低频优化不等同外部 taxonomy 原生共享

## 实验与项目联系

数据：SIFT、GIST、YFCC、YouTube-Audio、YouTube-Video。基线：FilteredVamana、StitchedVamana、ACORN、NHQ、post-filter HNSW、Faiss-IVF。指标：Recall@k、QPS/延迟、空间、构建时间。

§VI 单/多标签、分布/频率/数据规模比较；摘要报告 recall≥0.9 下最高约13倍 QPS，仅原文实验；未独立复现。

- 包含过滤原生强基线
- 按稀有标签覆盖后精确验证是合法 AND 路径，不可替换成 OR 多路合并
- 包含基数估计/扫描与图选择已有先前工作

## 证据

[原始 PDF](paper.pdf)、[全文提取](paper.md)；章节：I motivation, II Definitions 1–2, III Algorithm 3, IV Lemma 1, V Packing Algorithms 6–7, VI；PDF 页：1–7, 8–12。全文提取为 PyMuPDF，非 MinerU。

完整方案动机与关联见[包含过滤文档](../../../researches/containment-filtered-vector-search/index.md)。
