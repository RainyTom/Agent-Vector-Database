# 2026-10-31｜续学：少边、少展开与质量—时间支配

## 目标

将服务器2f8c4e0小ef实测接入知识库，纠正“预算小了就能救方案”“邻接更稀疏就会少展开”“共同编译成本只是新增LCA”的过强直觉。本轮是证据学习，不重新运行服务器已报告实验。

## 已读与实际执行

已读固定commit的[完整报告](https://github.com/RainyTom/Agent-Vector-Database/blob/2f8c4e0/researches/nested-attr-vector-search/2026-10-30-exps/closure-small-ef-report.md)、[复现记录](https://github.com/RainyTom/Agent-Vector-Database/blob/2f8c4e0/researches/nested-attr-vector-search/2026-10-30-exps/experiments/closure-small-ef-reproduction.md)，解析公开table1-points.csv的64行分组小聚合，重算四个全部非空预算点的A/U对H支配、质量差与时间比。未读取本机不存在的原raw，也未重跑ANN或回归测试。

本地[算术与限制记录](../../nested-attr-vector-search/2026-10-31-exps/data/closure-closeout-audit.json)，研究侧[完整收尾](../../nested-attr-vector-search/2026-10-31-exps/report.md)。不新建重复论文或关系记录。

## 本轮学到什么

1. **评价不用先争论硬质量门。** 若某点同时更慢且macro/pair更低，它已被正常基线支配。四个ef15/20工作点中，A和同池U都严格支配H。小预算低Recall点应保留，但不是自动成为可用取舍。
2. **存在确定工作量下限时，删边可能完全不减少状态数。** 当前合法堆在E<ef时填不满，scope骨架使全展开；真实非空强制行只占20.0%～51.9%，却贡献约77.0%～84.8%展开。该性质从有限例子获得了实际工作量证据。
3. **这个下限也不是全部原因。** 去掉E<ef行后，H/R展开仍无系统性下降。不能自动推导“解耦堆一定救回方案”，它是尚未做的新算法实验。
4. **共享成本必须有无机制控制。** U编译占79.8%～88.3%，H占82.0%～89.3%；共同逐源候选rank/scope路径已昂贵。两占比差不等于新增LCA成本的因果估计，扣除编译也不是优化后性能预测。
5. **校验记录数不是独立样本数。** 报告3627条强制记录可拆为2007条非空重复记录、1620条空scope记录；空scope满足0=0=0，不应扩大成非空遍历证据。三次运行也不增加query独立性。

## 结论、限制与下一步

当前闭包原型停止扩展，旧任务书只供复现，不自动上调ef、扩N或启动Dynamic。保留局部性质与组件，不能用其替代缺失的端到端论文贡献。新机制需要先有具体可区分行为和质量—成本预测，再决定是否实施；不是换个索引名字继续跑。

服务器真实维护、压实、staleness、并发仍未测，不记录为0。结论只覆盖现有数据、初始快照与旧query；SIFT全部非空48个qidx、历史确认16个的覆盖边界保留。后续知识学习仍沿既有SQL计划/一致性/编码队列推进，不假称后台自动执行。
