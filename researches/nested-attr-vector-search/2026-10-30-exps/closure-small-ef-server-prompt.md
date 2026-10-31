# 服务器任务：小ef重跑正常NodeANN与现有闭包方案

**已完成／停止扩展（2026-10-31）：** 服务器2f8c4e0已完整执行48次小预算运行；A及同池U在四个全部非空点的时间、macro/pair均优于H。见[研究端收尾](../2026-10-31-exps/report.md)。本文与命令仅作历史复现，不再自动重跑、升预算、扩N或启动Dynamic；真实维护仍未测。

## 结论与本轮边界

用户明确纠正：正常实验应优先看k附近查询预算，而不是为达到`.999`自动上探大ef。本轮是**小预算诊断性重跑**，不是撤回5fc1be3、降低历史质量门后重算胜负，也不是修改算法追正。

当前k=10，因此固定两个查询点：`efSearch=k+5=15`、`efSearch=20`。建图`efConstruction=200`、M16、seed100不变；候选池仍64，其离线HNSW取候选的查询ef200也不变。三个“预算”不要混用。当前hnswlib的`setEf()/--ef`就是查询efSearch，不存在另外一个独立ef=20旋钮。

**只新增调度/离线分析，不修改NodeANN、closure核心、候选池、骨架、入口、停止规则或属性。** 重跑A/U/R/H，两个查询预算、三次串行轮换；WIKI/SIFT各200条原Q。合计48次计时运行，另2次独立GT生成。没有选择/重选ef，没有为召回自动升到64/256/1024，没有Dynamic调用。

## 1. 代码提交如何接入

本任务书与新增脚本所在的**代码提交只新增文件**，可在保留5fc1be3结果的服务器分支上单独cherry-pick；不要整分支merge本地知识库或旧研究历史。提交哈希由研究端交付消息给出。

本轮新增：

- `codes/directory-aware-vector-attr-search/scripts/run_closure_small_ef.py`：固定小预算重跑、原始输入/源码绑定、全部质量点保留。
- `codes/directory-aware-vector-attr-search/scripts/analyze_closure_termination.py`：读取旧CSV分析ef合法堆与全scope遍历；不运行搜索。
- 本目录`closure-5fc1be3-review.md`：源码/聚合归因与未解决项。

驱动会校验11份执行器/依赖/构建脚本的Git blob与7654f1e一致，再重新构建原C++候选和NodeANN。任何不一致停止，不用本轮名义偷偷修核心。两份新脚本的SHA另入manifest。

## 2. 先对现有CSV做离线归因，不是ANN重跑

```bash
python3 codes/directory-aware-vector-attr-search/scripts/analyze_closure_termination.py \
  --raw work/closure/static-7654f1e-run1 \
  --out work/closure/termination-7654f1e-run1.json
```

它验证原CSV SHA，并按数据集/phase/ef/臂分开统计：

- `eligible_card < ef` 的非空/空查询数及比例；
- 这些query的expanded与distance是否等于scope；
- 这些query占总expanded的比例；
- 其余query中H/R同ef展开计数是否仍接近；
- 编译时间占比。

源码已可推出：当前合法结果堆不到ef不会触发距离早停；有覆盖scope的次序骨架时，E<ef会遍历整个scope。它不是实现偏离规格，也不证明所有真实query都触发了这个分支。WIKI漏ID8122的query有E8511>ef1024，不能用E<ef解释。

如果这里报不变量冲突，保留JSON与原CSV，报告具体行；不要修改日志、静默丢query或先改搜索。相同expanded数不证明相同访问集合/顺序，CSV没记录首次发现边，不能据此补造因果。

## 3. 执行正式小预算重跑

从仓库根目录：

```bash
python3 codes/directory-aware-vector-attr-search/scripts/run_closure_small_ef.py \
  --previous-raw work/closure/static-7654f1e-run1 \
  --out work/closure/small-ef-15-20-run1 \
  --cpu 2
```

只需旧raw目录仍在，无需重新填写四个数据路径。驱动从旧manifest取得原数据及源trace，逐文件校验SHA，再取原轨迹前200条字面Q；初始快照冻结，更新不执行。旧calibration/confirmation trace的query多重集必须与它一致，qidx组保持原样。

- 默认两数据集；若有数据路径故障可先用`--dataset wiki`或`--dataset sift`完整执行一个，不换数据。
- CPU2沿用上轮可用核心；若不可用，只改成实际空闲核心并记录原因。
- 不传`--smoke`。该选项只用于本地构造输入校验，不能混作真实数据。
- 输出目录须不存在；失败或成功均不能覆盖旧结果。
- 驱动生产模式重建原两C++程序，保存构建日志、源blob、二进制SHA、命令与亲和性；不导入/重建新算法。
- 先独立C++精确GT，计时运行只消费tape。非法/重复/状态错误、GT距离必须0；低召回合法结果**保留并继续完成固定网格**，不把它当执行错误中断。

## 4. 与上轮实验不同之处

上轮六臂×64/256/1024先校准后选点，以`.999`作门；本轮四臂×15/20为**预先固定的两个点**，对每点直接重复三次。不进行参数筛选，不声称这些点已经同质量。

每次程序用全部200条Q；输出同时保留：

1. 全部非空；
2. 历史calibration非空子集；
3. 历史confirmation非空子集；
4. 预设宽scope＋多字段AND子集。

重用旧qidx并不形成新独立测试集。SIFT历史确认非空只有16个qidx的覆盖限制仍在，不因重复三次或把两组并起来就写成新query。

本轮不把`.999`作为小预算点的淘汰/选参线。报告未四舍五入的nonempty macro、pair、逐query遗漏以及时间，绘制Recall—time取舍。历史`.999`失败仍保留；不能把低ef的较低Recall“宣布通过”。

## 5. 必须汇报的指标与判断

### 表一：同预算点，不能直接叫同质量加速

每数据集、每ef列A/U/R/H的三次非空均值、p50/p95/p99、macro/pair、距离数；同池臂加expanded、compile_us、E<ef占比。均值对均值，所有质量点保留。

输出`comparisons.json`中的`same_budget_H_over_A`只是同预算耗时比。只有质量相当且定义一致时才讨论同质量效率；若H质量更差，同时报告差多少，不能只给倍数。若小预算H仍慢但质量更差，应直接记为该预算点缺乏优势，不继续升ef补救。

### 表二：机制归因

- H/R：同ef是否仍几乎相同expanded？将E<ef强制遍历行与其他行分开。
- U/H：不做闭包剪枝时，候选覆盖/耗时/展开怎样？U也慢说明不能只怪LCA。
- H中的全池检查、动态rank、hierarchy_steps和compile占比；少边不是少访问证明。
- 原始高ef结论作为背景，不与新数据混池成一个确认集。

### 执行正确性与停止

实现不一致、非法/重复/状态错误、输入/GT SHA不一致：停止并报告缺陷。近似Recall不足不是状态错误，不修算法追结果。预算固定，禁止新增64/256/1024或改k/池/停止规则。

本轮没有真实更新、维护包络、扩大N、新索引或新质量门。`dynamic_admitted`固定false是本任务边界，不是测得动态失败。

## 6. 交付产物

- `manifest.json`：源/二进制/数据/trace/tape SHA、固定参数、命令、完成状态、无选参/无Dynamic声明。
- `runs.json`：全部48次记录、原summary、分组质量/时间和停止归因。
- `points.csv`：所有臂/预算/重复/分组，低质量点不删除。
- `comparisons.json`：H/A同预算取舍、H/R实际expanded计数。
- 每query原CSV/日志留work；提交小聚合、离线旧CSV归因JSON、报告与实际复现命令，记录大文件路径/SHA。

报告先回答：正常小预算下H是否有可用的质量—时间点？如果没有，主要是全scope遍历、scope内导航不足、还是每展开源的元数据编译成本？证据不够的分支标待核，不强归因。

## 7. 本地已验证

[本地小预算流水线证据](data/closure-small-ef-local-check.json)：真实NodeANN及原closure程序在80对象构造输入上，A/U/R/H×15/20×3共24次执行，180条Q/次，GT距离0；无参数选择、无Dynamic；原始快照SHA改变会拒绝，smoke不能冒充真实数据。是流程/功能证据，不是新的WIKI/SIFT结果。

另有[48对象源码性质探针](data/closure-budget-probe.json)：E4<ef8时，U/G/R/H/H0保留边数不同，但全部expanded=distance=48；空GT也如此。这不测真实E<ef比例，不能替代第一步旧CSV统计。
