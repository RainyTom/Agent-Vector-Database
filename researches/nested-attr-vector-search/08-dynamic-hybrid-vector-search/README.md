# 08 动态混合向量搜索：层级属性 + 多属性 + 频繁更新

任务书：2026-10-29 收到的研究简报（attachment）。范围比 01 方向（静态单查询 DSQ 加速）宽：多属性谓词（等值/范围/集合四语义/层级前缀子树）+ 属性高频更新 + 层级重挂 + 插入删除，且明确要探索 NodeANN 的联邦/分片搜索视角。

## 当前方案状态（先读这个）

[**scheme-status.md**](2026-10-29-exps/scheme-status.md)：当前可交付方案、成功的对照、被否证的假设（9 条）、相对公开基线的位置（空缺及原因）、以及产出端到端对比所缺的合并实验。

## 与既有方向的关系

- **01-directory-aware-acceleration**：静态 DSQ 的 NodeANN-first 加速（hnswlib 后端、B0/B1/B3 基线、D1–D6 判决），数据/工具/测量纪律全部复用。
- **04-certified-progressive-routing / 05-versioned-directory-search**：2026-10-19 alphaXiv 查重后冻结，仅作前序对照，不在此恢复。
- **07-laminar-edge-sharing**：静态共享边实验，结论供共享图路线参考。

## 阶段门（按任务书）

| 阶段 | 产物 | 状态 |
|---|---|---|
| 1 文献与基线对齐 | [基线复述](2026-10-29-exps/baselines.md)、[文献核查](2026-10-29-exps/stage1-literature.md)、[检索日志](search-log.md) | **完成**（2026-10-29，67 篇/族） |
| 2 候选方向 ≥5 + 排序 | [候选表](2026-10-29-exps/stage2-candidates.md)、[当日报告](2026-10-29-exps/report.md) | **完成**：主线 C1 层级联邦路由+不等预算，辅线 C3 层级更新协议 |
| 3 形式化 | [stage3-formalization.md](2026-10-29-exps/stage3-formalization.md)（局部索引 churn：T/C/R 三策略、成本模型、4 条预测与结果） | **完成**（P1 被否证） |
| 4 原型 | `churn_probe.cpp`：WIKI + SIFT D4bal + D6skew 三数据集 × T/C 臂 × ef∈{8,16,64}；召回不降、延迟 1.8–2.6×、压实 ≈2× 维护成本、α*≈0.6–0.8 | **完成**（[report.md](2026-10-29-exps/report.md)） |
| 5 单变量实验 | 线 1 并发一致性（[report.md](2026-10-29-exps/report.md)，`churn_concurrent.cpp`）；线 2 可回收摘要（`summary_reclaim_probe.py`） | **完成**：三处"显然的修复"被实测缩小/否证 |
| 6 合并实验 | 端到端 churn：局部索引 + 并发窗口 + 摘要退化合成一条曲线 | 下一步 |

## 硬约束（继承）

- 索引与搜索唯一语言 C++；原始 FP32 距离统一 hnswlib `RawL2Space`（revision ca426729）；FAISS 仅离线 k-means；SIMD/布局不做贡献。
- 基线定义不得偷改（PE-ONLINE / PE-OFFLINE / NODEANN 按 [baselines.md](2026-10-29-exps/baselines.md) 的复述执行）。
- 主数据：WIKI（真实树）+ SIFT1M 六变体；测量纪律：并行只跑独立任务，报告用串行。
