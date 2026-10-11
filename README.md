# Vector Database Research Workspace

本项目用于探索向量数据库研究问题。当前主线是 **vector join**，重点关注高维向量集合之间的阈值连接、近似执行、外存执行、过滤与基数估计。

## 目录约定

```text
.
├── papers/                  # 统一论文库
│   └── vector-join/
│       └── <arxiv-id>-<slug>/
│           ├── paper.pdf   # 原始论文
│           ├── paper.md    # MinerU 转换正文
│           ├── images/     # MinerU 提取图片
│           ├── metadata.json
│           └── mineru/     # MinerU 原始解析产物与日志
├── researches/             # 探索性笔记、调研报告、实验草稿
│   └── vector-join/
├── codes/                  # 论文实现和实验代码
├── datasets/               # 数据集与数据审计信息
└── paper-production/       # 论文写作工程
```

新增论文时应先查重，再按 `<arxiv-id>-<short-slug>` 建立独立目录。论文正文统一使用 `paper.pdf`、`paper.md` 和 `images/`，以便笔记和后续论文写作稳定引用。

## 当前入口

- [Vector Join 论文目录](papers/vector-join/README.md)
- [可检索论文索引](papers/vector-join/papers.jsonl)、[论文关系](papers/vector-join/relations.jsonl)、[更新记录](papers/vector-join/changelog.md)
- [检索覆盖与补查队列](researches/vector-join/search-log.md)、[待验证研究问题](researches/vector-join/research-questions.md)
- [Vector Join 研究综述](researches/vector-join/survey.md)
- [Vector Join 学习与实验路线](researches/vector-join/roadmap.md)

# Agent-Vector-Database

研究助理的长期工作规则保存在 [AGENTS.md](AGENTS.md)。索引维护在每次研究任务中执行；当前没有定时抓取任务。

```bash
python tools/research_index.py search 'NN-Map' --level A
python tools/research_index.py search 'cardinality' --json
python tools/research_index.py validate
```
