# 2026-10-31｜资格感知执行与NodeANN审计：本地实现交付

## 结论

实现及真实C++程序烟测完成，可交服务器跑原WIKI/SIFT。没有在本机复跑用户报告的真实数据失败，也没有新的真实数据加速结论。此次依据用户的新要求，修正E<ef执行设计并补充基线审计，不撤回当天早先对旧H的停止结论。

入口：[完整方案](eligibility-execution-plan.md)、[服务器提示词](eligibility-server-prompt.md)、[小体积验证证据](data/eligibility-local-check.json)。

## 实现

- `src/eligibility_index.h`：真实目录树+稀疏direct-owner word postings+192个属性bitplanes，生成完整Live/Scope/Extra资格mask及E。位平面存储192份，但区间上下界各作比较，单word属性读取最多256次；不得混淆存储数与计算量。
- `src/closure_index.h`：off/cap/hybrid/exact，E=0免搜索、动态合法结果容量、小合法集合精确扫描；不改原pool/几何剪枝/骨干/rank编译。V/T/M/B/R/D/I同步维护资格索引。
- `src/closure_research.cpp`：逐query输出资格工作、路径、有效ef；独立canonical逐ID核对完整mask；构建、空间、每事件维护计时。
- `src/dynamic_triad.cpp`：基线同样可用资格索引；图路径按owner计数并跳过零合法分组，局部ef限于局部E；新增图调用、扫描、距离分账、合并候选与局部ef范围。
- `scripts/run_eligibility.py`：固定协议、原输入SHA、独立GT、串行轮换、全部点保留；static与有限maintenance两入口。
- `tests/test_eligibility.py`：语义与执行路径回归，非性能断言。

Exact控制只是同构建条件下绕过几何搜索；图/pool仍构建并维护，因此不拿其RSS/build作独立无图结构的成绩。参数自适应目前是精确E驱动的容量截断和路径选择，不是已经学习出最优ef函数。

## 实际验证

### 1. 构建与回归

两程序均由现有build脚本构建，hnswlib版本 `ca426729609b3221563047ceb269cc1213e0d376`。依赖自身有unused parameter/unsigned comparison等警告，未通过屏蔽警告改变依赖。

最终8项CLI回归通过：eligibility 2项、dynamic_triad 3项、closure 3项。覆盖：

- 80对象、4维、跨64-bit word；AND/OR、direct内部节点、空tag模式、int64极值与字面-1区间。
- 全部V/T/M/B/R/D/I事件，插入reserve、删除恢复、树重挂与批迁移。
- 600对象、2维fixture，合法数依次为0/5/12/40/543/600；cap处理E<k、hybrid在512阈值切换精确/图路径。A在后两条查询各实际调用1张图；前四条不调用图。
- 旧GT的非法ID、错状态、重复、坏schema及距离伪计数继续拒绝。

首次回归发现集成错误：NodeANN输出新增诊断列后，其旧GT reader只允许26/27列，导致读取自身输出失败（3个子用例错误、6个子用例断言失败）。已改为验证固定GT前缀、附加列数量/重复与可选gt_distance_calls；未放宽GT身份、资格、召回或距离核对。最终相关3项回归全部通过。此项是已修复实现错误，不是算法负结果。

### 2. 完整调度烟测

输入为原有测试fixture：80对象（初始75活）、4维、6个源query，mixed轨迹180条Q。执行真实C++二进制；Python只组织输入、GT核对和汇总。

|最终入口|计时运行|query记录|状态/非法/重复/mask/计时GT错误合计|图/扫描距离分账错误|
|---|---:|---:|---:|---:|
|static-final|57|10260|0|0|
|maintenance-final|24|3768|0|0|

静态路径统计：legacy 4320、exact 2520、empty 1980、graph 1440。维护路径包含exact 2214、empty 612、legacy 942；实际观测6次compaction记录（包括重复运行），不是6种独立维护场景。hybrid/Exact精确路径逐ID等于独立GT，距离次数等于E。

保留第一次成功调度 `static/maintenance`；随后仅将A输出诊断anchor_depth恢复到旧计时边界外，最终重新运行至 `static-final/maintenance-final`。此输出诊断不参与搜索决策，H对应诊断也在计时外。报告与证据JSON采用最终目录，不将前后轮合计当作新增独立实验。

### 3. 默认off没有偷偷换算法

提取7654f1e历史源码，在同一编译器/依赖下构建冻结NodeANN与closure二进制。对同一合成mixed轨迹，比较A(scan512)、A(scan0)、U、H × ef15/20共8组、1440条配对query：result IDs、GT IDs、距离数、hop、scope/E、fanout、scan_nodes完全一致；U/H的expanded/pool候选/保留/剪枝计数也一致。

该比较在恢复anchor_depth诊断计时边界前执行；随后改动只调整计时起点，最终8项回归与81次调度烟测通过。不把配对一致性延伸为全部真实数据已验证，也不比较其时延。

## NodeANN为什么“多目录但不一定多图”

本地首条合成root查询scope=75，提供一个实际执行例子，不作为速度证据：

|变体|实际局部图调用|direct扫描组|向量距离次数|
|---|---:|---:|---:|
|A-off，scan512|0|8|75|
|A-graph-only，scan0|8|0|83|
|A-aware，E≤512|0|0（一次合法集合扫描）|75|

因此，“树上8个有对象的目录”不能直接推导为“原A搜索8张HNSW”。原A的小目录精确扫描在谓词之后才计算距离，而且每个对象仅直接归属一个局部组。用户早期设想的全非空节点建图与当前混合实现并不完全相同；新实验把两者分开报告，主基线不被较慢版本替换。

真实WIKI/SIFT中这个因素贡献多少仍待服务器新增计数；当前不以合成例子解释完全部真实时延。

## 成本、限制与下一步

- 已实测同步资格更新与图维护/压实程序路径；没有真实数据维护时延结论、并发或长期稳态结论。
- 资格取得成本完整计时；A大集合路径还需O(E)按owner计数，可能变慢。H大E路径保留原昂贵编译，本轮可能依旧输。
- 主要目标是同质量查询效率；内存辅助，不要求必须同时胜出。若优势只来自小集合扫描，则只支持执行策略实测，不支持H新索引论文主张。
- 原始轨迹仍是旧query；真实结果应单列历史确认组并披露SIFT非空qidx覆盖不足，不把重复当样本扩容。
- 下一步严格执行服务器提示词的114次静态与48次有限维护，不扩大N，不调整参数追正结果。

原始本地证据：`work/eligibility-execution-local/`，不入库；小JSON持久化于本目录data。复现命令见 [experiments/eligibility-local-reproduction.md](experiments/eligibility-local-reproduction.md)。
