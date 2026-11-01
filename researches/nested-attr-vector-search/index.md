# 层级属性向量检索：导航

本方向研究向量与索引全驻内存时的 DAG 层级属性近邻检索。贡献范围为索引状态、图结构、搜索和更新算法；不以数据排布、缓存友好访问、SIMD、预取或重排作为优化机制。实验同时区分完整查询质量/时间与节点扩展、距离计算等算法工作量。

## 当前工作

- **[DAG 层级属性约束下的内存向量检索](2026-11-01-exps/hierarchical-dag-attribute-index.md)**：七概念、八对象贯穿局部图基线、跨图查询、集合去重与标签更新；三幅 SVG 对应算法步骤。正文列出研究边界与机制计数缺口，不把常规分组或访存实现当新算法。

- [DAG query20 实测与文献依据](2026-11-01-exps/report.md)：同批 20 查询、每臂驻留三遍的完整计时/双 Recall、分区 fanout 与构建成本；附 TrieHI、Curator、ACORN、iRangeGraph、SciHTC 的证据。

- [01 目录感知加速（静态 DSQ，hnswlib NodeANN 基线已重测）](01-directory-aware-acceleration/README.md)
- **[08 动态混合向量搜索（动态三臂维护成本对照）](08-dynamic-hybrid-vector-search/README.md)**：[当前证据边界](08-dynamic-hybrid-vector-search/2026-10-29-exps/scheme-status.md)、[原树/两遍GT协议](08-dynamic-hybrid-vector-search/2026-10-29-exps/wiki-topology-protocol.md)、[当日报告与图](08-dynamic-hybrid-vector-search/2026-10-29-exps/report.md)。旧inline计时仅诊断；不预设高更新率优胜，不以旧并发探针证明一致性。
- [当前状态：闭包候选收尾，停止当前原型扩展](2026-10-31-exps/report.md)；[原方案与历史任务书](2026-10-30-exps/closure-server-plan.md)、[小预算原始执行任务](2026-10-30-exps/closure-small-ef-server-prompt.md)。已接收2f8c4e0，不再自动重跑或启动Dynamic。
- **[最新结果：资格执行修正有效，H停止；含汇总勘误](2026-10-31-exps/eligibility-3aaa0ed-review.md)**。已接收3aaa0ed的102次static/36次有限maintenance；[历史执行协议](2026-10-31-exps/eligibility-server-prompt.md)不再自动运行。E分层重复计数、双Recall门及每图锁表开销已独立复核。
- [查询效率与驻留索引复用](2026-10-31-exps/packed-query-first-plan.md)；[9pass复现提示词](2026-10-31-exps/packed-query-first-server-prompt.md)、[真实SIFT结果与后续勘误](2026-10-31-exps/packed-query-first-local-report.md)。历史标签P-dfs/P-dfs-aware分别对应同一分区HNSW的图搜索/小候选集扫描策略；顺序postings是工程优化，不作为新剪枝。原[完整定义与历史更新任务](2026-10-31-exps/packed-index-plan.md)保留，不自动恢复完整维护/扩规模矩阵。

## 01 方向文档

- [问题、基线与优化路线](01-directory-aware-acceleration/2026-10-27-hierarchical-attr-vector-search-report.md)
- [完整 WIKI 基线与 HLG 进展](01-directory-aware-acceleration/2026-10-28-exps/report.md)
- [2026-10-29 hnswlib NodeANN 基线结果](01-directory-aware-acceleration/2026-10-29-exps/report.md)
- [HLG 理论笔记](01-directory-aware-acceleration/2026-10-28-exps/hlg-theory.md)
- [文献检索与新颖性记录](01-directory-aware-acceleration/search-log.md)
- [TrieHI 论文笔记](01-directory-aware-acceleration/paper-notes-2606.16903.md)
- [历史问题定义与朴素基线](01-directory-aware-acceleration/historical-hierarchical-topk-notes.md)

## 历史方向与共用审计

以下保留历史设计和证据，不表示恢复实施或通过新颖性审查：

- [六个子方向与停止/暂停决定](directions.md)
- [alphaXiv 新颖性审计](alphaxiv-novelty-audit.md)、[前序分析](novelty-analysis.md)、[overlap 细化](overlap-refinement.md)
- [DAG 范围维护](02-dag-incremental-scope/README.md)、[联合索引](03-prefix-coupled-index/README.md)、[渐进导航](04-certified-progressive-routing/README.md)
- [多版本目录](05-versioned-directory-search/README.md)、[继承权限](06-inherited-acl-search/README.md)
- [Laminar Edge Sharing 历史记录](07-laminar-edge-sharing/index.md)

## 相邻主题

- [前缀包含过滤](../containment-filtered-vector-search/index.md)
- [语义时空混合搜索](../semantic-spatiotemporal-hybrid-search/index.md)
