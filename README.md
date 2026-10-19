# Vector Database Research Workspace

本项目用于探索向量数据库研究问题。当前主线是 **vector join**，重点关注高维向量集合之间的阈值连接、近似执行、外存执行、过滤与基数估计。

## 目录约定

```text
.
├── papers/                  # 按论文自身研究主题分类
│   ├── papers.jsonl         # 全主题总索引
│   ├── relations.jsonl      # 跨主题论文关系
│   ├── vector-join/         # 几何向量连接：7 篇
│   ├── filtered-ann/        # 过滤 ANN：12 篇
│   ├── prefix-vector-search/ # 前缀/目录检索：1 篇
│   └── similarity-join/     # 一般相似连接：1 篇
├── researches/             # 探索性笔记、调研报告、实验草稿
│   ├── vector-join/
│   ├── containment-filtered-vector-search/
│   └── nested-attr-vector-search/
├── codes/                  # 论文实现和实验代码
├── datasets/               # 数据集与数据审计信息
└── paper-production/       # 论文写作工程
```

论文按自身研究问题选择主题目录，“可迁移到 vector join”只作为相关性标签。新增论文时应先在总索引查重，再按 `<arxiv-id>-<short-slug>` 建立独立目录。论文材料沿用 `paper.pdf`、`paper.md`、`paper.report.md` 与已有 `images/`，以便笔记和后续论文写作稳定引用。

## 当前入口

- [论文库总入口](papers/README.md)
- [Vector Join](papers/vector-join/README.md)、[过滤 ANN](papers/filtered-ann/README.md)、[前缀/目录检索](papers/prefix-vector-search/README.md)、[一般相似连接](papers/similarity-join/README.md)
- [可检索论文索引](papers/papers.jsonl)、[论文关系](papers/relations.jsonl)、[更新记录](papers/changelog.md)
- [检索覆盖与补查队列](researches/vector-join/search-log.md)、[待验证研究问题](researches/vector-join/research-questions.md)
- [前缀包含过滤向量搜索：定义、归约与八项相关工作](researches/containment-filtered-vector-search/index.md)
- [Vector Join 研究综述](researches/vector-join/survey.md)
- [Vector Join 学习与实验路线](researches/vector-join/roadmap.md)

# Agent-Vector-Database

研究助理的长期工作规则保存在 [AGENTS.md](AGENTS.md)。索引维护在每次研究任务中执行；当前没有定时抓取任务。

```bash
python tools/research_index.py search 'NN-Map' --topic vector-join
python tools/research_index.py search 'containment' --topic filtered-ann --json
python tools/research_index.py validate
```
