# 服务器任务：资格感知执行与NodeANN速度来源审计

> **已完成，停止自动执行。** 服务器3aaa0ed完成102次静态与36次有限维护。资格执行修正有效，H在四个整体非空点被U-aware支配；不启动新的H任务。先读[结果复核与勘误](eligibility-3aaa0ed-review.md)：E分层重复计数、双Recall门和每图锁表开销需要纠正。下文保留作历史复现协议。

你负责执行已实现方案，不负责重新设计。获取本提示词所在代码提交；不要只复制脚本，也不要覆盖旧结果。用户明确要求修正固定ef与小合法集合的执行问题，并核查NodeANN为什么快。本次是新的执行控制实验，旧H停止结论和全部负结果保持原样，不据此宣称论文创新通过。

**最新基线定义：纯NodeANN为主基线A-graph。** 每个非空direct目录都建HNSW，`--scan-threshold 0 --eligibility-policy off`，不允许扫描回退或资格感知规划。旧scan512仅作A-hybrid-reference历史参考，不要求候选必须击败它；A-aware不运行。本修订取代3d75caa任务中的基线角色和运行数。

## 必读

1. [完整方案、预测、失败判据与维护范围](eligibility-execution-plan.md)。
2. [本地验证与实现记录](eligibility-local-report.md)。
3. 源码：`src/eligibility_index.h`、`src/closure_index.h`、`src/closure_research.cpp`、`src/dynamic_triad.cpp`，以及 `scripts/run_eligibility.py`；均在 `codes/directory-aware-vector-attr-search/`。
4. 历史 `2f8c4e0` 小ef报告及其 `work/closure/static-7654f1e-run1/manifest.json` 指向的原数据与轨迹。输入SHA必须匹配，不能临时生成另一份WIKI/SIFT替代。

## 允许与禁止

- 仅允许修正路径、CPU亲和核编号、构建环境。缺数据先定位原manifest；不得假装已测。
- 不改M=16、efConstruction=200、pool=64、k=10、ef15/20、候选eligible扫描阈值512。
- 主基线A-graph固定scan-threshold0、policy off；历史A-hybrid-reference固定scan512、policy off。禁止为主基线增加资格规划，也禁止人为减小建图参数、漏搜分组或添加无用工作来降速。
- 不添加ef64/256/1024，不搜索阈值，不改变H候选池/骨干/剪枝，不扩N，不运行旧run_closure的Dynamic任务。
- 新驱动有静态与有限maintenance两阶段：后者只审计当前样本的更新及压实成本，不依赖性能胜出，也不授权大规模动态实验。
- 所有计时串行。严禁一边跑本任务一边跑别的性能任务。
- 不把少距离当加速，不把同预算比当同质量比，不把旧查询重复当新增独立query。

## 1. 构建与语义检查

从仓库根执行：

```bash
bash codes/directory-aware-vector-attr-search/build_closure.sh
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_eligibility.py
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_dynamic_triad.py
```

若编译/语义失败，保留输出，先修复真实实现错误并说明；不得把它写成算法性能失败。HNSW依赖必须是 `ca426729609b3221563047ceb269cc1213e0d376`。

## 2. 静态对照

CPU 2如果不在当前允许亲和集合内，仅替换成可用核，并记录。输出路径必须新建；下面命令**不带 --smoke**。

```bash
python codes/directory-aware-vector-attr-search/scripts/run_eligibility.py \
  --previous-raw work/closure/static-7654f1e-run1 \
  --out work/eligibility/pure-nodeann-static-run1 --cpu 2
```

预期两个数据集102次计时、各1次独立oracle；运行数以实际manifest为准。原数据+前200Q+初始属性，不重采样。每数据集每重复：A-graph、U-off/U-cap/U-aware、H-off/H-cap/H-aware各ef15/20；Exact仅ef20；A-hybrid-reference各ef15/20。三次旋转顺序。

驱动记录源码/输入/二进制SHA、命令、逐query CSV、summary、runs.json、points.csv、comparisons.json。所有低质量点保留，没有自动选参。

先检查语义：
- 候选exact/hybrid精确路径result_ids必须逐ID等于GT，distance_calls必须等于E；候选资格前置后E=0不计算距离。纯NodeANN不提前获知E，不能对它套用该零距离要求。
- prepared mask逐ID与独立canonical验证一致；非法、重复、状态错误为0。
- gt_mode=precomputed，计时GT距离为0。
- cap只截断容量，不偷偷切精确扫描；hybrid才切小集合执行器。
- A-graph逐query扫描组数、扫描活对象数、扫描距离必须为0，graph_calls必须等于fanout；非空scope必须实际调用图。驱动自动拒绝违反这些条件的运行。

再回答性能：
- H-aware主比较为A-graph，另与U-aware隔离H增量；同时看H-cap对H-off、H-aware对H-cap。A-hybrid-reference仅作历史参考，不设置为额外胜出门槛。
- 非空macro/pair、均值、p50/p95/p99、距离、expanded、compile、qualification分别给出；empty单列。
- 同一确认组候选macro与pair均不低于对照才称本样本同质量胜出；其他时延比明确为同预算诊断。补充0.95/0.99质量目标下可用的固定网格点；不增预算追质量。
- 按E<ef、E≤512、E>512、direct/recursive及宽scope多属性AND分层。direct/recursive可从raw CSV汇总，不修改程序。
- Exact保留原图/pool构建与维护，虽然查询不使用图。其查询时间是有效控制，其RSS/build不是独立无图索引的空间/构建成绩。

## 3. NodeANN审计：必须具体回答“搜了多少图”

对A-graph与A-hybrid-reference分别报告：
- 初始局部HNSW索引容器数、图内活向量数、活对象总数；不能把容器数当向量数。
- 每query实际目录访问数、graph_calls、scalar_scan_group_calls、scanned_live_objects、graph_distance_calls、scan_distance_calls、merged_candidates、effective_ef_min/max。
- 按scope大小与direct/recursive分组，给出主基线实际搜索的局部图数量；历史混合版本才允许出现非空scope但零图调用的扫描情况。
- 验证graph_distance_calls + scan_distance_calls = distance_calls；不允许漏上层或底层距离。
- 比较A-hybrid-reference与A-graph的质量与时间，解释历史小目录扫描的影响；主基线角色事前固定，不能根据结果更换。只胜纯图、不胜混合参考时，应原样报告，不能声称全面优于NodeANN。
- 计时包含资格、目录枚举、查询、合并；GT/验证及仅用于输出的anchor_depth在计时外。不得将summary里的oracle扫描开销算进某一方查询。

## 4. 完整维护成本诊断

静态语义通过后执行；若静态性能仍输，也运行此有限包络以补齐本轮维护成本，而非追正结果。发现状态错误则先停下，不带错跑维护。

```bash
python codes/directory-aware-vector-attr-search/scripts/run_eligibility.py \
  --previous-raw work/closure/static-7654f1e-run1 \
  --out work/eligibility/pure-nodeann-maintenance-run1 --stage maintenance --cpu 2
```

预期两数据集36次计时。A-graph/U-aware/H-aware，ef20，3次；原mixed前缀与独立合成包络分别执行。完整原前缀中没有的事件与包络引入的事件区分。A-graph全程保持图索引，不在更新后切回扫描。

包络触发V/T、R重挂恢复、B批移动与收费M恢复、D达到closure墓碑压实门槛、I恢复。检查实际compaction字段，不能只根据计划声称已触发。按op给出整个latency_us、资格eligibility_update_us、图距离/插删、压实与重建、受影响对象数。只报实际事件；该有限包络不证明长期稳态、并发一致性或所有更新率下最优。

## 5. 必须交付

在 `researches/nested-attr-vector-search/<执行日期>-exps/` 追加报告、复现记录、小CSV/JSON及必要图；不要覆盖旧报告。同一天可用独立文件名。大raw保留work，保存位置与SHA。

结论按以下顺序：
1. 执行/资格语义是否正确，错误与修复历史；
2. E<ef强制全scope的问题实际消除了多少，cap与直接扫描分别贡献什么；
3. 同质量下是否超越纯NodeANN A-graph，H是否还有超出U-aware的独立贡献；与历史混合参考的差异另列；
4. NodeANN速度来源：实际图调用与扫描证据，不用“理论不可能”代替测量；
5. 全部构建、空间、资格及维护成本；
6. 可支持执行策略实测还是有资格重启H机制研究。没有独立H收益就停止H论文主张，不自动换算法。

提交源码修复（若有）、文档、小汇总，fetch后推送当前分支。不要提交大向量、work、依赖、二进制；不要强推或自行合并分叉历史。最后提供提交链接、准确运行数、主要质量—时间表、遗漏/未测事项和本机raw位置。
