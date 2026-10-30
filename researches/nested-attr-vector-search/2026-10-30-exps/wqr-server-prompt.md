# 交付实验服务器：只实施WQR，不再自行制定路线

你负责C++实施与真实实验，本地研究端已完成机制选择与算法定义。不要再输出候选方向、要求重新做机会诊断后才能设计，或擅自换成Memento/缓存调参/整scope扫描。

**8649e55后续执行优先级：先修复并闭合E0，再按本文继续E1/E2，不再询问二选一。** [实现审计与具体修复指令](wqr-8649e55-implementation-review.md)已在未修改源码上复现scope放宽、Euler兄弟误包含、数值与ID/slot错误、计数错误、reserve首次I越界及混合replay丢事件。现有小fixture成功不等于合同通过；修复前禁止把E1输出解释为WQR算法胜负。审计不改变核心方案与参数。

## 权威入口与工作范围

1. 完整阅读同目录[`wqr-index-spec.md`](wqr-index-spec.md)，包括§0前序勘误、§6搜索、§7生命周期、§8成本和§10已执行反例。
2. 原交接先§0勘误，按已有P0清单核语义/公共距离/两遍GT；历史审读用于避免已知错误，不覆盖最新冻结方案。
3. [`data/wqr-contract.json`](data/wqr-contract.json)为机械参数合同。规范冲突时以正文为准并报告，不能静默省略。
4. 本地探针不是生产代码：`experiments/wqr_semantics.py`实际验证bitplane/动态Euler；`experiments/wqr_hnsw_probe.cpp`实际验证proxy graph/zone gate及反例。Python不参与线上效率对比。
5. 阅读服务器0837918的第二轮联合诊断及规格§0.1。本轮64维证书负结果不是WQR的失败或正证据；不再实施64维cell/tuple细化、径向lift，也不把另造余空间编码设为WQR实施前置条件。

## 唯一主方案

对象唯一进入一个几何块及一个zone。块内完整资格mask按同一个slot计算。每块4个独立proxy向量及不交zone，在一张hnswlib HNSW路由：

`PortalAllowed(p,q) = any(ExactMask(block(p),q) & ZoneMask(p))`。

不是proxy源对象的资格，也不是整块非空就激活全部proxy。未准入proxy仍可导航。取得至多L个不同块后，在对应local HNSW按ExactMask过滤并合并top-k。

类别/数值/tag用固定64-bit位切片。目录是稳定owner ID＋动态Euler implicit treap，block内owner posting为稀疏slot lists。属性变化不改zone/几何图；M改owner posting；R为cut/link，不更新对象索引。

## 固定实施内容

- 新增独立`src/wqr_index.h`与`src/wqr_research.cpp`及build入口；不修改NodeANN的既有行为，不改正式历史CSV。
- C++实现完整Q/V/T/M/B/R/D/I，匹配当前replay sentinel、四种tag语义、内部节点直接归属、串行可见性。不能将未实现事件作为no-op。
- 实施§4确定性几何分块、r=4 farthest-first初选及最近proxy zone分配。
- 固定proxy副本生命周期：源对象删除仍保留proxy；空zone不可返回，空块回收全部proxy。按§7处理slot复用、外部ID、墓碑、split/容量/压实；不启用随机replace_deleted，不越界先插后split。
- 搜索严格一次portal调用：P=min(4L,total proxies)，ef至少P；返回排序后按块去重。callback不做状态性去重。最多L次local search，无GT停止、无隐式重试或全scope向量scan。
- hnswlib固定revision、全部FP32 squared-L2走RawL2Space；真实distance计数，不沿用漏底层的历史计数。

## 实验顺序，不需要自行选算法

1. 先用小fixture实现并通过全部语义/生命周期。必须包含真实支持域投影正例与中心反例、空结果、R后旧rank失效、internal owner、空tag、D/I与源对象删除后的proxy保留。
2. 再运行原WIKI/SIFT小数据；固定同一attributes/queries/tape，不另造有利分布。B0=1024,r=4,M16,efC200,local ef1024。禁止按新几何分块重新生成属性；旧K32/96条件下不同属性快照保留独立身份。先保存GeometryRoute/BlockGate/WQR各自精确proxy排序、同P/L去重后的离线route coverage参考，再运行线上搜索；不是让你重新选机制。
3. L取1,2,4,...到C并包含C；portal ef仅max(64,4L)、max(256,4L)。这不是开放搜索空间，不立即放大规模。
4. 对照NodeANN、GeometryRoute、BlockGate、WQR；后三者共享完全相同几何块/代表/图。另跑Eager-WQR以分离lazy准备收益。不是复现所有外部ANN论文。
5. 两遍GT；macro/pair均≥.999、invalid/duplicate/state=0才计胜出。记录实际mask块数、活跃zone、route coverage、conditional local recall、全部distance/edge/owner-rank工作及全成本。
6. 加V/T/M/R与I/D短窗口；B按真实冻结批量移动语义运行。所有同步重建和窗口末未清偿工作计费；原始数组、local图、proxy两份payload、bitplanes、owner列表、Euler与峰值内存全计。
7. 对全部配置保留离线exact-proxy归因，而非仅质量失败点；这是固定proxy打分参考，不是Recall上界。近似路由的coverage可能偶然更高，差值必须带符号。逐query分别计算route coverage、conditional local recall与final recall后聚合，禁止将两个macro均值相乘。不得在线使用参考排序或GT，也不得把其成本写成免费线上操作。

## 明确不做

不增加embedding修改、MERGE冲突语义、多写者MVCC、RL分区、量化、维护阈值优化、新图剪枝或另一种基础索引。它们不在当前replay合同中，不以“补完整系统”为由扩散。

若精确proxy路由本身只在L≈C时过门，或zone gate无额外作用，停止扩规模，回传完整失败。你不负责临时设计替代算法；本地研究端负责根据结果修订。

## 交付

提交实际源码、构建/版本记录、每query/每update小日志、结构审计、同质量query-update-space曲线和全部失败配置。报告区分：实现缺陷、近似表示失败、资格求值成本失败、完整维护成本失败。禁止用有限合成正例或只赢NodeANN便宣称已获论文贡献。
