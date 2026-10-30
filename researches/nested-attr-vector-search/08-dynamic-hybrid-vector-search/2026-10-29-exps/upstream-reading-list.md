# 上游AI阅读清单

## 最新：ef15/20固定小预算（用户纠偏后的正常查询预算）

先读 [小预算完整报告](../../2026-10-30-exps/closure-small-ef-report.md)、[复现命令](../../2026-10-30-exps/experiments/closure-small-ef-reproduction.md)、[48次原始结果与聚合](../../2026-10-30-exps/data/closure-small-ef-run1/)。算法与网格未改，固定k=10、efSearch=15/20，A/U/R/H×3次串行。

- **结论**：A的macro 0.995～1.000、0.28～0.41ms；H仅0.934～0.953、5.7～12.0ms。同预算H比A慢14～41倍且Recall更低，不构成有竞争力的质量—时间点。
- **机制**：[H/R expanded配对](../../2026-10-30-exps/data/closure-small-ef-run1/comparisons.json)在同ef下仍几乎相同；[强制遍历与编译占比](../../2026-10-30-exps/data/closure-small-ef-run1/aggregate.json)显示`eligible<ef`占非空查询20%～52%、占总展开77%～85%，U同样慢（编译占80%～89%）。
- **旧CSV归因**：[termination-attribution](../../2026-10-30-exps/data/closure-small-ef-run1/termination-attribution-7654f1e.json) 只读分析大预算结果，`all_forced_property_checks_pass=true`。
- 边界：两数据各200条旧query、一个快照；SIFT确认仅16个非空qidx；无Dynamic、无维护包络、无扩大N，更新成本仍记未测。

## 上一轮：7654f1e closure真实数据六臂static

先读 [服务器完整报告](../../2026-10-30-exps/closure-server-report.md)、[冻结任务书](../../2026-10-30-exps/closure-server-plan.md)、[实际复现](../../2026-10-30-exps/experiments/closure-server-reproduction.md)。原核心和调度未改，WIKI/SIFT路径及SHA沿用502753c，完成72次计时、4次独立GT和2次机制审计。

- **机制**：[真实池采样邻接及heredity](../../2026-10-30-exps/data/closure-server-run1/static/mechanism.json)、[同ef实际展开与质量](../../2026-10-30-exps/data/closure-server-run1/static/matched-ef-calibration.json)。H改变有效邻接，但expanded没有有量级下降。
- **六臂所有点**：[配置/质量失败](../../2026-10-30-exps/data/closure-server-run1/static/configurations.json)、[每重复分层结果](../../2026-10-30-exps/data/closure-server-run1/static/group-runs.json)、[逐漏召回诊断](../../2026-10-30-exps/data/closure-server-run1/failure-diagnostics.json)。WIKI H确认质量不足；SIFT H质量达标但本样本明显慢于真实NodeANN。不要将WIKI时延比写成同质量效率结论。
- **门控**：[原stage-decision](../../2026-10-30-exps/data/closure-server-run1/static/stage-decision.json)、[执行说明](../../2026-10-30-exps/data/closure-server-run1/dynamic-stage.json)。两个数据集均未放行；SIFT非空仅16个qidx，覆盖不足与样本内成本负结果分开。真实dynamic和维护包络未跑，不能填0或外推E0。
- **追溯**：[manifest](../../2026-10-30-exps/data/closure-server-run1/static/manifest.json)、[全部runs](../../2026-10-30-exps/data/closure-server-run1/static/runs.json)、[work原始CSV/日志路径与SHA](../../2026-10-30-exps/data/closure-server-run1/static/raw-inventory.json)。大raw/向量不上传。

此前NPI、WQR、维护和几何路线仍按各自历史边界阅读，不与本轮预计算GT、绑核和时间口径混比。


## 上一轮：谓词可寻址邻域，不是WQR新胜出结果

按研究端2456849固定方案完成C++小原型和E1/E2：同候选C快于普通二跳B，但同质量下慢于真实NodeANN A，优先宽scope＋多属性AND也未逆转；E3未放行。不要将候选取得成功改写为端到端胜出，也不要将这组静态结果当动态能力。

1. [完整最新报告、分位数、空间与图](../2026-10-30-exps/neighborhood-report.md)。
2. [事前协议](../2026-10-30-exps/neighborhood-protocol.md)、[实际构建与复现/输入限制](../2026-10-30-exps/experiments/neighborhood-reproduction.md)。
3. [全36次配置与协议核验](../2026-10-30-exps/data/neighborhood-run1/analysis/report.json)、[B/C结果/导航/距离一致性](../2026-10-30-exps/data/neighborhood-run1/audit/audit.json)。
4. [逐空/非空/profile/重复结果](../2026-10-30-exps/data/neighborhood-run1/audit/confirmation-populations.csv)、[原始逐query和失败预算](../2026-10-30-exps/data/neighborhood-run1/runs/)、[E3不放行记录](../2026-10-30-exps/data/neighborhood-run1/audit/e3-decision.json)。
5. [邻域索引/共同搜索](../../../../codes/directory-aware-vector-attr-search/src/neighborhood_index.h)、[精确资格](../../../../codes/directory-aware-vector-attr-search/src/neighborhood_filter.h)、[执行器](../../../../codes/directory-aware-vector-attr-search/src/neighborhood_research.cpp)。基线仍是dynamic_triad arm A/scan512，不是PE-OFFLINE。

WIKI是已有完整原始子树，SIFT为12000活对象的已有层级；每数据200条旧Q、qidx分组校准/确认，未运行源更新。大向量/二进制不上传，manifest给出SHA和本地来源；读取链接不等于拿到完整数据。WQR独立收尾没有被本轮冒称完成；新颖性正文核查仍属研究端，不能由C/B时间收益推出。

## 历史总索引（以下原文保留）


用途：独立审核全部已尝试路线、成功/负结果和证据缺口，决定下一步。**不要默认接受上一模型建议。** 实验/源码基线为 `19ad280`；本页与完整交接文档同一提交。默认GitHub `main` 可能落后，请使用分享者提供的固定commit链接或 `autoresearch/trie-20261028` 分支。相对链接从本页所在commit解析。

**独立复核入口（本轮新增）**：[事实/基线/候选排序/最小实验/论文门槛](independent-review.md)、[原文机制与新颖性证据](literature-mechanisms-review.md)、[162次离线重算及源SHA](data/independent-review-audit-20261029.json)。先完整阅读下列原交接，再对照复核；后者没有新增ANN性能数据，也不把历史方案推荐视为用户批准。

**交付实验服务器**：[最终执行提示词](experiment-server-prompt.md)。按用户后续决策，以NodeANN为主基线、限制基线优化和实现规模；逻辑归属/物理驻留解耦是待判别的新候选，不覆盖历史实验结论，也不代表已验证的新颖方案。

**用户最新纠偏：索引机制优先。** 上述维护任务书已暂停默认执行；当前入口是[新索引服务器提示词](index-mechanism-server-prompt.md)，依据为[条件化剪枝与稀疏表示候选](pruning-index-directions.md)。BioVSS原文机制与L2AP/Seismic等重合已核，不能把延迟迁移或普通稀疏倒排当新索引贡献。

**2026-10-30服务器实测交付**：[当日报告及图](../2026-10-30-exps/report.md) → [至多两候选/前序/证明/成本](../2026-10-30-exps/mechanism-assessment.md) → [事前协议](../2026-10-30-exps/conditional-geometry-protocol.md) → [真实复现/计时修正](../2026-10-30-exps/experiments/reproduction.md) → [完整run2数据](../2026-10-30-exps/data/conditional-geometry-run2/)。SIFT经典条件界有信息，WIKI独立坐标界失败；取得成本和新颖性未通过，不构造“新索引赢了”结论。下列历史清单仍供回源，不替代此最新机制门结果。

**同日第2轮，先读这一归因修正**：[联合证据实测/图/阶段门](../2026-10-30-exps/joint-certificate-report.md)、[两族结构/前序/成本/反例](../2026-10-30-exps/joint-certificate-analysis.md)、[复现及失败](../2026-10-30-exps/experiments/joint-reproduction.md)。WIKI64维完整联合oracle仍无排除，说明不是简单加联合cell便能补回原信息；SIFT很多联合收益在不过滤界上已有。尚无新共享取得操作，不将VA/PQ/simplex+F改名立项。原始173568记录只有16条源query，不能读成新大规模ANN结果。



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
