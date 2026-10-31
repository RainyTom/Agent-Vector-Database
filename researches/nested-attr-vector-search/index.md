# 层级属性向量检索：导航

静态目录感知加速位于 **[01-directory-aware-acceleration](01-directory-aware-acceleration/README.md)**，动态混合检索位于 **[08-dynamic-hybrid-vector-search](08-dynamic-hybrid-vector-search/README.md)**。本文件只做导航，不承载方案正文、实验记录或结果表。

## 当前工作

- [01 目录感知加速（静态 DSQ，hnswlib NodeANN 基线已重测）](01-directory-aware-acceleration/README.md)
- **[08 动态混合向量搜索](08-dynamic-hybrid-vector-search/README.md)**：[最新ef15/20小预算报告](2026-10-30-exps/closure-small-ef-report.md)、[复现](2026-10-30-exps/experiments/closure-small-ef-reproduction.md)、[六臂大预算报告](2026-10-30-exps/closure-server-report.md)、[上游阅读清单](08-dynamic-hybrid-vector-search/2026-10-29-exps/upstream-reading-list.md)；[前轮NPI](08-dynamic-hybrid-vector-search/2026-10-30-exps/neighborhood-report.md)、[历史维护对照](08-dynamic-hybrid-vector-search/2026-10-29-exps/scheme-status.md)保留。
- [当前状态：闭包候选收尾，停止当前原型扩展](2026-10-31-exps/report.md)；[原方案与历史任务书](2026-10-30-exps/closure-server-plan.md)、[小预算原始执行任务](2026-10-30-exps/closure-small-ef-server-prompt.md)。已接收2f8c4e0，不再自动重跑或启动Dynamic。
- **[最新执行结果：资格感知执行＋纯NodeANN审计](2026-10-31-exps/eligibility-server-report.md)**（[复现](2026-10-31-exps/experiments/eligibility-server-reproduction.md)、[静态聚合](2026-10-31-exps/data/eligibility-static-run1)、[维护聚合](2026-10-31-exps/data/eligibility-maintenance-run1)）：102次静态＋36次维护完成，语义零错误；`E<ef`缺口经cap/hybrid修复，但H-aware在两个数据集Recall更低且无相对U-aware增量，**无同质量胜出**。方案与任务书：[方案](2026-10-31-exps/eligibility-execution-plan.md)、[本地实现](2026-10-31-exps/eligibility-local-report.md)、[任务](2026-10-31-exps/eligibility-server-prompt.md)。旧H负结果不撤回。

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
