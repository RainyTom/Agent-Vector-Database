# 2026-11-01 PiPNN 论文审读与入库

## 目标与结论

核实用户所给“PiANN / PiPNN”解读，保存可追溯原文、方法/理论/实验笔记及主线迁移判断。按原始标题确认目标为 **PiPNN: Ultra-Scalable Graph-Based Nearest Neighbor Indexing**，KDD2026，DOI `10.1145/3770855.3817891`。归 `vector-ann`、B类可迁移借鉴，非阈值vector join。

核心机制是重叠RBC、多层fanout、叶内GEMM/双向局部kNN与HashPrune有界候选合并；最终RobustPrune在作者主实验中开启。有界候选状态不等于整体低内存，方向剪枝不能安全保留所有threshold pairs。

## 设置与本轮实际执行

1. 查重总论文/关系及笔记，核arXiv版本历史、Crossref正式出版注册、作者主页与官方代码入口；核读v3 §2–6 / A.1–A.10。原始PDF18页（3,421,153 bytes）、分页文本、SHA和Crossref响应已归档；ACM全文403，未冒称已比对正式正文。
2. 独立执行Algorithm3字面严格比较规则的有限构造，固定桶、候选、容量，枚举到达顺序；不是运行作者代码。实际输入/输出在[data](data/pipnn-hashprune-semantics.json)，完整复现代码与环境记录在[experiments](experiments/pipnn-reading.md)。
3. JSON/路径/PDF与检索smoke结果归档到[data](data/pipnn-archive-validation.json)。没有ANN/十亿/join性能实验，没有用合成规则例子支持论文速度结论。

## 实际观察数字

| 本轮有限构造 | 候选数 | 容量 | 到达顺序数 | 不同输出数 | 观察 |
|---|---:|---:|---:|---:|---|
| 距离互异、跨桶竞争 | 5 | 2 | 120 | 1 | 全部得到 b、c |
| 同桶、等距不同ID | 2 | 1 | 2 | 2 | 先到的a或b被保留 |

第二行说明字面严格 `<` 规则的唯一ID保证需要并列处理；**未确认官方实现存在相同问题**，本轮未核它的bf16/比较器/并发路径。第一行只是有限实例，不当作一般history independence证明。

## 论文报告数字（本轮没有复现）

论文§1报告构建最高加速相对Vamana 11.6×、HNSW 12.9×；下游§5.2的k=10、边recall≥95%构图包含索引构建与全量查询，相对hnswlib 2.2–6.9×、ParlayANN Vamana 1.4–1.7×。局部叶子优化27.03×不是总构建加速。完整数据/硬件/度量及内存数值保存在[结构化摘录](data/pipnn-paper-results.json)和[逐篇解读](../../../papers/vector-ann/2602.21247-pipnn/paper.report.md)，不重复抄所有表格。

v3 Table6补充了峰值RSS；例如BigANN-100M的PiPNN 214.45GiB，而Vamana 51.62GiB，128 threads。这个差异限制低内存外存迁移。Table4对Vamana关闭预取，不能无条件外推硬件计数器结论。

## 交付与限制

- 新增1条PiPNN记录、2条有方向/证据关系，区分明确引用与研究者综合；主索引44篇/32关系，`vector-ann` 5篇。
- 更新主题导航、总导航、changelog、search-log和research-questions；首次/复用top-k假设及最小实验明确尚未运行、新颖性尚未充分排查。
- 正式版全文仍缺；v3摘要“下游最高1.9×”与正文/结论最高6.9×不一致，两者按原位置保留。v3已把OpenAI Table1改为2M，不沿用v1的1M。
- 作者后续Turbocharging PiPNN已在主页列出，但OpenReview forum/PDF均被浏览器验证阻挡；只登记候选，不臆造方法和实验、不与PiPNN合并。
- 未核官方实现的self-match排除、并列距离/量化/并发细节；未逐点数字化所有曲线，未声称穷尽近期构图或join工作。

## 下一步

先补正式稿-v3差异与Turbocharging全文，再决定是否运行同RSS预算的首次/复用top-k实验。对于threshold join，HashPrune删合法边的反例已成立，不将其直接作为完整结果剪枝器；外存、属性过滤和动态维护均需另做正确性与成本验证。
