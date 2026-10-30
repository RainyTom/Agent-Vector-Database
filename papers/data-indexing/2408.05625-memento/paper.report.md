# Memento Filter: A Fast, Dynamic, and Robust Range Filter

分析版本：arXiv2408.05625v3,2024-10-27。正文定位：§4–6 pp.8–17；§7–8 pp.17–23。

- 高位分区指纹＋真实低位后缀
- RSQF keepsake box变长编码
- fluid fingerprints、扩缩容和filter chain

保证与条件：无假阴性；基本range FPR≤(α/ℓ)2^(1−f)，E次扩容界≤(E+2)(α/ℓ)2^(−f)。§6参数条件不可省略。

成本：期望insert/delete O(ℓ)，query O(log ℓ)；单次resize扫描结构；长区间可能检查更多分区。

作者实证（本轮未复现）：
- 作者WiredTiger等总内存实验：50%空查询时约1.9×查询提升、插入约2.5%开销；不是ANN结果

限制：
- 边际非空不能认证同对象合取
- 高删除率、目录MOVE及块拆并的本项目成本未测
- 默认/expandable分支能力需固定commit；GPLv3；并发API注明未充分测试
- v1与v3定理不同，不混用旧cluster界

迁移假设：[INFERENCE] 几何叶块的动态必要条件否定目录；稳定目录ID与拓扑间接层。不能凭邻接块无合格点切断ANN导航。

综合审读：`researches/nested-attr-vector-search/2026-10-30-exps/dynamic-range-transfer-review.md`。来源及文本SHA见同目录日期化metadata。
