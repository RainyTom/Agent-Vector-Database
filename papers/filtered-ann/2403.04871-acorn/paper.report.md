# ACORN: Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data

核查日期：2026-10-18。等级 B，可迁移借鉴；非 vector join。分析版本：2403.04871v1。

## 问题与方法

向量集合、查询向量、任意可计算精确谓词与 k。

Predicate subgraph traversal；ACORN-gamma 扩展构图与压缩；ACORN-1 多跳搜索扩展。

## 保证与限制

相关正文给候选扩展/可导航性概率分析，需保留分布及参数条件；不视为任意谓词下端到端召回保证。

本轮未完整核查所有复杂度条件。

实际召回以实验衡量；未核实无条件逐查询下界。

未显式按外部 taxonomy 组织索引；任意过滤可表达不等于所有过滤上的性能保证。

## 实验核查

数据：SIFT1M、TripClick、LAION（具体配置见正文）。基线：HNSW、pre-filtering、post-filtering、其他基线本轮未完整核实。指标：recall、throughput、index size、construction time。

正文比较共享谓词图的吞吐—召回与索引开销；本轮不转述跨 workload 加速比。未独立复现。

## 本项目启发与证据

以子树 bitmap/callback 适配为强基线；不要把 HNSW 层级当作类别树。

正文定位：predicate subgraph traversal, ACORN-gamma/ACORN-1, evaluation；PDF 页 1–2, 4, 6–8, 13。见 [PDF](paper.pdf)、[完整文本提取](paper.md)、[alphaXiv 页证据](alphaxiv-evidence-2026-10-18.xml) 和 [创新性比较](../../../researches/nested-attr-vector-search/novelty-analysis.md)。

## 2026-11-02 搜索策略实现核查

核对 v1 §5.1/Algorithm2、§5.2–5.3 与作者固定 commit `c259f11cdbec880671658eaa0d8747e2cc9de79d`，详见[日期化审计](implementation-audit-2026-11-02.md)。ACORN-1 扩展所有一/二跳邻居，再按谓词过滤和 M 截断；合格中间点也需要展开。“without pruning”不等于普通HNSW只关闭ACORN压缩，RNG剪枝也不同。

本地交付是固定 hnswlib 上的搜索策略移植：保留现有RNG图、采用合格owner入口、稳定局部去重与论文M截断，满足不合格item零查询距离；不是作者FAISS完整系统或ACORN-gamma构图复现。合成回归已验证有/无资格中间点扩展、上下层、去重、截断与二跳边界；SIFT观测和证据统一收录于[当日报告](../../../researches/nested-attr-vector-search/2026-11-02-exps/report.md)。不以移植的性能替代原论文结论，作者运行时和正式稿差异仍待核。

