# 属性索引、范围过滤与数据布局

按论文自身问题归档；相对vector join为C类可迁移邻近工作，不直接提供几何join或本项目filtered ANN。

| 工作 | 原问题与版本 | 本地审读 |
|---|---|---|
| [Memento](https://arxiv.org/html/2408.05625v3) | 动态固定长度整数键集合与闭区间；arXiv2408.05625v3,2024-10-27 | [正文](2408.05625-memento/paper.md)、[报告](2408.05625-memento/paper.report.md) |
| [Grafite](https://arxiv.org/html/2311.15380v2) | 静态整数键集合及区间；arXiv2311.15380v2,2024-03-19 | [正文](2311.15380-grafite/paper.md)、[报告](2311.15380-grafite/paper.report.md) |
| [FlexFlood](https://arxiv.org/html/2411.09205v2) | 少量维度的数值点与正交范围查询、insert/erase；arXiv2411.09205v2,2024-11-15（原入口无版本号，正文明确v2） | [正文](2411.09205-flexflood/paper.md)、[报告](2411.09205-flexflood/paper.report.md) |
| [Qd-tree](https://zongheng.me/pubs/qdtree-sigmod20.pdf) | 记录、查询工作负载、候选谓词cuts、最小块大小；SIGMOD2020作者PDF16页 | [正文](2020-qd-tree/paper.md)、[报告](2020-qd-tree/paper.report.md) |

统一书目见[总索引](../papers.jsonl)；研究迁移见[综合报告](../../researches/nested-attr-vector-search/2026-10-30-exps/cross-domain-index-transfer.md)。原文提取SHA不冒充PDF校验；未运行作者实现。
