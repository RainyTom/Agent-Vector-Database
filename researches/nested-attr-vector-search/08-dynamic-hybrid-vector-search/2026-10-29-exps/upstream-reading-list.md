# 上游AI阅读清单

用途：独立审核全部已尝试路线、成功/负结果和证据缺口，决定下一步。**不要默认接受上一模型建议。** 实验/源码基线为 `19ad280`；本页与完整交接文档同一提交。默认GitHub `main` 可能落后，请使用分享者提供的固定commit链接或 `autoresearch/trie-20261028` 分支。相对链接从本页所在commit解析。

**独立复核入口（本轮新增）**：[事实/基线/候选排序/最小实验/论文门槛](independent-review.md)、[原文机制与新颖性证据](literature-mechanisms-review.md)、[162次离线重算及源SHA](data/independent-review-audit-20261029.json)。先完整阅读下列原交接，再对照复核；后者没有新增ANN性能数据，也不把历史方案推荐视为用户批准。


## P0：必须先读

| 顺序 | 文件 | 阅读目的 |
|---|---|---|
| 1 | [完整研究交接 research-handoff.md](research-handoff.md) | 覆盖01–08所有已记录主要实验家族/候选/负结果；先读§0勘误，再读全部正文。§3是正式162次，§5–11是历史与其他路线，§14是不预定路线的开放问题 |
| 2 | [当前方案状态](scheme-status.md) | 三臂定义、当前证据边界、历史降级及新增交接审计 |
| 3 | [原树与两遍GT协议](wiki-topology-protocol.md) | 完整子树选择、native/balanced控制、当前状态GT、质量门、维护分项及计时边界 |
| 4 | [正式small results.json](data/dynamic_triad/wiki-topology/tape-main-ef1024/results.json) / [正式scale results.json](data/dynamic_triad/wiki-topology/scale/tape-main-ef1024/results.json) | 全部162次运行、配置、SHA、Recall、时间/空间/维护计数；不要只看均值摘要 |
| 5 | [small交叉审计](data/dynamic_triad/wiki-topology/tape-main-ef1024/analysis/crossing-audit.json) / [scale交叉审计](data/dynamic_triad/wiki-topology/scale/tape-main-ef1024/analysis/crossing-audit.json) | 逐重复实际Q/U、同质量胜负和离散翻转区间；无交叉也是结果 |
| 6 | [现行C++执行器](../../../../codes/directory-aware-vector-attr-search/src/dynamic_triad.cpp) / [统一距离](../../../../codes/directory-aware-vector-attr-search/src/raw_l2_space.h) | 核对谓词、M/B/R、局部图维护、canonical GT、实际filtered搜索/计数；不是直接把旧静态API计数混进动态数据 |

补充核查：

- [small GT模式等价](data/dynamic_triad/wiki-topology/tape-main-ef1024/analysis/gt-mode-control-audit.json)、[scale GT模式等价](data/dynamic_triad/wiki-topology/scale/tape-main-ef1024/analysis/gt-mode-control-audit.json)。
- [small完整分层指标](data/dynamic_triad/wiki-topology/tape-main-ef1024/analysis/)、[scale完整分层指标](data/dynamic_triad/wiki-topology/scale/tape-main-ef1024/analysis/)中的query/update/workload CSV、维护分项与拓扑审计。
- [正式图目录](figures/wiki-topology/)：文件名前缀 `tape-main-ef1024` 才是正式两遍GT；`main-ef1024` 是已降级的inline诊断。
- [当日报告](report.md)：先读末尾“续轮”§6.5起以及最后交接勘误，不要把开头的历史推断当现行结论。
- [构建/机器记录](experiments/wiki-topology-build.json)、[原树复现](experiments/wiki-topology-reproduction.md)、[调度器](../../../../codes/directory-aware-vector-attr-search/scripts/run_dynamic_triad.py)。正式复测必须显式 `--gt-mode precomputed --ef 1024`，默认值不是正式协议。

## P1：判断研究方向时必须读

| 主题 | 文件 | 防止误读 |
|---|---|---|
| 历史动态420次 | [动态协议](dynamic-triad-protocol.md)、[复现](experiments/dynamic-triad-reproduction.md)、[全部结果目录](data/dynamic_triad/) | inline计时仅诊断；pure M、batch B、R、mixed分开，不用旧时间宣称当前优越 |
| C1–C7候选 | [候选表](stage2-candidates.md)、交接§6 | 旧“路由关闭/MERGE无价值”已收紧；中央Sample-ANN、Tree-TA、MERGE三臂、MVCC并没有实现 |
| 路由机会与失败 | [oracle原始JSON](data/oracle_routing_wiki.json)、[层级探针源码](experiments/hierarchical_routing_probe.py)、交接§6.2 | 大scope质心前缀节省50.5%不是0%；前缀依赖GT，层级探针有inclusive覆盖缺陷 |
| 新静态hnswlib NodeANN | [静态报告](../../01-directory-aware-acceleration/2026-10-29-exps/report.md)、[实际clean CSV](../../01-directory-aware-acceleration/2026-10-29-exps/data/frontier_agg_clean.csv)、[静态runner](../../../../codes/directory-aware-vector-attr-search/src/nodeann_research.cpp) | clean文件混pass1/clean；静态距离/hops漏level0；B1不全面优于B3；D5尾部数字待核 |
| 剪枝负结果 | [probe源码](../../01-directory-aware-acceleration/2026-10-28-exps/experiments/probe_scope_pruning.py)、交接§8.4 | 欧氏下界与平方tau单位混用；单query结果不否证整个方向 |
| 全局图/HLG/MST | [HLG理论及实现笔记](../../01-directory-aware-acceleration/2026-10-28-exps/hlg-theory.md)、交接§9 | 只完成旧连接边/部分MOVE；新后端G1/G2未跑；MST连接性不等于有限ef召回 |
| LES共享图 | [closeout最终对照](../../07-laminar-edge-sharing/2026-10-20-exps/experiments/les-closeout-ee90400.md)、[追加报告](../../07-laminar-edge-sharing/2026-10-20-exps/report.md)、交接§11 | 使用共享向量PSG强对照；r98未达标不能算胜；旧paper.md不是发表证据或最新事实 |
| 冻结设计与新颖性 | [审计](../../alphaxiv-novelty-audit.md)、[文献记录](stage1-literature.md)、交接§10/12 | “未实现/停止/暂停”不同；67篇/族不等于67篇全文均已核实；同义词/引用链仍有缺口 |

## P2：需要追溯历史时读

完整交接§8列出FAISS复现、14轮scope-run、选择率/几何相关性、量化等全部主要尝试；§6.3–6.6列出MERGE规模采样、7个局部T/C运行、17个并发探针、摘要回收模型及缺陷；§11列出LES各版本、剪枝/度截断/压缩/预算/失败。各节都有源码和保存数据路径，不应因不是当前主线就丢掉这些负结果。

## 必须保留的判断边界

1. 索引/搜索C++、统一hnswlib原始距离；FAISS仅k-means，量化仅IGP路线；大scope精确扫描不能因快就成为主方案。
2. 当前正式162次只有三个完整WIKI子树、纯M、约50%唯一覆盖、一个seed、非隔离串行提交重放；不是全量/并发/所有更新类型已验证。
3. 聚合Recall过门不是逐query质量保证；空GT处理不同会改变macro，必须同时看pair及分层。
4. 不把离线GT路由、操作/空间模型、目录解析、旧缺陷并发探针混成端到端实测。
5. 仓库上传源码、小体积CSV/JSON、协议、图与SHA；原始向量/大索引/ignored逐事件日志及部分远端二进制**没有随这些链接上传**。results里的本机raw路径不是公网下载地址。
6. 下一步可以质疑“先改停止条件”的建议：候选C不能免费来自oracle，现行调用有max(ef,k)，缩小ef不保证解决C<k，也不保证稀疏候选全局导航便宜。

## 建议交给上游AI的任务文字

> 请先完整阅读 research-handoff.md，并按本清单核对正式JSON、关键源码和历史勘误。不要延续上一模型的路线偏好，也不要把整个路由/剪枝/MVCC方向当作已否证。请输出：（1）哪些事实可采信、哪些须纠正/降级；（2）当前基线是否足够强，缺什么控制；（3）所有候选中保留/暂停/重启的排序及新颖性风险；（4）最少、最有判别力的下一轮实验，写清预测、失败判据、同质量与维护计费；（5）什么结果足以支持论文贡献，什么只能支持维护策略实测。暂不大规模实现。
