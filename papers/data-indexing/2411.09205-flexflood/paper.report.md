# FlexFlood: Efficiently Updatable Learned Multi-dimensional Index

分析版本：arXiv2411.09205v2,2024-11-15（原入口无版本号，正文明确v2）。正文定位：§3–7；Appendix A,C,D,E。

- Flood的D−1维grid与一个排序维度
- cell内Btree
- 沿轴slab split/merge/equalize自适应占用
- 初始分区与sort dimension学习

保证与条件：附录C在净增长模型N≈N0+kΔ和(prod x_d)(sum x_d)≤DNlogN等条件下给摊销O(DlogN)；不包含附加ANN维护。

成本：split/merge可触达X个cell描述符及O(N/x_d)点；不是最坏常数更新。

作者实证（本轮未复现）：
- 作者相对UpdatableFlood查询3.3–10×；更新耗时≤2×
- 5D OSM上R-tree查询更快
- Btree比静态array Flood查询慢1.5–2.7×

限制：
- 仅低维有序属性，不是128/1024维向量索引
- rebalancing不重新优化初始sort dimension/partition counts
- README API enumerate完整范围结果；insert/erase坐标tuple为set语义，重复属性的不同对象需ID payload
- MIT，第三方许可证另列；未运行作者代码

迁移假设：[INFERENCE] 对时间/价格等少量数值属性组织动态cell，输出局部候选视图；外部目录与多值标签仍需精确接口。

综合审读：`researches/nested-attr-vector-search/2026-10-30-exps/cross-domain-index-transfer.md`。来源及文本SHA见同目录日期化metadata。
