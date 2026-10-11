# Vector Join Paper Catalog

论文按“核心执行工作”和“理论/相邻工作”分组。每个目录均包含 PDF、MinerU Markdown、图片、书目元数据，以及逐篇批判性分析 `paper.report.md`。跨论文综合见 [方案来源、演化、展望与缺陷](../../researches/vector-join/deep-analysis.md)。

结构化入口：[papers.jsonl](papers.jsonl)（8 条；A=7、B=0、C=1）、[relations.jsonl](relations.jsonl)（6 条）、[changelog.md](changelog.md)。本轮是已有论文的结构化迁移与纠错，不是完整领域清单。ANN/filtered ANN 的 B 类候选暂在[补查队列](../../researches/vector-join/search-log.md)。

## 核心执行工作

| 年份 | 论文 | 关注点 | 本地材料 |
|---|---|---|---|
| 2026（预印本） | [Fast Approximate Vector Joins via Offline-Online Co-Design](https://arxiv.org/abs/2603.16360) (`2603.16360v2`) | A；内存近似 cross-threshold join；merged index、SWS、OOD；v2 §6 包含 NN-Map、维护、统计与 map selection | [目录](2603.16360-fast-approximate-vector-joins/) |
| 2025 | Fast Approximate Similarity Join in Vector Databases / SimJoin (`10.1145/3725403`) | 核心基线；join-window sliding、MST 执行顺序、$k$-similarity join 与图索引维护 | [目录](10.1145-3725403-simjoin/) |
| 2025（PACMMOD；SIGMOD 2026） | [DiskJoin: Large-scale Vector Similarity Join with SSD](https://doi.org/10.1145/3769780) (`2508.18494`) | A；单机 SSD approximate self-join；分桶、I/O 调度、缓存和概率剪枝 | [目录](2508.18494-diskjoin/) |
| 2024（预印本；正式 venue 未核实） | [Xling: A Learned Filter Framework for Accelerating High-Dimensional Approximate Similarity Join](https://arxiv.org/abs/2402.13397) | A；学习型基数过滤器；按查询跳过低产出的 range search | [目录](2402.13397-xling/) |

## 理论与相邻工作

| 年份 | 论文 | 关注点 | 本地材料 |
|---|---|---|---|
| 2019（DaMoN；预印本 2018） | [GPU-Accelerated Similarity Self-Join for Multi-Dimensional Data](https://doi.org/10.1145/3329785.3329920) | A；exact Euclidean self-join；本地 arXiv 长稿与正式 9 页短稿差异待核 | [目录](1809.09930-gpu-similarity-self-join/) |
| 2016（PODS；预印本 2015） | [On the Complexity of Inner Product Similarity Join](https://doi.org/10.1145/2902251.2902285) | A；内积 join 条件下界/近似边界；主要为每查询至少一个 witness，非全部 pair 枚举 | [目录](1510.02824-inner-product-similarity-join-complexity/) |
| 2020（TKDE；online 2019，预印本 2018） | [Similarity Join and Similarity Self-Join Size Estimation in a Streaming Environment](https://doi.org/10.1109/TKDE.2019.2893175) | C；多字段 equality/Hamming 基数估计；sketch 可迁移但需重新证明 | [目录](1806.03313-streaming-join-size-estimation/) |
| 2011（PVLDB 4(6)） | [Similarity Join Size Estimation using Locality Sensitive Hashing](https://www.vldb.org/pvldb/vol4/p338-lee.pdf) | A；直接估计实向量 similarity join cardinality；cosine/TF-IDF 实例，非仅 Jaccard/set join | [目录](1104.3212-lsh-join-size-estimation/) |

## 完整性

当前收录 8 篇论文。所有论文均已通过 MinerU `vlm` 模型转换，原始解析结果保存在各目录的 `mineru/` 中；研究时用 `paper.report.md` 建立结构化认识，以 `paper.md` 核对正文证据，需要核对版式或页码时回看 `paper.pdf`。

## 索引字段与核实状态

`papers.jsonl` 一行一个 JSON 对象，`schema_version=1`。相同工作按正式 DOI（优先）或 arXiv ID 合并，`aliases` 保留预印本 ID；稳定目录名无需随标题/发表年份变化。

| 字段 | 内容 |
|---|---|
| `id`, `aliases` | 唯一键与去重别名 |
| `title`, `authors`, `year`, `preprint_year`, `venue` | 正式书目与预印本年份，完整作者顺序 |
| `version_status`, `versions` | 发表状态、实际分析版本、正式稿与早期稿链接及差异待办 |
| `links` | 论文/全文/预印本/代码/数据链接，未核实链接的状态 |
| `tags`, `relevance` | 主题与 A/B/C；说明直接支持或可迁移关系 |
| `problem`, `join_type`, `metrics`, `data_assumptions` | 输入输出、pair 计数、self/cross、threshold/top-k、精确/近似、属性过滤、度量/假设 |
| `method`, `guarantees` | 方法/组件、理论/复杂度/误差；“未提供”区别于“未核实” |
| `experiments`, `limitations` | 数据、基线、指标、原文结果及适用边界 |
| `related_work`, `project_insights` | 前序/比较/后续与项目可复用点 |
| `evidence`, `verification`, `local_dir` | 证据版本/章节/链接、本轮日期与未核事项、本地材料 |

`relations.jsonl` 使用 `source → target`：例如后续方法 → 前序方法，比较论文 → baseline。`basis=paper_explicit` 是原文明确关系；`researcher_synthesis` 是研究者类比或组合假设，不当作作者引用。

书目核实不表示所有算法保证与实验都已精读。当前记录来自本轮相关章节核对；正式稿与本地预印本的逐项差异、未核数据与官方代码链接留在 `verification.open_items`/`links` 中。旧 `metadata.json` 保留历史抓取快照，当前书目信息以注明日期的索引为准。

## 检索与维护

从项目根目录运行：

```bash
python tools/research_index.py search 'SSD' --level A
python tools/research_index.py search 'cardinality' --json
python tools/research_index.py validate
```

更新顺序：查重 → 核实书目/版本 → 核对正文证据 → 更新论文与关系 → 修订综述/研究假设 → 写 changelog 与 search-log → 验证标识符和路径。验证脚本检查结构和本地引用完整性，不自动判断论文事实正确性。

