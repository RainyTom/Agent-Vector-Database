# 层级属性向量检索：导航

本文件只做导航。

> **2026-11-04 状态：已清空，从零开始。** 全部 `*-exps` 实验目录（含报告、CSV、审计、实验脚本、PPT、源码快照，2.1 GB / 1,693 个跟踪文件）连同 19 份方案探索文档一并删除。历史版本在 git 中，删除前的完整状态是提交 `74a5e06`。
>
> 保留的只有四类：`papers/`（文献库）、`codes/dag-composition-methods/`（四种方法的 C++ 实现：`dag_merge` / `nodeann` / `global_prefilter` / `acorn1`）、`work/`（本地向量、GT、工作量文件，未纳入 git）、以及本目录下的方向背景与文献分析。

## 仍在磁盘上的前置材料

| 材料 | 位置 | 说明 |
|---|---|---|
| 四种方法的实现 | `codes/dag-composition-methods/{dag_merge,nodeann,global_prefilter,acorn1}/hnswlib` | 各占独立 hnswlib 副本，可重新接线 |
| 原始向量与 GT | `work/` | 未纳入 git，删除安全；具体路径需重新确认 |
| 文献库 | `papers/papers.jsonl`（46 条）、`relations.jsonl` | 含 filtered-ANN、目录感知、多向量检索等 |
| 方向背景 | 本目录 `02-`…`08-` 各 README | 历史方向的问题陈述，不含最新方案 |

## 需要重建的

- 实验入口（run 脚本、分析脚本、测试）：原在 `*-exps/experiments/`，已删除。
- 数据准备（属性 DAG 解析、工作量生成、GT 导出）：同上。
- 指标口径（Recall 分母、ties-aware、批内中位、同召回前沿）：需在新一轮里重新写死并记录。

## 边界

- 本文件不含方案、结果数字或口径定义。任何新结论必须附可复现的运行记录。
- 目录内其余 md（`directions.md`、`novelty-analysis.md`、`alphaxiv-novelty-audit.md`、`overlap-refinement.md`、各 README）为历史背景，其中指向已删实验目录的链接已改写为纯文字。
