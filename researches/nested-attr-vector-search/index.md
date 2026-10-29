# 层级属性向量检索：导航

当前研究统一归入 **[01-directory-aware-acceleration](01-directory-aware-acceleration/README.md)**。本文件只做导航，不承载方案正文、实验记录或结果表。

## 当前工作

- [01 目录感知加速（静态 DSQ，hnswlib NodeANN 基线已重测）](01-directory-aware-acceleration/README.md)
- **[08 动态混合向量搜索（多属性 + 频繁更新 + 联邦视角，阶段 1–4 完成）](08-dynamic-hybrid-vector-search/README.md)**：[基线复述](08-dynamic-hybrid-vector-search/2026-10-29-exps/baselines.md)、[文献空白 G1–G7](08-dynamic-hybrid-vector-search/2026-10-29-exps/stage1-literature.md)、[候选与 C1 负结果](08-dynamic-hybrid-vector-search/2026-10-29-exps/stage2-candidates.md)、[churn 原型与并发/摘要结果](08-dynamic-hybrid-vector-search/2026-10-29-exps/report.md)

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
