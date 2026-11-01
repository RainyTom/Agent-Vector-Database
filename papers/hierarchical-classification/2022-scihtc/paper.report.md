# Hierarchical Multi-Label Classification of Scientific Documents

- 作者：Mobashir Sadat；Cornelia Caragea（原文顺序）。
- 正式发表：EMNLP 2022，pp.8923–8937，Association for Computational Linguistics。
- DOI：[10.18653/v1/2022.emnlp-main.610](https://doi.org/10.18653/v1/2022.emnlp-main.610)。
- 预印本：[arXiv:2211.02810v1](https://arxiv.org/abs/2211.02810v1)，2022-11-05。
- 核读版本：[ACL 正式 PDF](https://aclanthology.org/2022.emnlp-main.610.pdf)，§1–5、§7–8；2026-11-01。未复现分类模型。
- 相关性：C，层级分类/数据背景，可迁移到层级属性负载构造；不是 ANN 或几何 vector join。

## 问题与方法

输入为论文标题、摘要及可选作者关键词，输出层级主题的多标签预测。§4 比较 flat 多标签分类与逐主题二分类；后者用父分类器参数初始化子分类器。§4.3 多任务模型共享编码器，联合优化主题分类与关键词标注损失。编码器包括 BiLSTM、BERT、SciBERT。论文没有向量近邻距离阈值、self/cross join 或 top-k pairs 的定义，也未提供 ANN Recall 或动态索引维护保证。

## 数据定义：§2–3，pp.8924–8926

原文以 ACM CCS tree 描述层级：186,160 篇论文，完整标签集 1,233 topics，深度最多六层。train/dev/test 为 148,928 / 18,616 / 18,616。

§3 原句：

> Among the author-specified sub-branches, we only consider the sub-branch with the highest relevance score for each paper.

> However, considering all relevant sub-branches can present a more interesting and challenging task which can be explored in future work.

因此其 multi-label 主要来自被选路径上的多个层级标签，不代表公开文件保留了每篇论文的全部独立直接标签。§3 的主要分类实验只取前两级且训练支持数至少 100 的 83 topics；完整标签层级也公开。原文 PDF 与 arXiv 摘要为 1,233 topics；ACL 网页摘要写 1,234，使用时需注明版本与口径。

[作者 README](https://github.com/msadat3/SciHTC#dataset-access)公开 ACM ID/标签下载入口；全文标题、摘要与关键词需联系作者申请，或使用获授权的 ACM proceedings 和作者脚本重建。README 的 total 184,160 与所列三个 split 之和不一致，本项目采用原文与本地行数的 186,160，不据 README total 改写数据。

## 实验与限制

§5、Table 4：HR-SciBERT 在训练和测试均使用作者关键词时 Macro-F1 为 34.57±0.05%；不能与 ANN Recall 混用。Table 5：没有测试关键词的多任务 HR-SciBERT Macro-F1 为 33.78±0.18%。基线包括 flat/hierarchical BiLSTM、BERT、SciBERT。§8 仅覆盖计算机领域，主题细、分布不均；一般化到其他学科或罕见类别仍有限。

理论/复杂度：相关章节未提供用于向量索引或更新的理论保证；分类模型的统一最坏复杂度未核实。已明确的 future work 包括保留全部相关分支，以及 rare topics 的 few/zero-shot 方法（§3、§7）。

## 项目使用

项目用公开标签路径终点作为单直接标签，在 CCS HTML 的稳定概念 ID 上合并同一概念并计算全部可达祖先，得到 DAG 继承负载。多父结构来自本体解析，不把该重构的继承语义归为论文原实验协议。本地 168,937 条可用标签、103 个多父概念及过滤规则见[结构数据](../../../researches/nested-attr-vector-search/2026-11-01-exps/data/scihtc-dag-structure.json)。

项目没有取得自然文本向量，SIFT 与标签的任意绑定只提供受限的几何检索成本实验。当前没有与其他已收录论文核实的作者引用/方法继承边；不建立推测边。
