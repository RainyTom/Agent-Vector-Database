# 3aaa0ed复核：保留执行修正，停止H；纠正重叠分层与内存归因

日期：2026-10-31。原始证据为[服务器正式报告](https://github.com/RainyTom/Agent-Vector-Database/blob/3aaa0ed/researches/nested-attr-vector-search/2026-10-31-exps/eligibility-server-report.md)。本轮读取公开聚合与打包源码、重算数字，并运行本地C++类型尺寸探针；没有重跑已报告ANN失败、修改算法、扩大N或启动新服务器任务。

## 1. 研究结论

1. 接受102次static、36次有限maintenance完成和服务器逐query语义核对。这里无法访问未提交raw，因此不能冒称在本机独立重核27,168条raw。
2. 资格前置、合法容量截断和小集合精确扫描是实际有效的执行修正；不是整个改进失败。
3. H-aware在WIKI/SIFT、ef15/20四个整体非空点均被U-aware在时间及macro/pair质量上支配；相对纯NodeANN亦无质量不降的胜出点。停止当前H机制的论文主张及继续追参。
4. Exact实测查询路径比A-graph更快且质量不降，说明当前样本并未证明需要额外图导航。该证据不否定大E/更大规模下的ANN价值，也不授权自动扩N追正结果。
5. 下列汇总问题应纠正，不影响第2–4项结论。它们是分析/叙述错误，不是核心实现错误或新的算法失败。

## 2. 关键勘误：E分层重叠导致重复计数

读取[static-summary.json](https://raw.githubusercontent.com/RainyTom/Agent-Vector-Database/3aaa0ed/researches/nested-attr-vector-search/2026-10-31-exps/data/eligibility-static-run1/static-summary.json)与[bands.json](https://raw.githubusercontent.com/RainyTom/Agent-Vector-Database/3aaa0ed/researches/nested-attr-vector-search/2026-10-31-exps/data/eligibility-static-run1/bands.json)。在ef20下，`0<E<ef`是`0<E≤512`的子集，不能再相加。

正确总量按：`all_nonempty.expanded_mean × nonempty_count`，并用互斥的`E≤512`与`E>512`之和复核。每项按三次均值折算为单次查询集的总展开，三次非空查询数一致；不是把三次重复或不同变体相加。

| U，ef20 | WIKI | SIFT |
|---|---:|---:|
| off正确总expanded | 125142 | 234244 |
| cap正确总expanded | 105976 | 169216 |
| aware正确总expanded | 2879 | 6626 |
| off中E<ef展开量 | 106060 | 194400 |
| off中E<ef实际占比 | 84.75% | 82.99% |
| cap相对off减少 | 15.32% | 27.76% |
| aware相对off减少 | 97.70% | 97.17% |

报告中的231202 = 125142 + 106060；428644 = 234244 + 194400，恰好重复加入了E<ef。192870/298588的cap总量同样重复。因此“45%”不是样本变化，而是分母错误；与上一轮约83%–85%实际一致。H-off的对应占比为84.71%/82.98%。

时延主表没有使用这组重叠加总，故5.564→4.848→0.336ms、10.983→8.047→0.421ms及H/U优劣判定不因此撤回。用户摘要§2的数据是U臂；不要误记成H的数字。

## 3. 质量门与统计口径

### 3.1 0.95/0.99必须同时检查macro与pair

报告§3.3列入WIKI的U/H off与cap不满足联合0.95门：U-off/U-cap的pair为0.94439，H-off/H-cap连macro也仅0.948125；H-aware的pair为0.94254，同样不通过。

WIKI的联合0.95可用变体为A-graph、A-hybrid-reference、U-aware及Exact；H-aware不在其中。WIKI联合0.99下A-graph仅ef20通过，ef15的pair为0.987025。SIFT在0.95下H-off/H-cap也因pair不足被排除。全部固定点列表见[复核JSON](data/eligibility-3aaa0ed-review.json)。

### 3.2 有效ef均值不是范围

[nodeann-audit.json](https://raw.githubusercontent.com/RainyTom/Agent-Vector-Database/3aaa0ed/researches/nested-attr-vector-search/2026-10-31-exps/data/eligibility-static-run1/nodeann-audit.json)的ef20记录，WIKI `effective_ef.mean=16`、SIFT为15，p50/p95/p99/maximum都是20。这是把空scope零值并入均值后得到20×160/200和20×150/200；不能写成“有效ef范围16，min=max”或认为实际改成ef15。主表非空与审计表全部query也必须分开。

### 3.3 其他分析边界

- 打包源码`pct`对`p*n`先取int，非整数时不是现有driver使用的ceil nearest-rank；应统一后只读重算相关审计分组分位数。主表static-summary的分位数来自原driver，不能把此问题扩大为全部分位数失效。
- `nodeann_audit`把scope≥512组上界硬编码为11869，SIFT最大scope12000可能漏在该分组外；全体per_query与主表不受这个分组遗漏影响。
- 打包器把所有E=0行都要求零搜索只适用于本次实际空scope输入；不能推广成纯NodeANN对非空scope、无合法对象也无需搜索。候选资格前置的E=0才有该一般执行保证。
- 已实际读取并视觉检查提交的PNG：展示的是macro recall与时延，不能凭图中的0.95/0.99竖线判定联合macro/pair门；标签较拥挤，但未据图替代JSON判定。

## 4. NodeANN速度与内存：事实成立，归因须收紧

保留纯图与历史混合的容器数/图内向量数、图调用、扫描及时间实测。纯图仍是事前固定主基线，不因结果调整。许多小目录走扫描的历史混合版本更快，是有效控制证据；但尚无逐目录配对profile证明每个16–64对象目录都满足同一速度比，不将整体对照扩大成逐目录定律。

**485.5MB不能只归因于2倍容量预留。** 当前固定hnswlib源码：

- `deps/hnswlib/hnswlib/hnswalg.h:26`：`MAX_LABEL_OPERATION_LOCKS=65536`。
- `:46`：每个图对象独立持有`std::vector<std::mutex> label_op_locks_`。
- `:102`：构造时按65536个锁初始化，与该目录对象数无关。

本机实际编译运行`sizeof(std::mutex)`为40字节。因此本地ABI下每图仅此锁表payload为2,621,440字节，143图合计374.87MB，2图仅5.24MB。这是固定每图开销，未包含向量、边、其他锁、分配器等。[INFERENCE] 若服务器ABI相同，它能解释大部分RSS差值；服务器mutex尺寸及分配驻留尚未实测，不能称已完成服务器RSS精确拆账。

报告§5.1另有一句“SIFT RSS反而低于closure臂”与其自身表格相反：A-graph为59.7MB、closure约39.5MB。应纠正叙述，不反改表格。

本轮不修改hnswlib锁表或优化NodeANN，这里只是解释成本来源；也不能把该固定库实例开销说成纯NodeANN算法必然的空间复杂度。

## 5. Exact和维护的正确研究含义

Exact非空查询WIKI约0.205ms、SIFT约0.089ms，macro/pair均1；A-graph ef20约0.901/0.319ms。该路径真实执行资格取得、原始距离与top-k，无免费GT决策，因此是有效、可执行的查询控制，不应描述成信息oracle或本质“不可部署”。但现有runner依然构建/维护图与pool，尚未测独立无图结构的build/RSS/生命周期，不能把它包装为已经完成的轻量新索引。

保留维护包络中U/H的全量压实、昂贵I与便宜M/B证据。一次全局压实与285次局部压实不是相同工作单位；比较总事件耗时和尾停顿，不用次数少来宣布维护更好。零距离元数据更新不等于总维护成本低。该轮已有真实输入上的有限维护证据，不能再说“维护完全未测”；未测的是长期动态负载、并发、稳态债务和大N。

## 6. 后续决定

- 结束本轮服务器任务；不再扩H预算、池、规模或围绕闭包追加诊断寻找正结果。
- 保留资格集合取得、执行路径选择与完整维护计费组件；将“执行缺口修复”和“H无独立收益”分别索引。
- 后续新机制必须先说明在哪个E/维数/更新负载窗口有机会超过已运行的Exact查询路径，并与U隔离机制增量；这是选择可行窗口，不是现在直接扩规模。
- 本次只需要分析勘误，不需要重跑ANN。远端原报告保留；本地新增本复核及JSON，避免修改未拉取的服务器raw或伪称已修复远端文件。

## 7. 实际执行与限制

本地解析34个静态聚合点、204条分组记录；重算互斥分母、四个H/U支配关系及联合质量门；检查固定源码，运行C++mutex尺寸程序。未调用真实WIKI/SIFT ANN，未运行新回归套件，未获取服务器逐query raw，未进行新文献检索。没有新的论文/关系条目需要入库。
