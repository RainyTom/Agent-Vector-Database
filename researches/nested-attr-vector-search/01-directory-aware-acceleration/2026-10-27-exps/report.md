# 完整 WIKI 实验准备与计量修复

## 目标

用新到齐的WIKI向量恢复全量实验，明确两类基线：全局HNSW/IVF+候选ID过滤；节点树+每节点局部HNSW、逐节点top-k后合并。Flat仅作对照，默认threshold=0保持不变。

## 已运行设置与结果

- 原始fvecs大小7,960,883,900 bytes，1,941,679×1024；真实树468,031节点，其中363,467非空。
- 转换修复前，输入float `(1.0,-2.5,0.125)`被错误数值转换为`(1065353216.0,-1071644672.0,1040187392.0)`；修复后两个记录（含signed zero）浮点payload逐字节相同。
- 小型真实fixture生成smoke输出456条有效查询；旧real workload的457行中qidx=456越界，本轮固定保留0..455全部原始查询。
- CLI smoke实际跑过12×4d、7节点、4查询，局部HNSW/Flat均命中GT数量`10/2/7/0`；NODE bitmap_us均0；越界qidx被拒绝。
- 已构建并启动完整WIKI三配置串行实验，原始数据保留本目录；实验跨日完成，**最终数字、结论、局限和下一步统一见[完成日报告](../2026-10-28-exps/report.md)**，不在这里重复主表。

## 结论、局限与后续

修正前的ANN质量/计时与修正后不可直接比较；已确认旧`data/real/test.f32`受错误转换影响，未核查输入不推断污染范围。目录解析/DSM不依赖向量，不因转换错误自动失效。固定约束为真实树上的合成锚点，不是原始发布过滤条件；本轮没有新增MOVE/MERGE实测。

复现命令、日志、输入hash、测试证据见[experiments/reproduction.md](experiments/reproduction.md)；汇总见[data/wiki_full_summary.csv](data/wiki_full_summary.csv)。下一步及尚未测量项以完成日报告为准。
