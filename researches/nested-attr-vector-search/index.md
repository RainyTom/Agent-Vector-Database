# 层级属性向量检索：导航

静态目录感知加速位于 **[01-directory-aware-acceleration](01-directory-aware-acceleration/README.md)**，动态混合检索位于 **[08-dynamic-hybrid-vector-search](08-dynamic-hybrid-vector-search/README.md)**。本文件只做导航，不承载方案正文、实验记录或结果表。

## 当前工作

- [01 目录感知加速（静态 DSQ，hnswlib NodeANN 基线已重测）](01-directory-aware-acceleration/README.md)
- **[08 动态混合向量搜索（动态三臂维护成本对照）](08-dynamic-hybrid-vector-search/README.md)**：[当前证据边界](08-dynamic-hybrid-vector-search/2026-10-29-exps/scheme-status.md)、[原树/两遍GT协议](08-dynamic-hybrid-vector-search/2026-10-29-exps/wiki-topology-protocol.md)、[当日报告与图](08-dynamic-hybrid-vector-search/2026-10-29-exps/report.md)。旧inline计时仅诊断；不预设高更新率优胜，不以旧并发探针证明一致性。
- [当前状态：闭包候选收尾，停止当前原型扩展](2026-10-31-exps/report.md)；[原方案与历史任务书](2026-10-30-exps/closure-server-plan.md)、[小预算原始执行任务](2026-10-30-exps/closure-small-ef-server-prompt.md)。已接收2f8c4e0，不再自动重跑或启动Dynamic。
- **[最新结果：资格执行修正有效，H停止；含汇总勘误](2026-10-31-exps/eligibility-3aaa0ed-review.md)**。已接收3aaa0ed的102次static/36次有限maintenance；[历史执行协议](2026-10-31-exps/eligibility-server-prompt.md)不再自动运行。E分层重复计数、双Recall门及每图锁表开销已独立复核。
- [此前树模型任务：查询效率优先，复用驻留索引](2026-10-31-exps/packed-query-first-plan.md)；[服务器9pass提示词](2026-10-31-exps/packed-query-first-server-prompt.md)、[本地验证](2026-10-31-exps/packed-query-first-local-report.md)。源码d4add37（基于cdc28f3）；暂停维护/扩规模矩阵，增删改只分析理论成本。原[完整索引定义](2026-10-31-exps/packed-index-plan.md)保留；旧H继续停止。该树模型协议不自动适用于DAG新语义。
- **[最新定位复核：DAG语义层与共同执行器](2026-11-01-exps/report.md)**：已核a1db223与TrieHI原文；7.64%是多父暴露比例，非实测漏答率；现有online/mat_all走全局HNSW，局部图同执行器臂尚需接线。逻辑scope交叠不等于物理图冗余；保留层级多属性主问题，不启动新的ANN或维护矩阵。
- **[当前决策：编码树替代图的方案被用户驳回](2026-11-01-exps/report.md#13-编码树路线被用户驳回回到图主检索约束)**。[历史整体方案](2026-11-01-exps/dag-unified-search-plan.md)和构建说明仅存档，未实施、非实验失败。后续要求图主检索、保留DAG多父与内部节点直属item、多属性同对象资格；不重启多图独立top-k合并，不把过滤前处理包装成完整搜索创新。
- **[最新下游交接入口](2026-11-01-exps/graph-exploration-handoff.md)**、[直接使用的图索引探索提示词](2026-11-01-exps/graph-exploration-prompt.md)：包含当前约束、固定12-item元数据、历史源码/结果阅读清单、驳回原因和全部本轮文件。图为主检索；允许下游独立推理新机制，不自动运行旧服务器任务书。

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

- **[下闭锥图：DAG 层级多属性过滤的单图搜索](2026-11-01-exps/dag-cone-graph-design.md)**：合格集是 poset 下闭集，据此可精确剪掉整种子树并把可访问集限制在入口最小锥（真实 DAG 上 p50 0.86%、p95 6.4%）；一张图、单队列，不回到每概念 top-k 合并。设计已完成，未实现、无性能数字。

- **[可导航性理论：跳出 NodeANN 的判据](2026-11-01-exps/dag-navigability-theory.md)**：图索引只有集合级与边级两个杠杆；`Mδ ≳ ln|S|` 说明全局图+过滤在低选择率下结构性失效；下闭边查询无损（保留率 `M` 对 `Mρ`）；`ρ` 是数据属性并附零模型测量；由此导出按 poset 距离分配度数的索引族。

- **[方案 v2：ACE（按局部 ρ 分配的双类边预算）](2026-11-01-exps/dag-ace-scheme-v2.md)**：每节点 `g_u` 条几何边 + `d_u` 条下行边，闭式分配 `g_u=(M-R_u)/(1-ρ_u)`；查询沿用 D/E 剪枝与单队列，加 Ant(D) 卡住回退。

- **[方案 v3：CGP（静态分簇图）](2026-11-01-exps/dag-cgp-scheme-v3.md)**：概念纯净簇 + 静态合并（`Bmax`/`τ`）+ 簇内 HNSW + 可比簇桥；位图/Bloom 做准入、Trie 做范围归约、量化只做簇粗排，图仍是唯一导航核心。

- **[方案 v4：ESG（入口选择型全局图）](2026-11-01-exps/dag-esg-scheme-v4.md)**：不改边、只改入口——一张标准 HNSW + 每概念 `k_e` 个代表入口，按质心/PQ 粗排取 `t` 组作为种子；位图/Bloom 筛入口组，图仍是唯一导航核心，无每概念 top-k 与归并。

- **[方案 v5：DAWG 骨架 + 可测区间压缩](2026-11-01-exps/dag-dawg-scheme-v5.md)**：trie 的服务必然、形状不必然——`A→D,B→D,C→D` 即反例；枚举全部 6 概念 DAG 有 16.3% 无法区间化，真实 CCS 有 310 个 ≥3 父概念；因此骨架取 DAG（DAWG），区间层只作可测压缩。

- **[方案 v6：子树分图 + 保边合并](2026-11-01-exps/dag-merge-scheme-v6.md)**：按 owner 精确划分子树小图，合并只做并集与门户桥、不重新剪边（子树内边一条不减），门户兼作入口，每概念边 O(q)；§10 给出属性/owner/DAG 边三类更新的局部化路径（门户槽 + 计数永不低估）；§11 区分显示名、属性值名与结构性改名，前者 O(1)、后者按边/owner 变更计。

- **[方案 v7：DAG 划分 + 合并策略矩阵（2×2）+ 三类过滤结构](2026-11-01-exps/dag-merge-matrix-v7.md)**：插入顺序（随机/拓扑序）× 合并规则（重插/保边）四臂，统一 M/efC/k/ef；Trie 解析、位图判资格、Bloom 做邻接级否定；基线为随机序全局 HNSW + 过滤。

- **[方案 v8：概念纯净聚类 + 质心图 + 残差 SQ（IGP 式）](2026-11-01-exps/dag-igp-quantized-scheme-v8.md)**：按概念内部 k-means 得概念纯净质心，item 存 `(质心ID, 2bit/维残差码)`，图建在质心上（`K=n/B`）；质心级剪枝精确，代价是量化与每质心 top-k 两个新增近似来源。

- **[方案 v9：(质心 × 概念) 分桶](2026-11-01-exps/dag-centroid-bucket-scheme-v9.md)**：允许质心横跨多个概念，把桶切到 `(μ, c)`，剪枝精确性由桶提供；聚类策略与剪枝正确性解耦，混合质心的损失限制在桶级。

## 相邻主题

- [前缀包含过滤](../containment-filtered-vector-search/index.md)
- [语义时空混合搜索](../semantic-spatiotemporal-hybrid-search/index.md)
