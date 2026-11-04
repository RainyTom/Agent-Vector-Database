# 层级属性向量检索：导航

静态目录感知加速位于 **[01-directory-aware-acceleration](01-directory-aware-acceleration/README.md)**，动态混合检索位于 **[08-dynamic-hybrid-vector-search](08-dynamic-hybrid-vector-search/README.md)**。本文件只做导航，不承载方案正文、实验记录或结果表。

> **2026-11-04 清理**：过往方案探索文档（v2–v9 各版方案、锥图、可导航性理论、分层路由、合并矩阵、IGP 量化、分桶、DAWG、ACE/CGP/ESG、JAG 式设计、探索交接与提示词，共 19 份）已删除，仅保留证据、协议与导航。历史版本在 git 历史中（`ee8941e` 之前）。本目录只保留：①实测证据（`report.md` 与各 `*-report.md`）；②数据协议（`dag-composition-delivery.md`）；③本轮问题确认与新颖性分析。

## 当前工作

- **[新颖性分析：问题层面与机制层面](2026-11-04-exps/novelty-2026-11-04.md)**：`direct/recursive` 两原语与维护解耦**已由 TrieHI（arXiv:2606.16903）正式定义**；"共享单图 vs 每子集索引"的对照范式已见于 Curator/SIEVE/RNSG；机制 **M（代表树）** 与 **T（覆盖终止）** 均判为 L2，M 的"任意下闭集连通"声称需限定到主下集。本轮新增实测：树化只影响 0.73% 查询、漏 0.24% item，但单查询最坏漏 43%，且 35.3% 查询随父选择改变闭包。

- **[最新：研究问题、方案与可行路径重新确认](2026-11-04-exps/report.md)**：重述问题与三条独立假设（结构/入口终止/维护）；列组件职责与 MST 与近代表桥的功能重叠；证据台账分"成立/已撤回/未验证"，其中**S0（单张全局 HNSW + 掩码）从未在同一 harness 跑过**、recursive 0.675–0.869× 不是等召回；给出 P0 归因、P1 覆盖终止、P2 代表树、P3 维护解耦四条路径及各自证伪判据。无新实验数字。

- **[当前交付：从原始记录到属性DAG合并图](2026-11-02-exps/dag-composition-delivery.md)**：区分属性节点与ANN节点；定义直属/递归两类查询、直属/子图两类入口、属性内HNSW、DAG连接、单队列搜索及CSR/bitmap消融。

- **[汇报PPT（10页，含校徽与三张示意图）](2026-11-03-exps/ppt/README.md)**：[pptx](2026-11-03-exps/ppt/hierarchical-hybrid-search-deck.pptx)。动机为属性扁平化后的递归更新；形态页给出 DAG 与四个组件；构建与查询用算法伪代码，合并规则单列选点与出度。配图由 GrsAI 生成，提示词见 [fig/PROMPTS.md](2026-11-03-exps/ppt/fig/PROMPTS.md)；LibreOffice 实际渲染逐页核对。
- **[最新：频繁属性更新能否作为差异化定位](2026-11-03-exps/update-positioning.md)**：实测当前静态构图约8 s，单次换owner的定向祖先链刷新中位约3.15×10^5次距离计算（约73次更新≈一次重建）；EMA已把动态更新作为主要贡献（属性-only 2.5 s/1M）。结论是“谓词状态图外、更新不修补边摘要”可辩护，但尚未实现，写出失效条件与最小实验。
- **[依托alphaXiv的DAG组合图创新性审查](2026-11-03-exps/alphaxiv-dag-novelty.md)**：基础机制有Stitched/LSSG等直接前序；完整DAG桥接组合尚未证等价、也未证独立贡献。原文/作者代码分级，祖先标签归约实跑通过，UNG全文仍缺；不是平台AI评分。
- **[Direct/Recursive按子图数G的QPS折线对比](2026-11-03-exps/subgraph-efficiency-report.md)**：保持AVX，固定两格，按精确非空属性图数实测并同步绘Recall。Direct最大G=3，Recursive最大G=24；没有Direct G=5点，高G多为单query且部分漏答，不能当等质量Pareto优胜。
- **[direct是否探索更多节点？](2026-11-03-exps/direct-work-report.md)**：补齐NodeANN原生上层/底层展开、邻接枚举与去重评分计数；含拒绝项的首次触达更多，不等于距离/展开更多。单属性多枚举约四分之一邻接；静态owner分段两版减少ID枚举却变慢，未采用。
- **[DAG单图执行器性能修复与归因勘误](2026-11-03-exps/report.md)**：固定k10/ef32、k100/ef128，查询内拒绝缓存、堆容量复用、replace-root及合格向量批量评分/预取；两轮整批延迟下降15.4%–29.6%，逐查询结果/距离/扩展/邻接计数不变。撤回旧精确成本分账与固定图数交叉阈值；direct仍未胜过固定n=k的NodeANN，召回未改变。
- [历史两格与n扫描对照](2026-11-02-exps/dag-two-cases-report.md)：保留原批次、子集与图，新增勘误；“多图就一定快”及“距离相近即可归因纯表示”均不成立。
- [历史探索：M64九档ef与局部n扫描](2026-11-02-exps/dag-pareto-m64-report.md)：跨ef图已撤下，仅保留原始点和源码快照，不作交付对比。


- **[历史M16独立hnswlib验证](2026-11-02-exps/dag-validation-report.md)**：24/120查询、剪枝失败与bitmap完整成本；原CSV与历史提交保留。

- **[历史24查询组合观察](2026-11-02-exps/dag-composition-report.md)**：共库递归原型数据仅作历史诊断；split-ef不等于总工作相同，剪枝不保证诱导连通，微基准不代表端到端收益。

- **[ACORN搜索策略来源与适配范围](2026-11-02-exps/report.md#8-acorn-1-搜索策略全一二跳邻域扩展)**：全一/二跳扩展，先资格后距离；保留RNG构图，与作者完整索引不同。当前实现放在独立`acorn1`目录。

- **[历史距离调用与资格核成本](2026-11-02-exps/report.md#6-三臂实际距离调用次数资格判断与内核时间)**：旧profile/replay只覆盖其注明工作负载，不与当前正常计时直接混比。


- [01 目录感知加速（静态 DSQ，hnswlib NodeANN 基线已重测）](01-directory-aware-acceleration/README.md)
- **[08 动态混合向量搜索（动态三臂维护成本对照）](08-dynamic-hybrid-vector-search/README.md)**：[当前证据边界](08-dynamic-hybrid-vector-search/2026-10-29-exps/scheme-status.md)、[原树/两遍GT协议](08-dynamic-hybrid-vector-search/2026-10-29-exps/wiki-topology-protocol.md)、[当日报告与图](08-dynamic-hybrid-vector-search/2026-10-29-exps/report.md)。旧inline计时仅诊断；不预设高更新率优胜，不以旧并发探针证明一致性。
- [当前状态：闭包候选收尾，停止当前原型扩展](2026-10-31-exps/report.md)；[原方案与历史任务书](2026-10-30-exps/closure-server-plan.md)、[小预算原始执行任务](2026-10-30-exps/closure-small-ef-server-prompt.md)。已接收2f8c4e0，不再自动重跑或启动Dynamic。
- **[最新结果：资格执行修正有效，H停止；含汇总勘误](2026-10-31-exps/eligibility-3aaa0ed-review.md)**。已接收3aaa0ed的102次static/36次有限maintenance；[历史执行协议](2026-10-31-exps/eligibility-server-prompt.md)不再自动运行。E分层重复计数、双Recall门及每图锁表开销已独立复核。
- [此前树模型任务：查询效率优先，复用驻留索引](2026-10-31-exps/packed-query-first-plan.md)；[服务器9pass提示词](2026-10-31-exps/packed-query-first-server-prompt.md)、[本地验证](2026-10-31-exps/packed-query-first-local-report.md)。源码d4add37（基于cdc28f3）；暂停维护/扩规模矩阵，增删改只分析理论成本。原[完整索引定义](2026-10-31-exps/packed-index-plan.md)保留；旧H继续停止。该树模型协议不自动适用于DAG新语义。
- **[最新定位复核：DAG语义层与共同执行器](2026-11-01-exps/report.md)**：已核a1db223与TrieHI原文；7.64%是多父暴露比例，非实测漏答率；现有online/mat_all走全局HNSW，局部图同执行器臂尚需接线。逻辑scope交叠不等于物理图冗余；保留层级多属性主问题，不启动新的ANN或维护矩阵。

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
