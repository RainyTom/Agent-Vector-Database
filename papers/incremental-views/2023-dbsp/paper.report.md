# DBSP: Automatic Incremental View Maintenance for Rich Query Languages

分析版本：正式PVLDB PDF；不能用arXiv四作者替换正式五作者。正文定位：§3–4 pp.1604–1608；§7–9 pp.1610–1612。

- QΔ=D∘Q∘I
- Z-set有符号权重与distinct跨零变化
- chain rule、线性/双线性算子增量化

保证与条件：给出可组合IVM语义与形式化理论；不是专门搜索索引。

成本：线性算子按delta，join通用O(|DB||ΔDB|)且保存局部关系；计划构造线性不等于更新运行线性；MIN删除需额外状态。

作者实证（本轮未复现）：
- 作者报告SQL compiler通过700万SQL Logic Tests、理论Lean形式化；本轮未重跑，实现本身非已形式化验证

限制：
- 正式版五作者与预印本四作者元数据不同
- 完整性能评价留待后文
- 变更计划通常需重新灌入状态
- 可增量化不等于快，不提供filtered ANN/top-k保证

迁移假设：[INFERENCE] 为共享资格状态的插删、重数、事务快照与传播规则提供语义基础；非独立主索引候选。

综合审读：`researches/nested-attr-vector-search/2026-10-30-exps/factorized-views-transfer-review.md`。来源及文本SHA见同目录日期化metadata。
