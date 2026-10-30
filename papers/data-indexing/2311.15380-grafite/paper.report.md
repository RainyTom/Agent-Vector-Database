# Grafite: Taming Adversarial Queries with Optimal Range Filters

分析版本：arXiv2311.15380v2,2024-03-19。正文定位：§2–3 pp.3–6；§6–7 pp.8–13（所读预印本页码）。

- 局部性保留宇宙缩减h(x)=(q(floor(x/r))+x) mod r
- 哈希值排序去重后Elias–Fano编码
- 跨分区和模环绕区间分解＋predecessor

保证与条件：λ≤L时FPR≤λε/L；空间n log2(L/ε)+2n+o(n)，含L≤uε/n等条件。固定B bits/key界min(1,λ/2^(B−2))，不是自适应密码学攻击保证。

成本：query O(log(L/ε))；B=O(1)时O(B)；静态构建排序。

作者实证（本轮未复现）：
- 原文核对FPR、查询/构造时间、8–28bits/key实验；不转述未经逐图核验的峰值倍数

限制：
- 静态；dynamic Elias–Fano为future work
- 长范围FPR界退化；没有对象级合取见证
- 作者实现GPLv3；未运行

迁移假设：[INFERENCE] 冻结几何块的空间/FPR对照，不冒充已解决频繁更新。

综合审读：`researches/nested-attr-vector-search/2026-10-30-exps/dynamic-range-transfer-review.md`。来源及文本SHA见同目录日期化metadata。
