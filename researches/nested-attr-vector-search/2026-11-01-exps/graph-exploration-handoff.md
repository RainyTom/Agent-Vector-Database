# 图索引探索交接：当前约束、证据与完整文件入口

## 0. 当前结论——先读

**没有已获批准的新图搜索方案。** 最近的PQ编码Trie主检索路线被用户明确驳回，不能继续当作当前实施任务。DAG-CoPack是物理分组定位暂停；DAG-JBF只是精确资格组件。图负责主要几何导航，DAG与多属性必须实质参与索引/搜索。

本轮交接目的是让下游独立推进探索，而不是执行旧冻结任务。直接使用[完整提示词](graph-exploration-prompt.md)。这些材料所在交付commit才是版本入口，不默认main最新；本地研究分支为review/handoff-20261029，最新基础决策为f5dfb56，交付commit由发送者另行给出。

### 当前用户要求

- 主目标：同质量端到端时间优于合理基线；内存辅助，不以距离计数减少代替时间收益。
- 数据：完整概念DAG、多父继承、普通属性硬过滤、向量top-k；item可以直接属于任意节点，包括根和内部节点。
- 当前每item一个direct-owner；不要无声改成多标签对象或路径来源语义。
- 主要近邻检索必须用图；不再以PQ编码树替代主搜索，也不以每概念图独立搜索合并为默认主线。
- 允许成熟组件迁移/组合，需引用；不把代码实现、SIMD或物理组织作为主创新，也不要求每个组件原创。
- 方案要有动机与完整构建/查询机制。用户要求从数据定义逐步讲清，不要以大量新缩写代替结构。
- 优先理论推理和具体方案，最少必要验证；不要频繁以oracle实验替代设计，不先扩规模或铺全部维护矩阵。

## 1. 已固定教学数据（不是实验数据）

[机器可读12-item例](data/dag-12-item-teaching-example.json)：

- R→A/B，A→C/F，B→E/F。
- 每概念两个直属item：r1/r2、a1/a2、b1/b2、c1/c2、e1/e2、f1/f2。
- 6个概念、12个唯一item，F只建一个概念节点。
- recursive A包括A/C/F直属item；recursive B包括B/E/F；A AND B仅包括f1/f2。
- 根R、内部A/B的直属item必须参与递归范围。
- 实际向量与普通属性未在此教学JSON提供；之前的示意码字、ordinal和距离不是实测，更不是下一方案的必需数据布局。

## 2. 最新纠偏：编码与属性没有自动关联

编码长度由子空间划分决定，分支由码字决定，与分类数量、属性字段数或DAG深度没有一一对应。一一对应不是必要条件，但缺少有效联合剪枝联系是该设计的真实缺口。

在对话中的12-item示意码字下，递归A允许owner={A,C,F}，第一层三个编码分支分别支持：

| 分支 | owner支持 | 合法部分 | 结论 |
|---|---|---|---|
| [1] | R/A/C/F | A/C/F | 混合 |
| [2] | R/B/E/F | F | 混合 |
| [3] | C/E | C | 混合 |

没有一个分支可整体拒绝或整体通过。这里只是对手工示例的结构分析，不是ANN实验。它说明“附加属性摘要”不自动意味着少搜索节点；当所有分支都混合时，树访问、堆、摘要和posting求交可增加工作。

后续图方案也必须回答同样的问题：具体省略哪类访问、为什么不破坏有效导航、新增判断成本是什么。仅给图节点附加标签不够。

## 3. P0：立即必读

| 文件 | 目的 |
|---|---|
| [完整探索提示词](graph-exploration-prompt.md) | 当前任务、约束、输出格式与执行边界 |
| [最新report §13及本轮交接追加](report.md#13-编码树路线被用户驳回回到图主检索约束) | 驳回与未实现/实测失败的区分 |
| [12-item数据定义](data/dag-12-item-teaching-example.json) | 根/内部节点直属item、共享F、同对象AND |
| [3aaa0ed复核与汇总勘误](../2026-10-31-exps/eligibility-3aaa0ed-review.md) | 纯图/混合NodeANN区别，实际证据与被纠正数字 |
| [a1db223复核report开头及§3–5](report.md) | 7.64%口径、全局/局部执行器接线、覆盖条件 |
| [原始距离内核](../../../codes/directory-aware-vector-attr-search/src/raw_l2_space.h) | 统一FP32 squared-L2、计数和GT边界 |

## 4. P1：制定机制前按主题读

| 主题 | 入口 | 保留的边界 |
|---|---|---|
| WQR | [c1c24e8固定报告](https://github.com/RainyTom/Agent-Vector-Database/blob/c1c24e8/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/wqr-implementation-report.md) | 固定proxy覆盖损失，不能靠多代表调参换名重启 |
| 普通二跳与邻域页 | [502753c报告](https://github.com/RainyTom/Agent-Vector-Database/blob/502753c/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/neighborhood-report.md)、[本地neighborhood_filter.h](../../../codes/directory-aware-vector-attr-search/src/neighborhood_filter.h) | 取得同一候选更便宜≠减少实际几何状态 |
| H/闭包与小ef | [5fc1be3报告](https://github.com/RainyTom/Agent-Vector-Database/blob/5fc1be3/researches/nested-attr-vector-search/2026-10-30-exps/closure-server-report.md)、[2f8c4e0报告](https://github.com/RainyTom/Agent-Vector-Database/blob/2f8c4e0/researches/nested-attr-vector-search/2026-10-30-exps/closure-small-ef-report.md)、[closure_index.h](../../../codes/directory-aware-vector-attr-search/src/closure_index.h) | 邻接变化不等于expanded下降；不自动扩N/大ef |
| 资格执行与基线 | [3aaa0ed服务器报告](https://github.com/RainyTom/Agent-Vector-Database/blob/3aaa0ed/researches/nested-attr-vector-search/2026-10-31-exps/eligibility-server-report.md)、[本地eligibility_index.h](../../../codes/directory-aware-vector-attr-search/src/eligibility_index.h) | 优先与独立勘误一起读，别把原聚合错误继承为事实 |
| 条件几何信息 | [7756064报告](https://github.com/RainyTom/Agent-Vector-Database/blob/7756064/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/report.md)、[0837918联合诊断](https://github.com/RainyTom/Agent-Vector-Database/blob/0837918/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/joint-certificate-report.md) | WIKI低维信息不足，oracle不是免费线上结构，不泛化否定全部证书 |
| DAG现有实现 | [dag_scope_ann.cpp@a1db223](https://github.com/RainyTom/Agent-Vector-Database/blob/a1db223/codes/directory-aware-vector-attr-search/src/dag_scope_ann.cpp)、[common.h](https://github.com/RainyTom/Agent-Vector-Database/blob/a1db223/codes/directory-aware-vector-attr-search/src/dag_scope_common.h)、[定位报告](https://github.com/RainyTom/Agent-Vector-Database/blob/a1db223/researches/nested-attr-vector-search/2026-11-01-exps/triehi-baseline-and-dag-positioning.md) | online/mat_all是全局图，partition_top才分区；只有scope OR；本地分支不自动含这组文件 |
| TrieHI | [本地全文](../../../papers/prefix-vector-search/2606.16903-directory-aware/paper.md)、[笔记](../01-directory-aware-acceleration/paper-notes-2606.16903.md) | 比较元数据层、canonical tree；不能声称所有Trie无法表达DAG，或未发表可不引用 |
| 跨方向借鉴 | [cross-domain-index-transfer](../2026-10-30-exps/cross-domain-index-transfer.md)、[pruning-index-directions](../08-dynamic-hybrid-vector-search/2026-10-29-exps/pruning-index-directions.md) | 方法可迁移，须明确新问题下非平凡变化，不照搬或换名 |

本地代码的存在不意味着与服务器固定commit字节相同。后续修改前记录实际版本，禁止用“同文件名”替代版本证明。

## 5. P2：完整历史追溯

- [原完整交接research-handoff](../08-dynamic-hybrid-vector-search/2026-10-29-exps/research-handoff.md)：先读§0勘误，按需要追正式162次、历史420次、LES、剪枝与路由等家族。
- [原上游阅读清单](../08-dynamic-hybrid-vector-search/2026-10-29-exps/upstream-reading-list.md)：有P0/P1/P2和数据/源码定位，但历史“当前任务”会过期，以本入口最新用户决策为准。
- [方向总导航](../index.md)：历史协议保留，不代表自动允许重跑。

重要纠偏：多父暴露比例不是树化漏答率；inclusive集合递推在DAG也成立；路径展开树可保留语义但有复制成本；逻辑scope交叠不是物理图覆盖冗余。不要只靠一个“新问题”标签宣称创新。

## 6. 本轮全部理论材料与状态

| 文件 | 状态 |
|---|---|
| [dag-copack-design](dag-copack-design.md) | 物理分组主线定位暂停，未实施，不是实测失败 |
| [dag-jbf-design](dag-jbf-design.md) | 逻辑资格摘要组件；不是完整新向量搜索机制 |
| [dag-qualified-code-search](dag-qualified-code-search.md) | 编码树主检索被驳回，历史草案 |
| [dag-unified-search-plan](dag-unified-search-plan.md) | 含动机与协同构想，但整体已被驳回 |
| [dag-unified-index-construction](dag-unified-index-construction.md) | 被驳回方案的历史构建说明，不实施 |
| [有限元数据反例JSON](data/dag-positioning-review-check.json) | 先前确已执行的小DAG/覆盖反例，不是ANN性能数据 |
| [本轮报告](report.md) | 逐次追加，前段旧“当前方案”需结合后段驳回解释 |

## 7. 图、论文PDF与完整性

- [历史OFFLINE PNG](figures/rejected-jbf/offline-index.png)、[ONLINE PNG](figures/rejected-jbf/online-search.png)。SVG源为同目录offline-index.svg/online-search.svg，文件SHA见[manifest](figures/rejected-jbf/manifest.json)。
- **这些图展示较早的R/A/B/C及x0…x5属性例，不是最新12-item例；它们是旧资格方案的说明，不是新的获批图搜索方案。** 原来位于ignored work，本轮移入研究目录归档；不提交整个work。
- BioVSS [PDF](../../../papers/multi-vector-search/2412.03301-biovss/paper.pdf)、[完整文本](../../../papers/multi-vector-search/2412.03301-biovss/paper.md)、[审读笔记](../../../papers/multi-vector-search/2412.03301-biovss/paper.report-2026-10-29.md)。本轮PDF首次入库，8591803字节，SHA与2026-10-29快照一致，见[2026-11-01归档元数据](../../../papers/multi-vector-search/2412.03301-biovss/metadata-2026-11-01.json)。旧“PDF local-only”日期快照保留；总索引已更新归档状态，未新增论文或重新宣称精读。
- 真实原始向量、大图索引、编译二进制、服务器逐query大日志不在本交付包；文档里的work路径不是可下载数据。新的AI必须先确认实际输入在哪，不能用教学例冒充真实实验。

本轮只做交接整理、文件归档与一致性检查，没有新增图算法实现或ANN性能实验。
