# 2026-10-25 研究记录：新问题背景与定义

## 当日目标与设置

基于 Directory-Aware/TrieHI 的目录作用域思想，寻找 nested attribute 背景下与其单查询 top-k 不同、同时仍属几何 vector join 的问题。检查本地论文索引、原文报告、原 D0–D5 淘汰审计和 LES 历史记录；定向核对 Directory-Aware、Fast AVJ v2 与 VBASE 原始来源。检索从 vector similarity join、vector join、similarity self-join、high-dimensional similarity join、inner-product similarity join 的 hierarchy/prefix 变体开始，再查 filtered ANN/metadata 变体。

## 结果

形成 [Problem Formulation](../problem-formulation.md)：以代码仓库文件目录树为背景，定义静态共用目录树、单路径对象、cross/L2/threshold/exact pair 输出及祖先可比路径过滤；明确同路径、内部节点、兄弟节点、阈值等号及去重边界。原 D0 按需范围编译停止独立立项的决定不变。

本轮**没有算法实现或实验**，没有性能、召回或新颖性验证数字。网页检索与当前本地文献不足以排除同构的 filtered/hierarchical vector join；待补查项见总检索日志。

## 同日问题修订

将原单父文件目录树设定改为共享虚拟目录的静态 DAG。一个逻辑目录可由多个上级入口访问，对象按逻辑 ID 去重；[修订后的 Problem Formulation](../problem-formulation.md)给出可达性谓词、精确跨集合阈值 pair 输出，以及四种精确基线及其共同的合格对象集合公式。原树模型为历史草案，不再作为当前问题定义。本次修订仍未执行实验。

## 同日再次修订：多维商品目录

文件夹及其快捷方式仍沿用原论文的文件系统场景，不能满足不同应用背景的要求。现将 [Problem Formulation](../problem-formulation.md) 改为跨平台商品匹配：目录状态是品类、品牌、尺码等已知属性构成的部分函数，筛选顺序可交换；两个互不可包含的目录状态仍可能属性兼容。结果为目录范围内兼容且 L2 阈值内的全部跨平台商品 pair。文档列出五种精确基线及冲突 posting 的候选公式。前两版文件目录定义均为历史草案，本次仍未执行实验。

## 同日层级语义修正

前版把所有维度放进平面 facet 偏序，只形成筛选条件的细化关系，未定义真正的商品品类层级。现按 [OpenSearch 官方说明](https://docs.opensearch.org/latest/tutorials/faceted-search/) 区分 hierarchical faceting 与普通品牌/尺码 facet：在 [Problem Formulation](../problem-formulation.md) 中显式给出品类树 `T`、属性部分函数 `σ` 和复合状态 `(c,σ)`。合法 pair 同时要求直接品类祖先可比、已知属性不冲突及 L2 阈值成立；B0–B4 全部同步到该谓词。本次为定义修订，无实验结果。
