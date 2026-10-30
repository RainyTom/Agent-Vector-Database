# Qd-tree: Learning Data Layouts for Big Data Analytics

分析版本：SIGMOD2020作者PDF16页。正文定位：§2–7；§5.2.4 NeuroCuts迁移来源；§8–9。

- 语义完整的query-data routing tree
- 从workload抽取cuts，路径为合取
- greedy及Woodblock PPO构树
- 数值区间、类别mask和advanced cuts

保证与条件：greedy近似分析有tree-submodularity条件；未来数据效果依赖分布条件；不提供ANN召回保证。

成本：greedy O(|V||P|depth)；构建/训练/路由成本计入，非在线常数过滤。

作者实证（本轮未复现）：
- 作者Table2 TPC-H访问比例Random56%、BU+46.1%、Greedy26.3%、RL25.8%
- 原实验为分析查询，不是向量ANN时延；近期更新维护未验证

限制：
- 2020经典工作，不包装为2026新索引
- workload-driven cuts和RL本身已有直接前序
- 跨属性边际mask仍可能放过无同对象见证的块
- 频繁MOVE改变cut归属，物理迁移和局部图修复另计
- author code入口未核实

迁移假设：[INFERENCE] 谓词与几何共同组织共享微分区；将scan-skip目标改为可测候选搜索成本和更新约束，先greedy不强制RL。

综合审读：`researches/nested-attr-vector-search/2026-10-30-exps/cross-domain-index-transfer.md`。来源及文本SHA见同目录日期化metadata。
