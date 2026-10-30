# 上游复核复现

本轮没有构建新索引。输入是固定提交7756064的queries.csv中保存的64行root/scope_only/r64/filtered_topk控制；提取窗口和文本SHA见[来源记录](../../08-dynamic-hybrid-vector-search/2026-10-30-exps/data/upstream-review-7756064/oracle-attribution.json)。不重新运行服务器run1/run2。

已执行：

```sh
python3 researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/experiments/upstream_joint_information_analysis.py
```

该脚本仅用Python标准库：按dataset/K/qidx去除basis重复、计算无过滤oracle权重，以及计算三个整数几何/归因反例。输出[joint-information-analysis.json](../../08-dynamic-hybrid-vector-search/2026-10-30-exps/data/upstream-review-7756064/joint-information-analysis.json)。没有在线时延、召回、更新吞吐数据。

结果：WIKI168010/189904=88.4710%，SIFT176710/192000=92.0365%；每数据8个旧query、两种K。无过滤与过滤pooled阈值不同，不能直接相减。E0/E1真实联合信息诊断需要服务器未上传的大数组，尚未执行。

分支：当前上游基于6b8f9cc，服务器7756064基于01ef7d6，已用merge-base核实；没有自动合并或覆盖服务器记录。当天上游报告单独放于nested-attr-vector-search日期目录，避免与08方向服务器report.md同名覆盖。
