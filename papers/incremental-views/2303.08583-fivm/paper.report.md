# F-IVM: analytics over relational databases under updates

分析版本：正式期刊HTML/PDF；2024卷期，online2023。正文定位：§4–7；§8.3；§9；§11。

- variable order/dep(X)共享依赖键
- view tree与ring payload
- 低秩delta先消元再组合
- eager/lazy factorized结果表示

保证与条件：固定q-hierarchical query且ring操作O(1)等条件下预处理O(N)、单tuple更新O(1)、枚举delay O(1)；外部目录层级不等于q-hierarchical。

成本：free-connex acyclic固定查询单tuple更新可O(N)；大bitmap payload必须乘真实操作代价；枚举M项仍Ω(M)。

作者实证（本轮未复现）：
- §9.4低枚举频率lazy占优，高频枚举eager-factorized占优；不编造未提取图的逐项吞吐
- 插入批次实验不能当高频随机删除/MOVE实测

限制：
- 2018前身，不把期刊扩展版当首次发明
- top-k截断不可删除逆；不能继承O(1)ring定理
- 共同ID posting交不能套一般循环join的WCOJ收益
- 所有scope跨所有cell或独有facet组合时共享退化

迁移假设：[INFERENCE] scope/facet/cell的依赖键共享资格视图，按访问cell/bitmap容器懒消费，避免每scope重复建相同局部状态。

综合审读：`researches/nested-attr-vector-search/2026-10-30-exps/factorized-views-transfer-review.md`。来源及文本SHA见同目录日期化metadata。
