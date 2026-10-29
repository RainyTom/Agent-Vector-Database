# 08 动态混合向量搜索：层级属性 + 多属性 + 频繁更新

任务书：2026-10-29 收到的研究简报（attachment）。范围比 01 方向（静态单查询 DSQ 加速）宽：多属性谓词（等值/范围/集合四语义/层级前缀子树）+ 属性高频更新 + 层级重挂 + 插入删除，且明确要探索 NodeANN 的联邦/分片搜索视角。

## 与既有方向的关系

- **01-directory-aware-acceleration**：静态 DSQ 的 NodeANN-first 加速（hnswlib 后端、B0/B1/B3 基线、D1–D6 判决），数据/工具/测量纪律全部复用。
- **04-certified-progressive-routing / 05-versioned-directory-search**：2026-10-19 alphaXiv 查重后冻结，仅作前序对照，不在此恢复。
- **07-laminar-edge-sharing**：静态共享边实验，结论供共享图路线参考。

## 阶段门（按任务书）

| 阶段 | 产物 | 状态 |
|---|---|---|
| 1 文献与基线对齐 | [基线复述](2026-10-29-exps/baselines.md)、[文献核查](2026-10-29-exps/stage1-literature.md)、[检索日志](search-log.md) | **完成**（2026-10-29，67 篇/族） |
| 2 候选方向 ≥5 + 排序 | [候选表](2026-10-29-exps/stage2-candidates.md)、[当日报告](2026-10-29-exps/report.md) | **完成**：主线 C1 层级联邦路由+不等预算，辅线 C3 层级更新协议 |
| 3 形式化 + 伪代码 + 可证伪假设 | C1 已被门禁实验否证（[report.md](2026-10-29-exps/report.md)）；形式化目标转向可回收摘要 + 一致视图 | 待启动 |
| 4 小数据原型 | 待定 | 未开始 |
| 5 单变量实验 | 待定 | 未开始 |

## 硬约束（继承）

- 索引与搜索唯一语言 C++；原始 FP32 距离统一 hnswlib `RawL2Space`（revision ca426729）；FAISS 仅离线 k-means；SIMD/布局不做贡献。
- 基线定义不得偷改（PE-ONLINE / PE-OFFLINE / NODEANN 按 [baselines.md](2026-10-29-exps/baselines.md) 的复述执行）。
- 主数据：WIKI（真实树）+ SIFT1M 六变体；测量纪律：并行只跑独立任务，报告用串行。
