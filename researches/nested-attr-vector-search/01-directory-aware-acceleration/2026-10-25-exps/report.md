# 2026-10-25 研究记录：新问题背景与定义

## 当日目标与设置

基于 Directory-Aware/TrieHI 的目录作用域思想，寻找 nested attribute 背景下与其单查询 top-k 不同、同时仍属几何 vector join 的问题。检查本地论文索引、原文报告、原 D0–D5 淘汰审计和 LES 历史记录；定向核对 Directory-Aware、Fast AVJ v2 与 VBASE 原始来源。检索从 vector similarity join、vector join、similarity self-join、high-dimensional similarity join、inner-product similarity join 的 hierarchy/prefix 变体开始，再查 filtered ANN/metadata 变体。

## 结果

形成 [Problem Formulation](../problem-formulation.md)：以代码仓库文件目录树为背景，定义静态共用目录树、单路径对象、cross/L2/threshold/exact pair 输出及祖先可比路径过滤；明确同路径、内部节点、兄弟节点、阈值等号及去重边界。原 D0 按需范围编译停止独立立项的决定不变。

本轮**没有算法实现或实验**，没有性能、召回或新颖性验证数字。网页检索与当前本地文献不足以排除同构的 filtered/hierarchical vector join；待补查项见总检索日志。
