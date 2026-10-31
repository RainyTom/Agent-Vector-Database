# 下一轮：子树闭包约束的可恢复邻接——本地实现与服务器执行合同

## 1. 决定

**不继续优化NPI二跳候选取得。下一轮验证：层级约束能否改变有效邻接，从而减少真实几何展开，在同质量下快于正常NodeANN。代码已在研究端实现，服务器不再承担方案制定。**

接受[502753c正式报告](https://github.com/RainyTom/Agent-Vector-Database/blob/502753c/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/neighborhood-report.md)：NPI相对普通二跳取得成本有效，但SIFT/WIKI同质量e2e仍分别为真实NodeANN的11.70×/24.38×。这是已实现、质量达标、查询成本不划算；不是未实现、召回失败或整个图过滤方向失败。旧E3未执行，不补跑以挽救旧方案。

本轮目标优先级：**同质量查询效率 → 层级机制增量 → 更新与构建成本 → 空间解释**。内存更省不是放行条件。

## 2. 实际方案，不是待服务器补齐的草图

每个live对象维护至多64个近邻候选，由普通HNSW生成；每条候选边保存一个64-bit几何见证集合。候选按源到候选的原始FP32 squared-L2、外部ID排序。先前候选w只有满足 `d(x,w)<d(x,y)` 且 `d(w,y)<d(x,y)` 才能成为x→y的几何见证。

H模式还要求w已被本次邻接选择保留，位于 `Subtree(LCA(owner(x),owner(y)))`，并且满足当前普通属性谓词。理由失效，就恢复原本可能被剪掉的候选边。不把这一规则冒充查询距离下界。

- **层级进入剪枝本身**，不只是向普通ANN交一个scope位图。源的候选LCA均落在同一祖先链上，每次展开构造至多64个局部闭包掩码；跨查询不缓存会被MOVE破坏的DFS层号。
- 搜索保留scope内、普通属性不合格对象的导航作用；它们不能作为压制其他边的合法见证，也不能返回。不同模式共用搜索前沿、合法结果heap及近似停止条件。
- 每个目录有enter/direct-end/exit标记，live对象有稳定token。实际order-stat treap支持子树cut/link、direct/recursive入口、计数、前驱后继。次序骨架保证scope内路径存在，不保证短路径。
- V/T只改属性；M/B移动对象token；R剪接整个目录段并修改parent，不重写后代DFS标签、不重算几何池。当前层级关系的查询编译成本计入e2e。
- I插入或恢复HNSW对象，刷新自身及其当前底层邻居对应的候选池/见证；D更新Live和图墓碑，累计墓碑达到图内存储对象的1/3时同步重建图与live池。全部距离、重建时间及触达池计数入账。

增量候选池是近似维护，不宣称等同每次全量重建的精确kNN。HNSW本身保存payload，加上输入向量、候选池、见证和目录token均有空间成本；不再宣传天然“只有一份向量”。

### 能证明到哪里

固定当前树、属性和候选池，H的**递归子树**邻接可由root邻接限制得到；依据是决定x→y的见证均在两者的最小公共子树内，按候选顺序归纳。实现没有追加度截断。

这个性质不包含内部目录的direct对象集合；root编译可能使用其后代见证，而direct查询排除后代。direct查询仍正常执行，但不套用递归子树定理。该性质也不是有限ef召回或次线性复杂度保证。

### 创新主张边界

直接前序是[RNSG v2 §3](https://arxiv.org/html/2603.12913v2#S3)的attribute-between见证及heredity；几何遮挡、图搜索、位图/掩码、Euler维护均不单独认领创新。本方案检验的是**允许scope为子树族时的闭包见证、随属性/拓扑改变恢复邻接、几何候选与当前层级编译解耦**这一具体组合。

DFS区间外但最小公共子树内的w可以成为H见证，却不能成为between见证，已有实际C++正例。必须进一步证明这种差异在真实数据中有搜索价值。R臂只是同池的between规则消融，不是完整复现作者RNSG。

## 3. 已提供文件

路径前缀为 `codes/directory-aware-vector-attr-search/`：

| 文件 | 已实现职责 |
|---|---|
| `src/closure_index.h` | HNSW候选、几何见证、四种编译规则、共用搜索、全部更新及压实 |
| `src/closure_order.h` | 稳定token、动态序列、recursive/direct范围、cut/link与骨架定位 |
| `src/closure_research.cpp` | 严格事件回放、独立canonical元数据、GT生成/消费、离线机制审计、CSV/JSON、自检 |
| `src/neighborhood_filter.h` | 原样复用502753c的数据加载与标量谓词合同；其静态Qualifier不用于新索引动态查询 |
| `build_closure.sh` | 固定HNSW版本的C++构建 |
| `scripts/run_closure.py` | 串行轮换、qidx分组校准/确认、输入/二进制/输出SHA、自动阶段门、动态及维护探针 |
| `tests/test_closure.py` | 真CLI、NodeANN/新索引共用GT、动态状态、坏GT拒绝、阶段门回归 |

`dynamic_triad.cpp`与公共`raw_l2_space.h`没有修改，已核其Git blob与502753c一致，分别为 `e3789114f2e1adbe6b316dc699020ae8449b5266`、`d08d747bec69343fd254ae13eaf1e515fffa909a`。hnswlib固定 `ca426729609b3221563047ceb269cc1213e0d376`。

## 4. 六臂及归因

| 臂 | 内容 | 回答的问题 |
|---|---|---|
| A | 真实NodeANN，原动态三臂runner的A；direct group≤512精确扫描，其余正常局部HNSW | 实际端到端是否胜出 |
| U | 同候选池，不做见证剪枝 | 64候选池/次序骨架是否先天缺少scope内捷径 |
| G | 普通几何见证，不保护层级闭包 | 层级约束是否改变质量—工作量权衡 |
| R | 当前Euler对象序上的strict-between见证 | 子树闭包相对通用区间规则是否有增量 |
| H | 子树闭包＋当前Extra合格见证 | 完整候选 |
| H0 | 同H，但普通属性不合格见证仍可剪边 | 资格变化时恢复邻接是否有价值 |

U/G/R/H/H0使用同一构建参数、固定候选池、骨架、距离核和搜索逻辑；脚本校验静态pool指纹一致。指纹只是状态标识，不是密码学证明。A不被人为限制到候选池或相同ef，独立选取同质量下的正常参数。

## 5. 最少且有判别力的实验

### E0：真实程序语义检查

本地已完成，服务器可作为环境检查运行现成回归，不重新设计：全事件、内部owner、非连续live ID、reserve/空索引首I、墓碑压实、负值/字面−1/极值、tag/AND/OR、超过64候选、见证移动/属性恢复、recursive限制性质、高ef完整结果、坏GT拒绝。退出非0时先修实现，不把它记为方案实验失败。

### E1：真实候选池机制审计（离线，不计性能）

沿用原WIKI完整11,869对象子树、SIFT12,000初始live输入，不重采样更容易的数据。默认各取既有轨迹前200条Q，在**初始属性快照**上执行；静态阶段不应用原轨迹更新。按qidx排序交替分配校准/确认，同一qidx不跨组。

确认组每条query审计最多8个scope内live源：输出U/G/R/H/H0完整候选ID列表，H与各控制的差异，并比较root H限制与子树H编译。全scope资格集合另作独立逐ID核对。只有recursive行用于heredity错误判定。

**预测**：应存在H/R实际邻接差异，以及H/H0因普通属性资格不同产生的边恢复；不保证每条query都有。若无差异，先看scope/属性/几何是否激活机制，不将no-op包装成新索引收益。审核的源有采样上限，不能声称检查了全部真实邻接。

### E2：同质量端到端与实际展开

脚本自动执行：6臂×3个ef（64/256/1024）校准；每臂选择满足质量门的最快非空均值配置，然后冻结，在确认组串行轮换3次。默认每数据集36次计时运行，另加2次独立GT生成和1次离线审计。质量不通过的点全部保留；没有合格校准点时用最大ef做失败诊断，不伪装为已通过。

共同要求：nonempty macro recall和pair recall均≥0.999；非法/重复/状态错误为0；空GT单列。所有臂消费同一个C++预计算GT tape，计时回放不读取GT向量或计算GT距离。输入GT不参与入口、路由、停止规则或选边。

报告每重复非空均值、p50/p95/p99、距离调用；同池臂还报告expanded、compile_us、pruned/retained、资格工作。另给宽scope（至少初始live的25%）＋至少两字段AND分层。不要均值/p50混比，不把新图expanded与NodeANN跨层ann_hops混成同一计数。

**预测与判别**：

- H应相对R/H0改变实际展开，而不是仅让同一批状态取得更便宜；真实目标仍是H/A的e2e胜出。
- U质量好、H差：见证选择/停止规则是优先归因，不先扩候选数。
- U也依赖长骨架且很慢：固定候选池缺少scope内有效连接，属于当前表示/覆盖问题。
- H比R少展开但e2e不降：当前层级/谓词编译或队列成本抵消收益；不只报告距离数。
- H赢A但不优于G/R：可以支持该整体实现效率，不足以单独支持层级闭包贡献。

**自动放行E3**：A/H校准及每次确认均过质量；全非空或预设宽scope多属性AND分层至少20个非空qidx；3次H/A均值比均≤1、中位比≤0.90；E1至少存在H/R差异。约10%耗时下降是本轮调度线，不是论文充分条件，也不否证更小收益。内存不设胜出门。不足20个qidx是覆盖不足，不能写成性能失败。

### E3：短动态与维护成本（仅放行的数据集）

不再改算法或在线参数。A/H用静态冻结ef，执行原轨迹前200条Q对应的完整事件前缀，保留中间所有更新；GT另行按事件版本生成。该前缀必须实际含更新，否则脚本拒绝将其当动态实验。

此外，脚本已实现**独立的合成维护包络**，不要求服务器手写事件：仅依据原树和owner计数，选择约10%live子树及合法外部目的节点，做R移走/移回、B迁移、逐对象M恢复，再删除初始live的1/3以触发一次全局同步压实，最后执行一次reserve/恢复I。每阶段用至多8条原query向量，在root、源、目的scope观察。无合法非平凡R/B时明确记`topology_unavailable`，不伪造更新。

这个包络是维护压力控制，不是原应用频率分布；与原轨迹分别出表。它暴露R/B成本和墓碑偿还，避免仅测未触发重建窗口。M恢复事件也收费，不能当免费reset。

原轨迹与包络均先进行离线全scope及池不变性审计，再进行计时回放。输出更新各类型mean/p50/p95/p99/max、geometry_calls、pools_rebuilt、compactions/rebuild_us，以及后续query质量/时延。脚本给出 `workload_us = Σquery_e2e + Σupdate_latency` 和加构建后的 `workload_with_build_us`。图插入/见证构建、同步回收、查询侧当前层级编译不可漏计。

验证及输出IO不算算法内核时间；原始串行回放不能宣称并发吞吐。结构bytes是容量估计、排除借用Dataset和暂态工作区；峰值RSS另报。保留构建双图共存时的峰值，内存更大不自动淘汰。

## 6. 服务器可直接执行

仓库根目录，先使用本次提交的代码；不要整分支合并旧实验的大CSV，也不要替换NodeANN算法。

```bash
git submodule update --init codes/directory-aware-vector-attr-search/deps/hnswlib
bash codes/directory-aware-vector-attr-search/build_closure.sh
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_closure.py -v
```

从502753c保留的manifest找到同一WIKI/SIFT data目录及原动态trace。只需填写这四个已有路径；data格式沿用`base.f32/test.f32/bindings.txt/attrs.txt/tree.txt`，可有`meta.txt`。

```bash
python codes/directory-aware-vector-attr-search/scripts/run_closure.py \
  --dataset "wiki=$WIKI_DATA" --trace "wiki=$WIKI_TRACE" \
  --dataset "sift=$SIFT_DATA" --trace "sift=$SIFT_TRACE" \
  --out work/closure/static
```

默认无并发；有空闲CPU时用`--cpu`固定到实际可用核心并记录机器负载，不与其他ANN任务并跑。输出目录必须不存在，失败和旧结果不会被静默覆盖。不要在正式实验加`--smoke`；该标志只供合成环境检查，不能授权真实E3。

检查 `work/closure/static/<name>/result.json` 的 `dynamic_admitted`。仅对true的名字执行，例如仅WIKI通过：

```bash
python codes/directory-aware-vector-attr-search/scripts/run_closure.py \
  --stage dynamic --static-results work/closure/static \
  --dataset "wiki=$WIKI_DATA" --trace "wiki=$WIKI_TRACE" \
  --out work/closure/dynamic
```

数据SHA、源trace SHA、C++二进制SHA及k必须与静态校准一致，不能悄悄换数据/重编算法后沿用冻结参数。需要修实现时记录失败、修复、重开全新输出目录；不将修正后数据覆盖历史。

## 7. 本地完成的验证及限制

[原始日志](experiments/closure-local-check.log)、[SHA与结构化证据](data/closure-local-check.json)。3项回归通过，包含真实C++核心、真实NodeANN、六臂混合事件回放、完整静态/动态脚本和强制压实探针。外部fixture为80对象、4维、6条向量query、75初始live，180条Q和9条更新；ef128时六臂实际返回均与canonical排名一致，非法/重复和计时GT距离为0。不是180条独立query。

首次自检把“每次I后池指纹必变”当要求，合法复活可保留原几何池，故删除这一偶然实现断言；保留独立结果/状态验证。未通过改算法追自检正结果。另纠正direct范围不适用recursive heredity。完整输入向量/真实WIKI和SIFT本机未具备，本轮新增真实数据性能运行0。

## 8. 交给服务器AI的执行要求

你是实施与实验端，不需要重新设计方案。先读本任务书与源码，沿用502753c的真实数据。执行已提供构建、回归、static流程；汇报E1机制是否激活、E2同质量e2e及实际expanded是否改善、失败发生在哪一层。只对自动放行数据执行dynamic及脚本生成的维护包络。

允许修编译/路径/真实语义缺陷，禁止为了正结果换停止规则、扩大pool、更改scope、换质量阈值、扩大N或用GT辅助搜索。若遇实现缺陷，先留失败证据再修，不把该次失败记为算法负结果。若实现正确但质量/效率不达标，按冻结设置交付，不转回二跳、WQR代理或维护调参。

必须提交小体积manifest/result/selection/runs与分析JSON、复现命令、输入/二进制SHA和失败记录；大raw CSV留work并给路径/SHA。结果至少区分：①实现错误；②覆盖/表示或质量不足；③减少展开但总时间不划算；④同质量效率胜出但层级消融不足；⑤查询与动态总成本均有支持。优先给均值对均值，不拿孤立距离次数或内存数字替代结论。

论文支持需要H相对同池近前序/消融的机制增量、同质量查询效率、跨scope与动态状态证据；仅R重挂零距离、更新更快、或总体实现赢A但层级规则无增量，只支持维护组件/实现实测。这个边界不要求每项资源全面胜出，也不预设本方案一定成功。
