# BioVSS v2补充机制审读

承接 [2026-10-29报告](paper.report-2026-10-29.md)，不覆盖旧记录。本轮没有复现BioVSS性能；相关原文已重新核读，以下尺度/反例是研究者分析，不声称作者新增结论。

## 尺度与保证

[原文v2](https://arxiv.org/pdf/2412.03301v2) §4.2 Theorem4、PDF18（本地paper.md1058–1094）写1−min/max similarity作Hausdorff界。对单位向量有||q−x||²=2−2q·x，故应区分原H与H²/2尺度；单调排序不等于原始数值距离界。还需保留归一化、等集合大小、强点对分离与碰撞模型条件。

§5.3的connectivity是全点对相似度和，不是Hausdorff min–max聚合。它没有给Top-A计数列表/Top-T sketch截断的无损覆盖证书。

## 成员绑定与伪码边界

[解析构造，未新跑数值实验] Q={(1100)/sqrt2,(0011)/sqrt2}，V={(1010)/sqrt2,(0101)/sqrt2}；identity top2码下count和OR均相同，但H(Q,Q)=0，H(Q,V)=1。即使代理成员码无随机误差，集合聚合也可丢成员关系。

Algorithm6的capacity c与T、F/F2记号不一致；Definition8按成员累加与Algorithms1/3编码集合union对重复码处理的关系尚待核实现，不把伪码歧义当已完成的可执行规格。

## 与本项目的关系

L2AP已提供部分倒排的覆盖约束与prefix残差界，Seismic已有几何posting/max摘要；本研究的比较见 [条件证书机制审读](../../../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/mechanism-assessment.md)。关系索引中新增的是researcher_synthesis比较，不是BioVSS作者引用/继承这些方法。

不能直接把BioHash+属性bitmap当新机制，也不能把单查询集合Hausdorff替换当前raw L2。FlyHash/BioHash基础证明、正式稿逐式差异仍未完成复核。
