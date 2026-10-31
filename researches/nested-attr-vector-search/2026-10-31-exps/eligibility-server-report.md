# 资格感知执行与纯NodeANN审计：E<ef缺口已修正，H仍无独立增益

日期：2026-10-31。源码：`4fc4b00`（cherry-pick `be69e3e`/`b81da0a`/`a9b7278`/`6b4640e`）。全部旧结果保留，旧H停止结论不改写。

**六条结论（按任务书顺序）：**

1. **执行与资格语义正确。** 102次静态＋36次维护运行、20,400＋6,768条query记录中，非法/重复/状态错误、资格mask不一致、计时GT距离全部为0；930条Exact与2,592条hybrid精确路径逐ID等于GT且距离数等于E；A-graph 2,400条query的扫描组/扫描活对象/扫描距离/资格时间全为0，`graph_calls==fanout`。未发现需要修复的实现缺陷（唯一改动见§1.3的可视化脚本）。
2. **`E<ef`全scope遍历被实质消除。** 这类query在U-off/H-off中占全部expanded的45%，实际耗时SIFT 40.1ms/条。cap只截断容量：总expanded −30.3%（SIFT）/−16.6%（WIKI）、同Recall下时延 −27%/−13%；hybrid直接扫描：总expanded −98.5%/−98.8%，该band降至0.053ms/条。
3. **同质量下没有超越纯NodeANN A-graph。** H-aware在两个数据集Recall都更低（WIKI −3.4pp、SIFT −2.3pp），同预算时延WIKI 0.40×、SIFT 1.83×。H相对U-aware**也是负增量**（更慢且Recall更低）。A-hybrid-reference反而在质量匹配下快于A-graph（0.46×/0.92×），说明纯图基线本身比历史混合版本慢。Exact控制在两数据集上质量匹配且最快（0.205ms/0.089ms），但它是控制而非可部署索引。
4. **NodeANN的速度来源是“小目录精确扫描”而不是“8张图”。** WIKI实际建143张局部图（11,869向量）时每query调用31.5张图、2,191次图距离，e2e 0.72ms；历史混合只建2张图（5,154向量）、每query 0.42张图＋31.1个扫描组（1,470个活对象），e2e 0.35ms。SIFT为14图/12,000向量对8图/9,600向量。
5. **完整成本已收费。** A-graph因每个非空目录2×容量建图，WIKI峰值RSS 485.5MB（index估计51.4MB），远高于混合85.0MB；资格位图另需约0.96MB。维护包络中H/U的同步压实单次可达7.5s，总更新耗时是A-graph的4.2倍（WIKI）。
6. **只支持执行策略实测，不支持H机制论文主张。** 收益完全来自资格前置＋小集合精确扫描（U-aware与H-aware在同路径上几乎同速），H的闭包规则在本样本上是负贡献。不自动换算法、不扩大N、不重启Dynamic。

## 1. 执行/资格正确性

### 1.1 实际运行

| 阶段 | 命令 | 计时运行 | query记录 | 结果 |
|---|---|---:|---:|---|
| static | `run_eligibility.py --out work/eligibility/pure-nodeann-static-run1 --cpu 2` | 102 | 20,400 | complete，764.35s |
| maintenance | `... --stage maintenance --out work/eligibility/pure-nodeann-maintenance-run1 --cpu 2` | 36 | 6,768 | complete，301.17s |

- 主基线固定 **A-graph**：`--scan-threshold 0 --eligibility-policy off`；A-hybrid-reference 固定 `scan 512 / off`，只作历史参考；未运行 A-aware。
- 参数未改：M=16、efConstruction=200、pool=64、k=10、ef15/20、阈值512。无选参、无新预算、无Dynamic、无扩大N。
- 数据、原trace、GT均来自5fc1be3的manifest并逐文件SHA校验；原200条Q与初始属性快照不变。hnswlib `ca426729…`。
- CPU2在可用亲和集合内，串行单worker，未并跑其它性能任务。

### 1.2 独立复核的不变量

包脚本 [package_eligibility.py](experiments/package_eligibility.py) 重新读取每query CSV（不依赖驱动自述）：

| 检查 | 结果 |
|---|---|
| Exact路径 `result_ids==gt_ids` 且 `distance_calls==E` | 930行通过 |
| hybrid精确路径同上 | 2,592行通过 |
| E=0不计算距离/不调用图 | 4,590行通过 |
| cap是否偷用精确扫描 | 0行（cap只截断容量） |
| A-graph扫描组/扫描活对象/扫描距离/资格时间 | 2,400行全0 |
| A-graph `graph_calls==fanout` 且非空scope确实调图 | 通过 |
| `graph_distance_calls+scan_distance_calls==distance_calls` | 0不一致 |
| 非法/重复/状态错误/mask不一致/计时GT距离 | 全0 |

完整结果：[semantic-check.json](data/eligibility-static-run1/semantic-check.json)（`problems: []`）、[maintenance 复核](data/eligibility-maintenance-run1/maintenance-summary.json)。原始artifact哈希在打包时逐个重算，未发现漂移。

### 1.3 已发生的修正

本轮唯一修正是**可视化脚本**：首版 [plot_eligibility.py](experiments/plot_eligibility.py) 之外的旧闭包图例逻辑不适用新臂集合，重新实现为只读 `static-summary.json` 的散点；不涉及程序、数据或计时。核心、NodeANN、资格实现与调度**未改动**，也没有出现需要修复的语义缺陷。

## 2. `E<ef`全scope遍历消除了多少

### 2.1 修正前的分布

U-off/H-off（off策略，原停止规则）按E分层，ef=20，非空：

| 数据集 | band | query数 | macro/pair | 平均e2e | expanded均值 | 占全部expanded |
|---|---|---:|---|---:|---:|---:|
| WIKI | E<ef | 83 | 1.0000/1.0000 | 8.551ms | 1277.8 | 45.9% |
| WIKI | E≤512 | 133 | .98797/.98022 | 6.333ms | 919.3 | 52.9% |
| WIKI | E>512 | 27 | .83704/.83704 | 1.778ms | 106.6 | 1.2% |
| SIFT | E<ef | 34 | 1.0000/1.0000 | 40.097ms | 5717.6 | 45.4% |
| SIFT | E≤512 | 83 | .96024/.94754 | 19.240ms | 2742.4 | 53.1% |
| SIFT | E>512 | 67 | .96269/.96269 | 0.754ms | 98.9 | 1.5% |

`E<ef`的expanded等于scope：SIFT 5,717.6、WIKI 1,277.8（该band的scope均值）。这与上一轮65%–85%的结论方向一致；本轮样本中它占45%的展开量。

### 2.2 cap与直接扫描各自贡献

| 数据集 | 臂 | 总expanded | 相对off | 非空均值 | 相对off | macro |
|---|---|---:|---:|---:|---:|---|
| WIKI | U-off / U-cap / U-aware | 231,202 / 192,870 / 2,879 | −16.6% / −98.8% | 5.564 / 4.848 / 0.336ms | −13% / −94% | .9625 / .9625 / .9725 |
| SIFT | U-off / U-cap / U-aware | 428,644 / 298,588 / 6,626 | −30.3% / −98.5% | 10.983 / 8.047 / 0.421ms | −27% / −96% | .9613 / .9613 / .9833 |

- **cap**：只把合法结果容量限制到E，使距离早停在E-th最优处；同Recall下减少约1/3（SIFT）到1/6（WIKI）的展开与时延，但没有消除图遍历，`E<ef`band仍有3,805/1,047次展开。
- **hybrid（直接扫描）**：`E≤512`走精确枚举，展开归零、距离等于E，并且**Recall反而上升**（WIKI +1.0pp、SIFT +2.2pp），因为小集合返回的是精确答案。
- 两者不重复计费：cap的收益在同Recall下成立，hybrid的收益同时改变Recall，不能并列成“同质量加速”。

## 3. 同质量与效率

### 3.1 静态主表（非空；三次重复均值；单位ms）

**WIKI**（160条非空／160个qidx，40条空GT单列，下同）

| 臂/ef | macro | pair | mean | p50 | p95 | p99 | 距离数 |
|---|---|---:|---:|---:|---:|---:|---:|
| A-graph / 15 | .991250 | .987025 | 0.900 | 0.072 | 4.607 | 5.259 | 2649.5 |
| A-graph / 20 | .995000 | .992586 | 0.901 | 0.068 | 4.543 | 5.097 | 2739.3 |
| A-hybrid-ref / 15 | .995000 | .992586 | 0.409 | 0.013 | 1.978 | 2.363 | 1124.9 |
| A-hybrid-ref / 20 | .996250 | .994439 | 0.433 | 0.014 | 2.022 | 2.440 | 1181.7 |
| U-off / 20 | .962500 | .944393 | 5.564 | 0.629 | 35.734 | 82.445 | 1600.4 |
| U-cap / 20 | .962500 | .944393 | 4.848 | 0.590 | 35.010 | 78.082 | 1595.5 |
| U-aware / 20 | .972500 | .959222 | 0.336 | 0.027 | 2.064 | 3.771 | 518.1 |
| H-off / 20 | .948125 | .923077 | 5.930 | 0.678 | 40.741 | 88.321 | 1473.4 |
| H-cap / 20 | .948125 | .923077 | 5.217 | 0.601 | 40.106 | 85.304 | 1468.5 |
| H-aware / 20 | .961250 | .942539 | 0.362 | 0.026 | 2.346 | 4.494 | 402.4 |
| Exact / 20 | 1.000000 | 1.000000 | 0.205 | 0.027 | 1.161 | 2.180 | 594.2 |

**SIFT**（150条非空／48个qidx，50条空GT）

| 臂/ef | macro | pair | mean | p50 | p95 | p99 | 距离数 |
|---|---|---:|---:|---:|---:|---:|---:|
| A-graph / 15 | .999333 | .999230 | 0.298 | 0.145 | 1.151 | 1.435 | 3728.5 |
| A-graph / 20 | 1.000000 | 1.000000 | 0.319 | 0.160 | 1.152 | 1.491 | 3980.0 |
| A-hybrid-ref / 15 | .999333 | .999230 | 0.274 | 0.151 | 0.971 | 1.248 | 3078.9 |
| A-hybrid-ref / 20 | 1.000000 | 1.000000 | 0.291 | 0.161 | 0.990 | 1.230 | 3280.1 |
| U-off / 20 | .961333 | .955350 | 10.983 | 1.172 | 81.846 | 86.180 | 2402.9 |
| U-cap / 20 | .961333 | .955350 | 8.047 | 1.230 | 64.249 | 72.693 | 2264.5 |
| U-aware / 20 | .983333 | .980754 | 0.421 | 0.095 | 1.518 | 1.998 | 558.2 |
| H-off / 20 | .953333 | .946112 | 11.859 | 1.563 | 87.874 | 92.836 | 2312.5 |
| H-cap / 20 | .953333 | .946112 | 8.708 | 1.591 | 68.021 | 77.016 | 2174.0 |
| H-aware / 20 | .977333 | .973826 | 0.585 | 0.075 | 2.159 | 3.127 | 474.1 |
| Exact / 20 | 1.000000 | 1.000000 | 0.089 | 0.059 | 0.242 | 0.350 | 1151.9 |

ef15的完整数值在 [static-summary.json](data/eligibility-static-run1/static-summary.json)／[points.csv](data/eligibility-static-run1/points.csv)，未删任何点。

### 3.2 判定（按冻结规则：macro与pair均不低于对照才可称本样本同质量）

| 对照 | WIKI | SIFT |
|---|---|---|
| A-hybrid-ref vs A-graph | **同质量胜出**（Recall更高，0.46×） | **同质量胜出**（0.92×，ef15） |
| H-aware vs A-graph | Recall低3.4pp（.96125 vs .99500），不成立 | Recall低2.3pp，且1.83×慢 |
| U-aware vs A-graph | Recall低2.25pp，不成立 | Recall低1.67pp，且1.32×慢 |
| H-aware vs U-aware | Recall低1.13pp且1.08×慢 → **H负增量** | Recall低0.60pp且1.39×慢 → **H负增量** |
| U-cap vs U-off | Recall相同，0.87× → cap有效 | Recall相同，0.73× → cap有效 |
| Exact vs A-graph | **同质量且0.23×**（控制臂） | **同质量且0.28×**（控制臂） |

**A-graph不是历史最强NodeANN。** 协议事前固定主基线，不能事后调换；这里如实报告：纯图版本在WIKI比历史混合慢2.2倍且Recall低0.4pp。任何“H优于NodeANN”的说法都不能用A-graph被放慢来解释，因为H连A-graph也没赢。

### 3.3 0.95/0.99目标下可用的固定点

| 目标 | WIKI 可用点（macro@mean） | SIFT 可用点（macro@mean） |
|---|---|---|
| ≥0.99 | A-graph .995@0.901、A-hybrid .996@0.433、Exact 1.0@0.205 | A-graph 1.0@0.319、A-hybrid 1.0@0.291、Exact 1.0@0.089 |
| ≥0.95 | 上述＋U-aware .9725@0.336、H-aware .96125@0.362、U/H-cap 与 off | 上述＋U-aware .9833@0.421、H-aware .9773@0.585 |

不新增预算追质量；0.95点与0.99点不能互相称呼为同质量。

### 3.4 direct / recursive 分层（ef20）

| 数据集 | 臂 | direct | recursive |
|---|---|---|---|
| WIKI | A-graph | 1.0@0.005ms（240条，含空GT） | .99333@1.198ms |
| WIKI | U-aware | 1.0@0.004ms | .96333@0.446ms |
| WIKI | H-aware | 1.0@0.004ms | .94833@0.481ms |
| WIKI | Exact | 1.0@0.004ms | 1.0@0.271ms |
| SIFT | A-graph | 空GT，0.000ms | 1.0@0.319ms |
| SIFT | H-aware | 空GT，0.001ms | .97733@0.585ms |

差距集中在recursive大scope；direct在小目录上各臂都接近零成本。

## 4. NodeANN速度来源（实际计数）

| 指标 | WIKI A-graph | WIKI A-hybrid | SIFT A-graph | SIFT A-hybrid |
|---|---:|---:|---:|---:|
| 局部HNSW容器数 | **143** | **2** | **14** | **8** |
| 图内活向量 | 11,869 | 5,154 | 12,000 | 9,600 |
| 活对象总数 | 11,869 | 11,869 | 12,000 | 12,000 |
| 每query目录访问 | 33.52 | 33.52 | 6.00 | 6.00 |
| 每query graph_calls | 31.49 | 0.42 | 5.36 | 3.25 |
| 每query扫描组 | 0 | 31.07 | 0 | 2.11 |
| 每query扫描活对象 | 0 | 1,469.9 | 0 | 844.0 |
| 每query图距离 | 2,191.4 | 671.3 | 2,985.0 | 2,306.7 |
| 每query扫描距离 | 0 | 274.1 | 0 | 153.3 |
| 合并候选 | 96.55 | 96.55 | 41.63 | 41.63 |
| 有效ef范围 | 16（min=max） | 16 | 15 | 15 |
| 全部200条Q的均值e2e | 0.721ms | 0.347ms | 0.240ms | 0.219ms |

按scope大小（WIKI A-graph）：scope 1–63 时1.00张图/0.005ms；64–511 时1.47张/0.052ms；≥512 时88.33张/2.017ms。**小目录每个都建图，但每个只含≤64个对象**：116/143个目录的活对象≤64，建图与调图的固定成本远高于对该目录做16–64次标量距离。这是WIKI差别（2.2×）的主因；SIFT只有14个目录、每组≥数百对象，差别仅9%。

两臂的距离分账均满足 `graph+scan==total`（0不一致）。A-graph的 `ann_hops`（WIKI 1769.5、SIFT 2605.5）是图邻居展开计数，与closure的`expanded`不是同一单位，未混比。

## 5. 构建、空间、资格与维护成本

### 5.1 构建与空间（ef20，重复0）

| 数据集 | 臂 | build_s | 结构估计MB | 其中资格MB | 峰值RSS MB | 容器 |
|---|---|---:|---:|---:|---:|---:|
| WIKI | A-graph | 1.535 | 51.37 | 0 | **485.5** | 143 |
| WIKI | A-hybrid | 1.154 | 22.85 | 0 | 85.0 | 2 |
| WIKI | U-off / H-off | 13.85 / 14.46 | 67.13 | 0 | 123.7 / 123.8 | — |
| WIKI | U/H-aware, Exact | 14.17 / 14.17 / 14.16 | 68.09 | 0.964 | 124.5 | — |
| SIFT | A-graph | 0.472 | 8.93 | 0 | 59.7 | 14 |
| SIFT | A-hybrid | 0.414 | 7.34 | 0 | 42.1 | 8 |
| SIFT | U/H-aware, Exact | 2.38 / 2.35 / 2.37 | 27.57 | 0.964 | 39.5 | — |

- A-graph的RSS包含143个局部图各按`max(16, 2×direct活对象)`预留容量（合计24,226槽，而活对象11,869）；这是该基线实现的实际分配策略，不是“纯NodeANN必然占用”。SIFT因只有14个目录，RSS反而低于closure臂。
- 资格结构约0.96MB/数据集（192个位平面＋direct posting＋目录），是逻辑容量；Exact与hybrid都必须支付它。
- closure臂另持有HNSW payload副本与64候选池/见证；`index_bytes`是容量估计，不含借用Dataset与暂态工作区。内存未作为胜出门槛。

### 5.2 维护（36次；A-graph / U-aware / H-aware，ef20）

原始混合前缀（真实事件，200条事件）与合成包络（5,208/5,276条事件）分开：

| 数据集 | 轨迹 | 臂 | 事件 | 更新总耗时 | 压实次数 | 构建s |
|---|---|---|---:|---:|---:|---:|
| WIKI | mixed | A-graph | 200 | 9.6ms | 1 | 1.57 |
| WIKI | mixed | U-aware | 200 | 0.4ms | 0 | 13.87 |
| WIKI | mixed | H-aware | 200 | 0.4ms | 0 | 13.93 |
| WIKI | envelope | A-graph | 5,276 | 1,800.9ms | **285** | 1.55 |
| WIKI | envelope | U-aware | 5,276 | 7,492.4ms | 1 | 14.02 |
| WIKI | envelope | H-aware | 5,276 | 7,604.7ms | 1 | 14.38 |
| SIFT | mixed | A-graph | 200 | 5.5ms | 0 | 0.47 |
| SIFT | mixed | U-aware | 200 | 72.9ms | 0 | 2.36 |
| SIFT | mixed | H-aware | 200 | 69.6ms | 0 | 2.32 |
| SIFT | envelope | A-graph | 5,208 | 788.2ms | **50** | 0.49 |
| SIFT | envelope | U-aware | 5,208 | 1,415.5ms | 1 | 2.40 |
| SIFT | envelope | H-aware | 5,208 | 1,417.5ms | 1 | 2.47 |

逐操作（重复0，单位µs）：

| 数据集/轨迹 | 操作 | A-graph | U/H-aware |
|---|---|---:|---:|
| WIKI envelope | B（整批移动） | 610,212（1次） | 957 / 1,002（1次） |
| WIKI envelope | M（逐对象，1,311次） | 466.5 | 0.7 / 0.7 |
| WIKI envelope | D（3,957次） | 146.3（含231次局部压实） | 1,889.8 / 1,918.2（含1次全量压实，单次最大7.49s/7.59s） |
| WIKI envelope | I（1次） | 27.6 | 12,711 / 12,362 |
| SIFT envelope | B | 197,940 | 969 / 976 |
| SIFT envelope | D（4,000次） | 89.9（含50次局部压实） | 352.9 / 353.5（1次全量压实，最大1.41s） |
| SIFT envelope | I | 76.1 | 1,653 / 1,477 |

实际观测到压实（包络轨迹）：A-graph在WIKI 285次、SIFT 50次（逐局部图）；closure臂各1次（墓碑比达1/3触发的全量重建）。原mixed前缀只有WIKI A-graph发生1次，其余为0。**不是按计划声称，而是从updates.csv的`compaction_count`/`compactions`统计。**

解读：
- 元数据操作（V/T/M）在资格索引上极便宜（0.4–0.7µs），A-graph的M要移除并重插局部图（47.8–466.5µs）。
- 批移动B：A-graph要为每个被移动对象改图，WIKI 610ms；closure只移动token＋逐对象M收费，约1ms，但随后整批M事件也计入总账。
- **closure的债务集中在I（重建自身与邻居候选池，12.7ms）与全量压实（7.5s）**，使WIKI包络总更新耗时是A-graph的4.2倍。这是H/U共同成本，不是H独有。
- 资格子项（`eligibility_update_us`）在B时约112–143µs/次，其余操作≤1µs；A-graph为0（不建资格索引）。
- 该包络只测已触发的事件；不代表稳态、并发、崩溃恢复或无限流债务上界。

## 6. 能支持什么

- **支持**：`E<ef`执行缺口是真实且可修的；资格前置＋`E≤512`精确扫描把该band与总展开降到接近0，并在小E上同时提升Recall；cap单独也能在同Recall下减负。NodeANN的速度主要来自对小目录做谓词后精确扫描而非逐目录建图。
- **不支持**：H的子树闭包规则带来独立收益（同路径上U与H几乎同速，H在`E>512`图路径上Recall更低）；H-aware同质量超越NodeANN；任何新的图索引理论或“首次Trie/bitmap联合索引”主张。
- **不改变**：旧H负结果、旧维护与Dynamic结论。本轮无Dynamic、无大N扩展，`dynamic_admitted`仍为false。

**下一步只在研究端决定。** 若继续，问题已不是“H是否更快”，而是“闭包见证在`E>512`图路径上为何降低Recall且不减少展开”；本轮不自行换算法、不调阈值、不扩池。

## 7. 交付与复现

| 文件 | 内容 |
|---|---|
| [eligibility-static-run1/manifest.json](data/eligibility-static-run1/manifest.json) | 源/二进制/输入/trace/tape SHA、参数、命令、亲和性 |
| [eligibility-static-run1/points.csv](data/eligibility-static-run1/points.csv) | 全部102次×所有分组的质量/时间/距离/展开/资格 |
| [eligibility-static-run1/comparisons.json](data/eligibility-static-run1/comparisons.json) | 7组配对比较（含H/U/A增量与cap消融） |
| [static-summary.json](data/eligibility-static-run1/static-summary.json) / [bands.json](data/eligibility-static-run1/bands.json) | 三次重复聚合；E分层、direct/recursive |
| [nodeann-audit.json](data/eligibility-static-run1/nodeann-audit.json) | 容器/向量/图调用/扫描/合并/有效ef 实测 |
| [semantic-check.json](data/eligibility-static-run1/semantic-check.json) | 独立不变量复核（problems为空） |
| [raw-inventory.json](data/eligibility-static-run1/raw-inventory.json) | 每个raw文件路径/大小/SHA |
| [eligibility-maintenance-run1/](data/eligibility-maintenance-run1/) | 维护逐操作/总量/压实计数 |
| [figures/quality-time.png](data/eligibility-static-run1/figures/quality-time.png) | 上述实测点散点（0.95/0.99参考线） |

完整raw（每query CSV、summary、日志、构建输出）留在 `work/eligibility/pure-nodeann-static-run1/`（11MB）与 `work/eligibility/pure-nodeann-maintenance-run1/`（12MB）。`runs.json`因含逐run artifact哈希达3.6MB，未入库；其SHA见 [raw-inventory.json](data/eligibility-static-run1/raw-inventory.json)。

复现命令见 [eligibility-server-reproduction.md](experiments/eligibility-server-reproduction.md)。
