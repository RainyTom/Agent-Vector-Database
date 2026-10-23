# 2026-10-23：问题对照范围纠正

## 当日目标

纠正“时间窗约束的语义 episode 检索”的相关工作分类，避免用输入输出语义不同的单对象过滤检索支撑问题新颖性。

## 核查方法

按四个字段重新划分工作：查询单位、数据单位、返回单位、排序/约束语义。只有共享“轨迹、序列或子轨迹检索”语义的工作进入问题新颖性矩阵。

本轮是文献分类纠正，没有运行新的性能实验，也没有新增结果数字。

## 纠正结果

直接问题家族保留：

- ETQ：查询点序列到 top-k 轨迹；
- TISIS*：完整语义轨迹相似搜索；
- SimSub 与 Top-k (Sub-)Trajectories：子轨迹搜索；
- STKST-I：关键词、时间与位置约束下的语义轨迹 top-k；
- TrajPrism/OmniTraj：整轨迹语义检索，继续补查查询协议。

从问题新颖性对照中排除：

- ACORN、Filtered-DiskANN：属性过滤下的单向量 ANN；
- TANNS/TANGO：有效期或新鲜度下的单向量检索；
- CubeGraph/LIST：空间或空间文本约束下的单对象检索。

这些工作只保留为过滤图搜索、时间分区、空间分区和路由机制的实现参考，不作为直接相关工作、问题级基线或“本问题尚未解决”的证据。

## 结论

当前新颖性问题应只相对轨迹与子轨迹工作判断：ETQ 已覆盖“查询点序列 → top-k 轨迹”，SimSub 已覆盖子轨迹定位，TISIS* 已覆盖 POI embedding 序列匹配。候选增量必须是**绝对时间窗与持续时间约束下的连续语义 episode 检索，并返回可解释区间**，而不能来自与过滤 ANN 的表面差异。

## 局限

- ETQ 的后续引用闭包尚未完成；
- SimSub ICDE 2024/2025、STKST-I、TrajPrism/OmniTraj 仍有摘要级证据；
- 当前仍不能声称“别人未曾解决”。

## 下一步

1. 对 ETQ、SimSub、TISIS*、STKST-I 做前后向引用闭包；
2. 检索 `trajectory episode retrieval`、`time-constrained subtrajectory search`、`semantic trajectory segment search`；
3. 若直接工作已经支持时间窗、持续时间、连续 episode 与区间输出，则 pivot；否则冻结问题定义并实现 ETQ-embedding 直接基线。
