# 08 动态混合向量搜索：层级属性 + 多属性 + 频繁更新

任务书：2026-10-29 收到的研究简报（attachment）。范围比 01 方向（静态单查询 DSQ 加速）宽：多属性谓词（等值/范围/集合四语义/层级前缀子树）+ 属性高频更新 + 层级重挂 + 插入删除，且明确要探索 NodeANN 的联邦/分片搜索视角。

## 当前方案状态（先读这个）

[**scheme-status.md**](2026-10-29-exps/scheme-status.md)：正式两遍GT完成162运行；原始WIKI子树root3626存在Q/U排序交叉，root410/root828在已测范围无交叉。旧420次及早期原树inline计时已降为诊断，失败与操作计数保留。见 [原树/GT协议](2026-10-29-exps/wiki-topology-protocol.md) 与 [报告及正式图§6.5起](2026-10-29-exps/report.md)；不宣称新索引优于公开作者实现。

## 与既有方向的关系

- **01-directory-aware-acceleration**：静态 DSQ 的 NodeANN-first 加速（hnswlib 后端、B0/B1/B3 基线、D1–D6 判决），数据/工具/测量纪律全部复用。
- **04-certified-progressive-routing / 05-versioned-directory-search**：2026-10-19 alphaXiv 查重后冻结，仅作前序对照，不在此恢复。
- **07-laminar-edge-sharing**：静态共享边实验，结论供共享图路线参考。

## 阶段门（按任务书）

| 阶段 | 产物 | 状态 |
|---|---|---|
| 1 文献与基线对齐 | [基线复述](2026-10-29-exps/baselines.md)、[文献整理](2026-10-29-exps/stage1-literature.md)、[检索日志](search-log.md) | 已整理候选；重叠篇目/版本与部分论断仍需逐条核查，不称“67篇全文核实完成” |
| 2 候选方向 ≥5 + 排序 | [候选表](2026-10-29-exps/stage2-candidates.md)、[当日报告](2026-10-29-exps/report.md) | 保留候选；简单摘要的离线结果不能关闭整个路由方向 |
| 3 形式化 | [旧局部 churn 模型](2026-10-29-exps/stage3-formalization.md)、[新三臂协议](2026-10-29-exps/dynamic-triad-protocol.md) | 以新协议区分 ITEM/批量/子树更新，不再推断 O(1) 维护或必然交叉 |
| 4 原型 | `churn_probe.cpp` 旧局部 T/C 臂 | 有历史观测；alpha 非唯一 ITEM 覆盖，不作为同 trace 端到端胜负 |
| 5 一致性与摘要 | `churn_concurrent.cpp`、`summary_reclaim_probe.py` | 旧并发探针有同步缺陷；摘要是模型，均不能替代同协议动态三臂 |
| 6 同协议动态三臂 | NodeANN / PE-ONLINE / PE-OFFLINE，逐查询当前状态 GT | 历史420运行：126筛查+234主矩阵+54仅移动+6pilot；inline-GT计时降为诊断，非并发吞吐结论 |
| 7 原始层级与GT干扰控制 | 完整WIKI子树、占用匹配平衡树、独立C++动态GT tape | 正式162运行全部过门；测量GT调用0、81拓扑配对与逐事件等价检查通过；root3626交叉，另两cohort无观测交叉 |

## 硬约束（继承）

- 索引与搜索唯一语言 C++；原始 FP32 距离统一 hnswlib `RawL2Space`（revision ca426729）；FAISS 仅离线 k-means；SIMD/布局不做贡献。
- 基线定义不得偷改（PE-ONLINE / PE-OFFLINE / NODEANN 按 [baselines.md](2026-10-29-exps/baselines.md) 的复述执行）。
- 主数据：WIKI + SIFT；精确标注完整原始WIKI子树、合成父子关系或合成普通属性，不冒充全量原树。独立进程并行任务节省时间，真实披露进程重叠，不称为隔离串行测量。
