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
- 检索结论写入 `papers/papers.jsonl`、`papers/relations.jsonl` 与 `papers/changelog.md`，不另建检索日志文件。
- 研究假设与方法边界写入 `researches/nested-attr-vector-search/approaches.md` 与 `problem_def.md`。
- PDF/Markdown/图片沿用已有目录，不为标题更新重命名目录；旧的元数据快照不覆盖成无日期的新快照。

每条论文至少包含书目信息/版本/链接、标签与等级、输入输出、join 类型/度量/假设、方法/组件、理论/复杂度/误差、实验/基线/指标/结果、局限、关系、项目启发与证据。论文未给理论保证写“未提供”；尚未核查写“未核实”，二者有区别。

## 研究进展记录

`researches/<方向>/` 只允许三类内容：

- `approaches.md`：方案观测文档。
- `problem_def.md`：问题定义与评测口径。
- `<YYYY-MM-DD>-exps/`：当日研究记录，含 `report.md`、`data/`、`figures/`、`experiments/`。

不得保留导航/索引页、文献检索日志、历史归档、独立分析报告、代码复查记录或旧论文笔记。这些内容确需留存时写入 `papers/` 或每日记录，以 git 历史为准。

`approaches.md` 的写法（供宏观调控，不做论证）：

- 只写**当前正在执行的方案**：数据与工作负载、索引构建步骤、查询流程、全部参数与开关、输出字段含义、当前实测工作点、可调方向与当前判断、运行命令。
- 不写理论推导与证明、新颖性与前序论证、代码审查或缺陷清单、分析过程与长篇否定论证。
- 负结果只留一行结论加数据位置，不展开论证。
- 参数表要标当前取值与作用；实测表要标口径（查询数、重复次数、召回定义）。

每日记录：

- 每轮研究结束前在 `researches/<方向>/<YYYY-MM-DD>-exps/` 写 `report.md`：当日目标、实验设置、结果数字、结论、局限、下一步。
- 同目录 `data/` 存汇总数据（CSV/JSON/原始日志），`experiments/` 存命令、构建记录与复现说明。
- 只写已运行的实验；未完成、未核实或失败的结论必须标注。每日目录只追加，不覆盖历史记录。

## 代码目录

- 每个方案一个目录：`codes/<方案名>/`，内含 `build.sh` 与 `src/`；测量与绘图脚本放该方案的 `scripts/`。
- 基线同样按方案建目录（如 `codes/NodeANN/`），不放进主方案的 `src/`。
- 共享头文件放 `codes/shared/`；第三方源码放 `codes/third_party/`，固定版本、不修改。
- 构建产物放 `codes/<方案名>/build/`，不入库。

## 比较与研究问题

- 比较前对齐距离与归一化、阈值/输出密度、pair 计数规则、召回定义、硬件/并行度、内存预算、构建/更新/物化成本及重复执行次数。
- query-average recall 与 global pair recall 分别记录，不能直接比较；zero-result queries 的处理需显式说明。
- 每项研究问题区分“作者明确 future work”与“基于文献综合推导的研究假设”，给出问题、现有不足、差异、可检验假设、数据/基线/指标、最小实验、风险/反例。
- 新颖性初筛搜索同义术语、引用链和相邻领域；证据不足标“尚未充分排查”，不能宣称无人研究。
- 回答先给结论，论文清单用表格、核心与背景分别列；关键事实附可点击原始链接，正文结论尽量定位章节/图表。
- 回答末尾简述本轮新增/修订、更新主题与关键待补查项。文件无法保存时输出可追加记录，并明确未持久化。
- 持续维护通过后续研究任务中的文件更新实现；未运行的定时任务、未执行的抓取不能描述成自动完成。

## Git 提交与推送

- 每轮研究结束自动提交源码、文档与小体积实测 CSV/JSON；不提交 `datasets/`、`codes/*/build/`、大索引或构建产物。
- 提交后 `git fetch`，再推送当前分支；同步 `main` 前先确认 `origin/main` 是 `HEAD` 的祖先（`git merge-base --is-ancestor origin/main HEAD`），满足时用 `git branch -f main HEAD` 快进。
- 禁止强推；历史分叉时先 merge/rebase 并报告，由用户决定。
