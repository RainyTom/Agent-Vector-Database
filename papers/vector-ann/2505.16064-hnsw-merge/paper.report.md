# Three Algorithms for Merging Hierarchical Navigable Small World Graphs

2026-11-03核查。Alexander Ponomarenko；arXiv:2505.16064v1，2025-05-21；正式venue/DOI未核实。通过[alphaXiv全文](https://www.alphaxiv.org/abs/2505.16064)取得正文，并以[arXiv元数据](https://arxiv.org/abs/2505.16064)核标题和作者。原文唯一全文见`paper.md`，来源及SHA见`metadata-2026-11-03.json`。

## 问题与方法

B类可迁移ANN图合并，不是vector join。输入两个独立HNSW，输出合并后的多层HNSW；原文不定义属性DAG过滤或Direct/Recursive。

已核§1.4、§2、§4和§5相关正文：把合并分为顶点选择、候选收集、邻域重建、信息传播；逐层执行。NGM每点在另一图搜索候选，联合原邻居后重选；IGTM按相近点顺序处理、复用候选和LocalSearch；CGTM允许跨输入图选下一个点。不是保留全部底层原边后仅加少量代表桥的算法。

## 实验与保证

§5：SIFT1M分成两个500k不交集合；原HNSW M16、level-0为32、efConstruction32；与SIGM、NGM、IGTM、CGTM比较。查询报告Recall@5与距离次数，搜索L=32/40/50/64/72；合并距离工作和查询距离工作分别展示。摘要称最多少70%距离，不将该摘要峰值作为本项目可迁移收益；未逐点读取全部曲线，未运行作者实现。形式化导航/召回定理及完整复杂度未核实，不将经验结果写成保证。

§7 p.16明确将“从小图开始递归合并以构图”列为未来方向。它不是已经实现了多父属性DAG合并，但足以否定这一宽泛想法本身为首次。

## 与本项目的关系

不能主张首次合并HNSW、首次复用原邻接或首次提出递归合并小图。当前具体差异是外部属性DAG、直属owner分区、递归真实item代表与过滤桥接、最终平面共享搜索；这些差异是否形成独立贡献仍未核实。必须区分HNSW随机层级与语义属性层级，也不能将本文的普通合并用于证明属性过滤诱导图连通。

未核：正式发表状态、完整理论、作者代码入口与全部参数/图表；本轮仅核指定相关节，不声称全篇复现。
