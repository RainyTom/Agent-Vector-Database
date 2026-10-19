# VADER: Filtered Vector Search with Declarative Recall

核查日期：2026-10-18。等级 B，可迁移借鉴；非 vector join。分析版本：2609.38053v2。

## 问题与方法

查询向量、过滤谓词、k、目标 recall 和底层向量索引。

离线生成跨 selectivity/correlation 搜索轨迹；GBDT recall predictor；可选非对称损失减轻召回高估；自适应调用预测器并早停。

## 保证与限制

所读 §3 为预测器与早停设计；未提供无条件逐查询 recall 证书。

预测开销与调用策略计入，通用渐近查询界未核实。

预测召回不等于保证召回；分布偏移和早停违约须实测。

需要训练查询与搜索轨迹；层级特征收益未被证明；目标召回接口不是无条件质量保证。

## 实验核查

数据：SIFT1M、GIST1M、GLOVE1M、DEEP10M、T2I10M、CASELAW7M。基线：Sweeping、ACORN-1、Filtered-ScaNN、DARTH/DARTH+ related-work comparisons。指标：recall、query time、termination quality、prediction error。

§4 扫描选择率与相关性；Table 2 给数据，合成谓词与 CASELAW 真实元数据分开。未独立复现。

## 本项目启发与证据

按 recall 早停已有直接先前工作；层级路由假设需与本方法和 selectivity-only 基线对照。

正文定位：2.2, 3.1–3.4, 4；PDF 页 1–6, 15。见 [PDF](paper.pdf)、[完整文本提取](paper.md)、[alphaXiv 页证据](alphaxiv-evidence-2026-10-18.xml) 和 [创新性比较](../../../researches/nested-attr-vector-search/novelty-analysis.md)。
