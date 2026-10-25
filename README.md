# Vector Database Research Workspace

研究型仓库：论文索引 + 研究笔记 + 可复现实验。**当前主线是语义时空 episode 检索**；早期 vector join、前缀/包含过滤、层级属性检索三条线已暂停，保留历史文档。

## 入口

| 用途 | 文件 |
|---|---|
| **项目移交与服务器迁移** | [PROJECT_HANDOFF.md](PROJECT_HANDOFF.md) |
| **Planner/Executor 协议** | [coordination/README.md](coordination/README.md) |
| 项目状态（机器可读） | [coordination/state.json](coordination/state.json) |
| 研究助理长期规则 | [AGENTS.md](AGENTS.md) |
| 方向级交接 | [researches/semantic-spatiotemporal-hybrid-search/HANDOFF.md](researches/semantic-spatiotemporal-hybrid-search/HANDOFF.md) |
| 问题定义（唯一规范） | [problem-definition.md](researches/semantic-spatiotemporal-hybrid-search/problem-definition.md) |
| 方法完整规范 | [approach.md](researches/semantic-spatiotemporal-hybrid-search/approach.md) |
| 基线方案 | [baseline.md](researches/semantic-spatiotemporal-hybrid-search/baseline.md) |
| 查重与边界 | [novelty-audit.md](researches/semantic-spatiotemporal-hybrid-search/novelty-audit.md) |
| 候选新问题 | [open-problems.md](researches/semantic-spatiotemporal-hybrid-search/open-problems.md) |

## 当前结论（6 城、1.84M 签到、500 查询、1 年窗、50 km）

| 方法 | Recall@10 | mean ms | p95 ms |
|---|---:|---:|---:|
| 精确空间先行 | 1.000 | 15.3 | 37.5 |
| 平坦桶 catalog | 0.989 | 10.5 | 23.6 |
| **EST（Episode Span Tree）** | 0.989 | **5.66** | 15.0 |
| **EST + 有界 slack G=2** | 0.989 | **4.68** | 11.4 |

EST 的硬约束结果与全量扫描 500/500 逐条一致。详见 `researches/semantic-spatiotemporal-hybrid-search/2026-10-25-exps/`。

## 目录

```text
.
├── AGENTS.md                  # 研究规则
├── PROJECT_HANDOFF.md         # 项目移交手册
├── coordination/              # planner/executor 任务、结果、状态
├── papers/                    # 论文库：29 篇、27 条关系
│   ├── papers.jsonl / relations.jsonl / changelog.md
│   └── vector-join/ filtered-ann/ prefix-vector-search/
│       similarity-join/ trajectory-search/
├── researches/
│   ├── semantic-spatiotemporal-hybrid-search/   # 当前主线
│   ├── vector-join/                             # 暂停
│   ├── containment-filtered-vector-search/      # 暂停
│   └── nested-attr-vector-search/               # 暂停
├── production/semantic-mobility-search/         # 实验代码与 workload
│   ├── src/（prep_potion、bench、bench_est、bench_pq）
│   └── work/（大文件，gitignored）
├── datasets/                  # 原始 CSV（gitignored）
├── deps/                      # 第三方源码（gitignored）
├── codes/                     # 早期复现代码
└── paper-production/          # 论文写作工程（未用于本主线）
```

## 约定

- 论文按**自身研究问题**分类，`papers.jsonl` 为总索引；新增前先查重，更新 `changelog.md`。
- 研究笔记与实验记录在 `researches/<方向>/`；每日实验在 `<YYYY-MM-DD>-exps/`，含 `data/` 与 `experiments/`。
- 大数据、workload、模型、venv、第三方源码不入库。
- 结果必须可复现：命令、环境、产物路径、指标一起记录。
