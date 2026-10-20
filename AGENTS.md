# 研究助理工作约定

本项目主线是高维 vector join。研究任务除回答问题，还应维护可检索、可追溯的本地知识索引。默认中文回答，保留重要英文检索术语。

## 范围与分类

- A：直接研究几何向量连接，包括执行、理论与基数估计；分别注明 self/cross、threshold/top-k、exact/approximate、属性过滤及输出语义。
- B：向量数据库/ANN 工作，标记“直接支持 vector join”或“可迁移借鉴”，写明迁移点；普通 ANN 查询不能称为 join。
- C：semantic/LLM join、集合/字符串连接等邻近问题，仅在有明确迁移价值时收录，并保留原问题分类。
- 不将语义谓词判定与几何距离阈值混为一谈。不将为每个查询输出 k 个近邻与全局 top-k pairs 混为一谈。

## 文献工作流程

1. 先检查 `papers/papers.jsonl`、`relations.jsonl`、已有全文和研究笔记，再查重与更新。
2. 每轮先搜 vector similarity join、vector join、similarity self-join、high-dimensional similarity join、inner-product similarity join 及变体，再搜 ANN、filtered ANN、混合查询等。
3. 优先原始论文、正式 proceedings、作者稿和全文；综述用于术语与引用链。覆盖数据库、IR 相关会议期刊及有直接迁移价值的其他领域。
4. 核实标题、完整作者顺序、年份、venue、链接、版本状态。正式发表年份与预印本/会议年份分开。未知写“未核实”，不能猜测。
5. 核心论文阅读相关正文，核实定义、假设、保证、算法、实验与局限；记录证据版本、章节/页码/图表及本地定位。
6. 一项工作一条记录，优先 DOI，其次 arXiv ID/正式页面。保留 aliases 和各版本链接；扩展版、短文和同名稿不凭标题强行合并。
7. 将未精读的候选列入检索日志待办，不填造算法或实验。记录检索范围和缺口，不声称覆盖全部文献。

## 持久索引

论文按自身主研究问题存放在 `papers/vector-join/`、`papers/filtered-ann/`、`papers/prefix-vector-search/` 或 `papers/similarity-join/`；总索引使用 `topic` 与对应 `local_dir`。A/B/C 是相对 vector join 主线的相关性，不决定主题目录。每篇全文只存一份，跨主题用统一关系索引关联。

- `papers/papers.jsonl`：结构化论文记录，字段约定见同目录 README。
- `papers/relations.jsonl`：有方向、有证据的引用、比较、方法继承等边；作者关系与研究者推断分开。
- `papers/changelog.md`：新增、修订、去重和分类变化。
- `researches/vector-join/search-log.md`：检索词、来源、覆盖限制、未完成候选与补查项。
- `researches/vector-join/research-questions.md`：研究假设、新颖性排查与最小实验。
- PDF/Markdown/图片沿用已有目录，不为标题更新重命名目录；旧的元数据快照不覆盖成无日期的新快照。

每条论文至少包含书目信息/版本/链接、标签与等级、输入输出、join 类型/度量/假设、方法/组件、理论/复杂度/误差、实验/基线/指标/结果、局限、关系、项目启发与证据。论文未给理论保证写“未提供”；尚未核查写“未核实”，二者有区别。

## 研究进展记录

- 每个方向的相关文档（协议、基线调研、方案、实验记录）统一放在 `researches/<方向>/` 内，不新增顶层独立文件；`researches/nested-attr-vector-search/index.md` 只做导航。
- 每轮研究结束前，在 `researches/<方向>/<YYYY-MM-DD>-exps/` 写 `report.md`：当日目标、实验设置、结果数字、结论、局限、下一步。
- 同目录 `data/` 存汇总数据（CSV/JSON/原始日志），`experiments/` 存命令、构建记录与复现说明。
- 只写已运行的实验；未完成、未核实或失败的结论必须标注。每日目录只追加，不覆盖历史记录。

## 比较与研究问题

- 比较前对齐距离与归一化、阈值/输出密度、pair 计数规则、召回定义、硬件/并行度、内存预算、构建/更新/物化成本及重复执行次数。
- query-average recall 与 global pair recall 分别记录，不能直接比较；zero-result queries 的处理需显式说明。
- 每项研究问题区分“作者明确 future work”与“基于文献综合推导的研究假设”，给出问题、现有不足、差异、可检验假设、数据/基线/指标、最小实验、风险/反例。
- 新颖性初筛搜索同义术语、引用链和相邻领域；证据不足标“尚未充分排查”，不能宣称无人研究。
- 回答先给结论，论文清单用表格、核心与背景分别列；关键事实附可点击原始链接，正文结论尽量定位章节/图表。
- 回答末尾简述本轮新增/修订、更新主题与关键待补查项。文件无法保存时输出可追加记录，并明确未持久化。
- 持续维护通过后续研究任务中的文件更新实现；未运行的定时任务、未执行的抓取不能描述成自动完成。
