# 2026-10-29：阶段 1（文献与基线对齐）+ 阶段 2（候选方向）

按任务书每轮 autoresearch 输出格式。

## 1. 本轮研究问题

带层级特性的多属性混合向量搜索 + 属性频繁更新：固定三个基线（PE-ONLINE/PE-OFFLINE/NODEANN）的定义，核查动态 ANN、混合过滤、联邦搜索三条文献线，给出候选方向与排序。**本轮无实现、无实验。**

## 2. 相关工作与可能重合点

67 篇/族核查完成（[stage1-literature.md](stage1-literature.md)）。与本方向最可能重合的：

- **ELI**（PVLDB 19(4)'25，弹性子索引选择）与 C1 的"为谓词选子索引"最近——差别在层级节点 vs flat 标签组合键。
- **Taily/Rank-S/Dynamic Shard Cutoff**（SIGIR'13/CIKM'12/SIGIR'18）与 C1 的路由/预算机制最近——差别在文本 flat shard vs 谓词决定的层级子树。
- **EMA/Curator** 与 C3 的属性更新协议最近——差别在平铺标签 vs 层级属性。
- **Lu & Callan'04 / SHiRE / Hierarchical BM25** 是仅有的层级联邦先例，均为弱先例（poster/学位论文/preprint）。

核查中解决的事实矛盾 6 项（Filtered-DiskANN 无更新协议、SeRF=SIGMOD'24、FreshDiskANN 无正式版、SPFresh=SOSP'23、"Diff-Index/LEAPER"不存在、MCI 自身静态）——详见文献文档 §1。

## 3. 算法或索引的明确改动

本轮不改任何算法。产出的是定义与判据：

- [baselines.md](baselines.md)：三基线的存储/查询/更新/成本复述（§5 与任务书逐条对齐，§6 两个待对齐问题）。
- 空白判定 G1–G7（文献文档 §3）。
- 实测硬约束 4 条（文献文档 §4），候选设计不得违背。

## 4. 相对基线的预期变化

见 [stage2-candidates.md](stage2-candidates.md)：C1 预期 ≥10k 桶同 recall 下 ≥2×（失败判据 <20% 即关闭）；C3 预期 MERGE 惰性方案更新延迟远低于重建、召回不变。其余候选未承诺数值。

## 5. 需要运行的实验与变量

下一轮（进入实现前的两个可证伪前置实验）：

1. **C1 oracle 分析**（纯离线）：WIKI/SIFT ≥10k scope 查询的真实 top-k 节点分布、oracle 访问量节省上界、质心/样本投票两种摘要的节点命中率。变量：scope 桶、摘要类型、k'。
2. **C3 MERGE 三臂**：惰性并存 vs 重建合并 vs 后台增量合并 × 更新延迟/查询开销/空间。变量：MERGE 冲突率、更新比例 20%/50%。

## 6. 实际结果及失败结果

本轮无实验。失败/负结果引用：C6 因与 05 冻结方向重叠暂缓；C7 因 ELI/Curator 重叠降级；joint-embedding 路线因语义偏移+更新灾难排除。

## 7. 结果是否支持假设

阶段 1–2 产物支持继续：G1/G2 空白置信度高，C1 与本仓库实测瓶颈（D5）精确对齐。**支持"先跑 oracle 证伪再决定实现"的推进方式。**

## 8. 下一轮唯一最重要的问题

**oracle routing 在 WIKI ≥10k scope 上的 item 访问量节省上界是多少？**（决定 C1 立项与否）

## 附：本轮产物

- [baselines.md](baselines.md)、[stage1-literature.md](stage1-literature.md)、[stage2-candidates.md](stage2-candidates.md)、[../search-log.md](../search-log.md)（检索词/覆盖/待补查）。
- 方向入口 [../README.md](../README.md) 建立；阶段 1、2 完成，阶段 3（形式化）待 C1 oracle 结果后启动。
- 未运行实验、未改代码；文献中标注"未核实/尚未充分排查"的条目不得当结论引用。
