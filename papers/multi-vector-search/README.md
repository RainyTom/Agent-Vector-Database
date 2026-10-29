# 多向量与向量集合检索

本主题按论文自身问题分类，不把单查询vector-set top-k称为vector join，也不把Hausdorff与MaxSim/Chamfer混为一谈。相对项目主线属于B类可迁移表示与候选剪枝机制。

| 工作 | 版本与问题 | 证据 |
|---|---|---|
| BioVSS / BioVSS++ | ICDE2025正式版；分析arXiv2412.03301v2扩展稿；主要为对称Hausdorff集合检索 | [全文（按PDF页）](2412.03301-biovss/paper.md)、[机制审读](2412.03301-biovss/paper.report-2026-10-29.md)、[提取元数据](2412.03301-biovss/metadata-2026-10-29.json) |
| MSG | SCIS2026正式19页；固定字段组合的加权对应向量距离，不是Hausdorff/MaxSim | [正式全文](10.1007-s11432-025-4793-9-msg/paper.md)、[来源](10.1007-s11432-025-4793-9-msg/metadata-2026-10-29.json)；审读入口见来源中的review_path |

原始PDF下载到本地，约8.6MB，不纳入本轮小文件提交；可用日期化元数据的原始URL与SHA重取。全文Markdown已提交，不使用AI摘要代替原文。总索引为[papers.jsonl](../papers.jsonl)，尚未为MUVERA另建完整记录。
