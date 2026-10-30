# 论文库

论文按其自身研究问题归档，总索引支持跨主题查重、检索和关系追踪。相对项目主线的“可迁移借鉴”不再决定物理存放位置。

| 主主题 | 数量 | 内容与入口 |
|---|---:|---|
| vector-join | 7 | [几何向量连接](vector-join/README.md)：执行、复杂性、基数估计 |
| filtered-ann | 12 | [过滤 ANN](filtered-ann/README.md)：标签、范围、共享结构、召回控制 |
| prefix-vector-search | 1 | [前缀与目录检索](prefix-vector-search/README.md)：Directory-Aware/TrieHI |
| similarity-join | 1 | [一般相似连接](similarity-join/README.md)：多字段 equality/Hamming 等相邻问题 |
| trajectory-search | 8 | [轨迹检索](trajectory-search/README.md)：完整轨迹/子轨迹 top-k、时空距离与混合范围查询 |
| multi-vector-search | 2 | [多向量/向量集合检索](multi-vector-search/README.md)：BioVSS表示与MSG组合空间图 |
| vector-ann | 4 | [向量ANN机制](vector-ann/README.md)：Hubness、CDMG、Flash、多代表分区路由 |
| memory-systems | 1 | [记忆管理系统](memory-systems/README.md)：VikingMem |
| hybrid-retrieval | 1 | [多路相关性检索](hybrid-retrieval/README.md)：Balancing the Blend；不是scalar-filtered ANN |
| data-indexing | 4 | [属性索引与范围过滤](data-indexing/README.md)：Memento、Grafite、FlexFlood、Qd-tree |
| incremental-views | 2 | [增量与因式分解视图](incremental-views/README.md)：F-IVM、DBSP |

总计 **43 篇论文、30 条关系**。统一入口：[papers.jsonl](papers.jsonl)、[relations.jsonl](relations.jsonl)、[变更记录](changelog.md)。每条论文用 `topic` 标明主主题，`local_dir` 必须位于对应目录；一篇一份全文，跨主题关系不受目录限制。

```text
papers/
├── README.md
├── papers.jsonl
├── relations.jsonl
├── changelog.md
├── vector-join/<id>-<slug>/
├── filtered-ann/<id>-<slug>/
├── prefix-vector-search/<id>-<slug>/
├── similarity-join/<id>-<slug>/
├── multi-vector-search/<id>-<slug>/
├── vector-ann/<id>-<slug>/
├── memory-systems/<id>-<slug>/
├── hybrid-retrieval/<id>-<slug>/
├── data-indexing/<id>-<slug>/
├── incremental-views/<id>-<slug>/
└── trajectory-search/<id>-<slug>/
```

`paper.pdf` 是原始全文，`paper.md` 是文本提取，`paper.report.md` 是阅读报告。保留原有版本、日期化元数据、图片与 MinerU 产物，不因主题迁移改写论文身份。原有 8 篇有 MinerU 产物、13 篇为 PyMuPDF 文本；本轮 8 篇 trajectory-search 论文保存原始 PDF、日期化元数据与问题定义证据，尚未统一转换全文 Markdown。

2026-10-29新增BioVSS按PDF页提取的完整Markdown与审读记录；原始PDF本地保存不纳入本轮小文件提交，来源URL与SHA见日期化元数据。新主题按其vector-set主问题归档，不错误归入vector join或filtered ANN。

同日新增MSG、CDMG、Hubness、Flash、VikingMem、Blend六篇工作。保存工具取得的分页全文与文本SHA；CDMG仅2025历史作者报告，正式2026正文未取得；Flash分析arXiv v1。VikingMem/Blend按2026正式PVLDB稿归档。版本和获取限制见各目录日期化metadata。

2026-10-30新增6篇跨方向工作，归档reader提取原文与文本SHA，未运行作者实现。FlexFlood为NeurIPS2024 ML for Systems workshop，不是主会；Memento正式出版2024/SIGMOD2025；F-IVM在线2023/卷期2024且有2018前身；DBSP使用正式五作者版。详见[迁移研究](../researches/nested-attr-vector-search/2026-10-30-exps/cross-domain-index-transfer.md)。服务器7756064分支未自动合并，计数不相加。

同日具体设计续轮新增PVLDB2025《Unleashing Graph Partitioning for Large-Scale Nearest Neighbor Search》，核其多代表HNSW路由骨架；重申Curator已有lazy/local谓词求值，不将该思想单独当新意。交付[WQR冻结设计](../researches/nested-attr-vector-search/2026-10-30-exps/wqr-index-spec.md)及真实hnswlib有限机制探针，无WIKI/SIFT端到端性能主张。


UniFilter 和 UNG 的原文全文尚缺，仍列在[研究候选与证据](../researches/containment-filtered-vector-search/evidence/)，不计入上述43篇工作。获取受限与理论未提供是两种不同状态。

2026-10-19 的目录迁移对应关系见[迁移清单](reorganization-2026-10-19.json)。A/B/C 保留其原意义：A 为直接几何 vector join，B 为可迁移 ANN/向量数据库研究，C 为有明确迁移价值的邻近问题；不将 B/C 论文放入 vector-join 目录。

## 索引字段与核实状态

`papers.jsonl` 一行一个 JSON 对象，`schema_version=1`。相同工作按正式 DOI（优先）或 arXiv ID 合并，`aliases` 保留预印本 ID；稳定目录名无需随标题/发表年份变化。

| 字段 | 内容 |
|---|---|
| `id`, `aliases` | 唯一键与去重别名 |
| `title`, `authors`, `year`, `preprint_year`, `venue` | 正式书目与预印本年份，完整作者顺序 |
| `version_status`, `versions` | 发表状态、实际分析版本、正式稿与早期稿链接及差异待办 |
| `links` | 论文/全文/预印本/代码/数据链接，未核实链接的状态 |
| `topic`, `tags`, `relevance` | 主研究主题、细分标签、相对 vector join 的 A/B/C 相关性；三者分开 |
| `problem`, `join_type`, `metrics`, `data_assumptions` | 输入输出、pair 计数、self/cross、threshold/top-k、精确/近似、属性过滤、度量/假设 |
| `method`, `guarantees` | 方法/组件、理论/复杂度/误差；“未提供”区别于“未核实” |
| `experiments`, `limitations` | 数据、基线、指标、原文结果及适用边界 |
| `related_work`, `project_insights` | 前序/比较/后续与项目可复用点 |
| `evidence`, `verification`, `local_dir` | 证据版本/章节/链接、本轮日期与未核事项、本地材料 |

`relations.jsonl` 使用 `source → target`：例如后续方法 → 前序方法，比较论文 → baseline。`basis=paper_explicit` 是原文明确关系；`researcher_synthesis` 是研究者类比或组合假设，不当作作者引用。

书目核实不表示所有算法保证与实验都已精读。当前记录来自本轮相关章节核对；正式稿与本地预印本的逐项差异、未核数据与官方代码链接留在 `verification.open_items`/`links` 中。旧 `metadata.json` 保留历史抓取快照，当前书目信息以注明日期的索引为准。

## 检索与维护

历史约定使用 `tools/research_index.py`，但2026-10-29当前checkout未包含此脚本，不应照抄不存在的命令。本轮以独立JSON/ID/主题目录/本地证据路径核验替代；没有新建同名工具或冒称旧验证器运行成功。

更新顺序：查重 → 核实书目/版本 → 核对正文证据 → 更新论文与关系 → 修订综述/研究假设 → 写 changelog 与 search-log → 验证标识符和路径。结构核验不自动判断论文事实正确性。

