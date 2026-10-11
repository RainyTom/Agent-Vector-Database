# 索引变更记录

## 2026-10-11（Asia/Shanghai）

- 首次将已有 8 篇论文迁移到 `papers.jsonl`：A=7（执行/理论/基数估计），C=1（多字段 streaming estimator）；本轮没有新增论文全文。
- 建立 6 条关系：4 条原文明确关系、2 条研究者综合关系，标注证据与方向。
- 修正 LSH-SS 的问题分类：原文 Definition 1 直接定义实向量 VSJ size，cosine/TF-IDF 为主要实例；不是仅集合/Jaccard estimator。保留现代 dense embedding 迁移待验证的边界。
- 补入 DiskJoin 正式 DOI、PACMMOD 2025 卷期与 SIGMOD 2026 的区别；记录本地 15 页稿与注册 27 页正式稿待比较。
- 补入 IPS 的 PODS 2016、SJPC 的 TKDE 2020、GPU 的 DaMoN 2019，保留各自预印本年份。GPU 同题长短稿尚未逐项比较，JPDC 低维工作不并入。
- 补记 Fast AVJ v2 §6 NN-Map、维护、map selection/composition、join statistics；撤回“基数/优化器/动态维护完全缺失”式无范围判断。
- 记录不同 recall、输出 pair 语义与 build/online 计时，防止跨论文加速比直接横比。
- 增加项目 `AGENTS.md`、本地检索/结构验证工具、检索覆盖日志与一个明确标注待验证的研究假设。
- 待办：正式稿差异、官方代码/数据链接、LSH/分布式/早期精确连接、B 类 ANN/filtered ANN 与研究新颖性引用链扩展。
