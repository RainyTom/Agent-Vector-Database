# 前缀与目录约束向量检索

原生目录/属性前缀范围检索及相关维护；与当前前缀过滤研究直接相关。

当前收录 1 篇已保存全文的论文。每篇只在一个主主题存放；跨主题可检索，不复制材料。

当前[前缀过滤问题与逐篇讨论](../../researches/containment-filtered-vector-search/index.md)以目录前缀范围为研究场景。Directory-Aware/TrieHI 是直接前序；[filtered ANN](../filtered-ann/README.md)中的论文通过编码或导航组件参与比较，不复制全文到本目录。

当前研究规范：[问题定义](../../researches/containment-filtered-vector-search/problem-definition.md)、[创新性复核](../../researches/containment-filtered-vector-search/novelty-analysis.md)。

| 论文 | 年份与版本状态 | 本地材料 |
|---|---|---|
| [Directory-Aware Query and Maintenance in Vector Databases](https://arxiv.org/abs/2606.16903) | 2026；arXiv preprint; 正式venue未核实 | [目录](2606.16903-directory-aware/)、[阅读报告](2606.16903-directory-aware/paper.report.md) |

从项目根目录检索本主题：

```bash
python tools/research_index.py search '' --topic prefix-vector-search
```

索引字段、证据等级和维护方式见[论文库总入口](../README.md)。A/B/C 是相对 vector join 主线的相关性等级，不是物理目录分类。
