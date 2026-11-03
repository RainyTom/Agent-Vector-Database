# JAG: Joint Attribute Graphs for Filtered Nearest Neighbor Search

2026-11-03核查。Haike Xu、Guy Blelloch、Laxman Dhulipala、Lars Gottesbüren、Rajesh Jayaram、Jakub Łącki，顺序以[arXiv元数据](https://arxiv.org/abs/2602.10258)和[alphaXiv论文页](https://www.alphaxiv.org/abs/2602.10258v1)交叉核对。分析v1（2026-02-10）；正式venue/DOI未核实。唯一全文见`paper.md`，由alphaXiv公开PDF取得，日期化来源/SHA另存。

## 问题与方法

B类可迁移filtered ANN，不是vector join。输入向量、属性及查询过滤器，输出每query近似top-k；支持Label、Range、Subset、Boolean。§3.1给出attribute/filter distances；subset以缺失标签数作filter distance，Boolean以到满足赋值的Hamming距离作例子。

已核PDF pp.2–7、§2–3.3：Threshold-JAG对多个capped attribute-distance阈值分别搜索候选，合并候选后通过JointRobustPrune选边。最终是一张共享图，按阈值分配总出度预算；不是每阈值一个完整独立查询。Alg.3 pp.6与Alg.4 p.7明确union和R/|T|预算。Weight-JAG另以不同属性/向量权重取代阈值。

搜索以(filter distance,vector distance)词典序导航，固定入口，允许不合格点参与路径；不等于本项目合法多入口、严格b=0。这里的hierarchy是多属性严格度，不是外部多父taxonomy DAG。

## 实验、限制与可迁移点

§4.1–4.3已核：SIFT、ARXIV、LAION、YFCC、MSTuring，QPS–Recall、构建时间及附录距离数曲线；基线含ACORN、NaviX、RWalks、Filtered/StitchedVamana、UNG、iRangeGraph等。本文明确SIFT互斥label上专用分区索引可优于通用JAG。未细读全部曲线/附录，不抄峰值，也未运行作者实现。形式化ANN召回/复杂度保证未核实。

“不同属性尺度的边融合成单图”及“统一总度预算”已有直接前序，不能作为本项目新增。可借鉴JointRobustPrune的多尺度预算思想，但不能直接称其剪枝在Direct/Recursive OR范围保持导航；须另证资格见证和诱导图性质。当前未实现全局分层、b>0或等字节剪枝，不拿这些计划申报相对优势。
