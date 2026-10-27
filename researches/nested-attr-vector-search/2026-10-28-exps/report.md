# 全量 WIKI：两类基线重跑与计量修正

## 结论

完整 WIKI 上，**节点树 + 每节点局部 HNSW**（默认 threshold=0）在本轮递归 workload 达到 query-average recall **0.999559**、global pair recall **0.999320**、平均 **6.839 ms**。TrieHI + 全局 IVF（nprobe=256）平均 **6.770 ms**，但两种 recall 仅 **0.467083 / 0.547247**，不是同等质量对比。全列表 IVF 达到 recall=1，平均 **25.126 ms**；局部 Flat 精确对照达到 recall=1，平均 **4.305 ms**。

局部 HNSW 的目录枚举均值只有 **93.279 µs**，占其递归端到端约 **1.36%**，本轮主要成本在局部搜索/合并，不能再用旧记录声称“目录枚举是唯一瓶颈”。Flat 对照不替代用户要求的局部 ANN 基线；默认 threshold=0 保持不变。

## 1. 目标与设置

- 目标：用新下载的完整 WIKI 向量重做 A 全局 ANN + 属性 ID 过滤、B 节点树 + 局部 ANN 两类基线；修正此前转换、越界与 NODE 计时问题。
- 输入：**1,941,679×1024**，原始 fvecs 为 **7,960,883,900 bytes**。转 flat f32 后 **7,953,117,184 bytes**；全部浮点位模式逐块核对一致且有限。
- 查询：发布的 **456×1024** 查询向量；227 recursive + 229 non-recursive。目录锚点为 seed=20261026 的深度混合合成约束，**不是发布方真实 query constraint**。
- 目录：468,031 trie 节点，363,467 非空直接绑定节点，与论文的有绑定目录口径一致；最大单节点 **13,255** 条，不能再称所有节点最多几千条。
- 语料路径元数据的全部 id 按行严格为 0..1,941,678，与向量行号对齐。
- 距离：原始浮点上的 squared L2，无额外归一化。语料 L2 范数范围 **0.999631–1.000366**，查询 **0.999683–1.000311**。
- 硬件：Ryzen 7 5800X，WSL2，运行时可用内存约94 GiB；OMP/OpenBLAS/MKL/BLIS均固定1。各配置串行，**每配置1次**，无运行间置信区间。p95 是该次运行的 query 分布分位数。
- GT：每查询 scope 内暴力 top-10；独立于执行器和表示，GT计算及结果审计不计入查询时间。
- 全局 HNSW：FAISS PathSeer fork，M16，efConstruction=200，efSearch=2048（PG近似替代，不是论文 Viking PG）。IVF：nlist=1024，nprobe=256或1024。
- 局部 HNSW：363,467 个非空节点各一个索引，M16、efConstruction=200、efSearch=64；完整枚举候选节点，每节点搜至多10个，再合并top-10。未截断候选节点。

| 查询类型 | 查询数 | 空scope数 | scope中位条目数 | GT pair总数 |
|---|---:|---:|---:|---:|
| recursive | 227 | 0 | 12 | 1,471 |
| non-recursive | 229 | 41 | 2 | 878 |

**指标口径**：macro为非空查询上平均 `hit/min(10,scope_card)`；空scope单列，不放进macro分母。global pair recall为 `sum(hit)/sum(min(10,scope_card))`，本轮总GT pairs=2,349。延迟/QPS包括所有查询（含空scope）；QPS是串行 `1000/mean_ms`，不是并发吞吐。旧CSV的 `recall10=hit/10` 保留为legacy列，不能当作本轮标准recall。

## 2. 递归查询结果

| 类别/方案 | 参数 | macro recall | global pair recall | mean e2e (ms) | p95 (ms) | 串行QPS |
|---|---|---:|---:|---:|---:|---:|
| A PE-Online + HNSW | ef2048 | 0.212384 | 0.308634 | 21.899 | 29.781 | 45.7 |
| A PE-Offline + HNSW | ef2048 | 0.212384 | 0.308634 | 20.091 | 26.065 | 49.8 |
| A TrieHI + HNSW | ef2048 | 0.212384 | 0.308634 | 20.091 | 25.811 | 49.8 |
| A PE-Online + IVF | np256 | 0.467083 | 0.547247 | 8.641 | 25.855 | 115.7 |
| A PE-Offline + IVF | np256 | 0.467083 | 0.547247 | 6.561 | 19.583 | 152.4 |
| A TrieHI + IVF | np256 | 0.467083 | 0.547247 | 6.770 | 20.489 | 147.7 |
| **B 节点局部 HNSW** | **thr0，ef64** | **0.999559** | **0.999320** | **6.839** | **24.534** | **146.2** |
| 局部 Flat精确对照 | thr=2147483647 | 1.000000 | 1.000000 | 4.305 | 15.939 | 232.3 |
| A TrieHI + IVF全列表对照 | np1024 | 1.000000 | 1.000000 | 25.126 | 73.493 | 39.8 |

PE-Online/Offline的全列表IVF结果同为recall=1，平均分别28.010/26.067 ms，完整行见原始汇总。

观察：
- 同一个全局索引与参数下，三种目录表示的hit总数完全一致；不同处是scope解析成本，不是ANN质量。
- 局部HNSW仅漏掉递归GT的1个pair（1,470/1,471）；非递归全部命中。
- 近乎同质量时，局部HNSW比TrieHI+IVF全列表扫描快约3.67倍，但前者仍是近似、后者本轮精确命中；**严格同质量**的局部Flat对照比全列表IVF快约5.84倍。
- Flat在本数据上比局部HNSW快约1.59倍，而不是旧记录的数百倍。不能把旧记录差异归因于某个单一修复：转换、NODE双重解析、查询越界和线程环境均有变化。

## 3. 非递归查询结果

| 类别/方案 | 参数 | macro recall | global pair recall | mean e2e (ms) |
|---|---|---:|---:|---:|
| A PE-Online + HNSW | ef2048 | 0.013623 | 0.018223 | 19.450 |
| A PE-Offline + HNSW | ef2048 | 0.013623 | 0.018223 | 19.539 |
| A TrieHI + HNSW | ef2048 | 0.013623 | 0.018223 | 19.603 |
| A PE-Online + IVF | np256 | 0.155098 | 0.127563 | 2.233 |
| A PE-Offline + IVF | np256 | 0.155098 | 0.127563 | 2.114 |
| A TrieHI + IVF | np256 | 0.155098 | 0.127563 | 2.179 |
| **B 节点局部 HNSW** | **thr0，ef64** | **1.000000** | **1.000000** | **0.005329** |
| 局部Flat精确对照 | thr=2147483647 | 1.000000 | 1.000000 | 0.003458 |
| A TrieHI + IVF全列表对照 | np1024 | 1.000000 | 1.000000 | 8.207 |

该workload的non-recursive scope中位数仅2，含41个空scope；这解释了全局过滤检索面对的极低选择率，也限制了外推。**不能把全局HNSW的低召回归因于目录表示**，三种表示的质量相同。本轮没有扫描到同recall的HNSW工作点，不能宣称其调优极限。

## 4. 目录解析、构建与内存

递归scope解析均值（同all3一次运行）：

| 方案 | 配合全局HNSW时 dir (µs) | 配合全局IVF时 dir (µs) |
|---|---:|---:|
| PE-Online | 1,787.049 | 1,749.764 |
| PE-Offline | 7.824 | 5.295 |
| TrieHI | 8.343 | 6.155 |
| 局部HNSW候选节点枚举 | 93.279 | 不适用 |

NODE递归6.839 ms中搜索/合并为6.746 ms；局部Flat的4.305 ms中枚举0.093 ms、搜索/合并4.211 ms。此前NODE“目录开销随N线性主导”的数字混入了全局scope解析，不适用于修复后的两类基线。

| 组件 | 构建秒数 | 大小口径 |
|---|---:|---|
| 全局HNSW | 4,961.96 | 序列化索引8,233.26 MB |
| 全局IVF（all3） | 84.266 | 序列化索引7,972.85 MB |
| 363,467个局部HNSW | 76.304 | 仅向量payload下界7,953.12 MB；完整索引大小未测 |
| 363,467个局部Flat | 5.994 | 仅向量payload下界7,953.12 MB；完整索引大小未测 |

all3整进程峰值RSS为35.492 GiB，局部Flat独立运行17.111 GiB，IVF全列表独立运行17.042 GiB。all3同时持有base、全局HNSW、IVF、局部HNSW等对象，**不能用该RSS充当局部HNSW单索引内存**。目录模块CSV为当前实现估算值，亦非进程RSS；构建计时边界并非所有组件完全相同，不能按这些秒数作严格系统级构建成本排名。

## 5. 两类基线与MOVE语义

1. **类A**：全局ANN只建一份；PE-Online、PE-Offline、TrieHI解析同一个scope ID集合，再经FAISS selector限制结果资格。selector不是属性谓词引擎，也不能据此声称不合格HNSW节点一定不参与遍历。
2. **类B**：节点树上维护直接绑定对象的局部ANN；递归枚举子树非空节点，各搜top-k后合并。索引不覆盖整棵子树，不为每个祖先重复建ANN。
3. **PE-Online**以完整路径字符串编码目录身份，需要枚举移动子树路径键并重映射。**PE-Offline同样需要这项工作**，还需把子树成员从旧祖先集合移除、加入新祖先集合。`O(m_u)+O(t)`中的t仅是集合操作次数，实际还取决于位图规模与字符串长度。
4. **TrieHI**不需为所有后代重写路径键，但需维护新旧祖先聚合ID集合。局部ANN随节点保留；本地公共`Tree::relink`还遍历后代更新depth，因此不能声称本实现整次MOVE为O(1)。参见[论文MOVE说明，第5页](https://www.alphaxiv.org/abs/2606.16903?page=5)。
5. 本轮只重跑DSQ，没有重新测MOVE/MERGE。当前prototype以数值node id索引posting，并维护合成segment路径字符串来模拟路径重写；不是生产级完整字符串键字典实现，PE-Online/Offline的查询路径查找成本不与论文实现逐项等价。旧DSM绝对值保留但不作为本轮新增证据。

## 6. 修复、验证与证据

- 修复`fvecs_to_f32.py`把int位模式数值转换成float的错误。已确认旧`data/real/test.f32`恰好等于该错误转换结果；**至少这份旧查询fixture确实受影响**。未逐一核查的旧实验不作污染范围推断。
- 修复旧457条约束对456条向量的越界：保留所有有效query id=0..455，删除非法合成行query id=456；生成脚本累计舍入保证恰好456。旧文件不覆盖。
- NODE不再先构造全局scope位图；GT每namespace一次准备；per-query增加可审计计数与独立search计时。
- CLI smoke覆盖全HNSW/Flat、scope>k、scope<k、空scope、越界拒绝；转换回归测试实跑1项通过。
- 全量3个进程exit code均0；日志无`RESULT_SCOPE_WARNING`；汇总验证全部执行器GT metadata一致，三种目录表示固定参数hit一致，局部Flat与IVF全列表均命中全部2,349个GT pairs。

证据均留在上一日启动的实验目录，不搬迁、不覆盖：
- [原始输入校验与SHA-256](../2026-10-27-exps/data/wiki_full_inputs.json)
- [ID顺序与旧转换影响核查](../2026-10-27-exps/data/wiki_full_alignment_check.json)
- [逐配置/查询类型汇总CSV](../2026-10-27-exps/data/wiki_full_summary.csv)
- [全局及局部HNSW逐查询CSV](../2026-10-27-exps/data/wiki_full_all3_dsq_perquery.csv)
- [Flat逐查询CSV](../2026-10-27-exps/data/wiki_full_node_flat_dsq_perquery.csv)
- [IVF全列表逐查询CSV](../2026-10-27-exps/data/wiki_full_ivf_exact_dsq_perquery.csv)
- [完整协议、命令与烟测说明](../2026-10-27-exps/experiments/reproduction.md)
- [运行脚本](../2026-10-27-exps/experiments/run_wiki_full.py)；[汇总脚本](../2026-10-27-exps/experiments/summarize_wiki_full.py)。同目录`.log/.time.txt/.command.json`保存退出码、时间、命令及RSS。

## 7. 局限与下一步

本轮是原始向量/真实树/合成约束下的固定参数单次运行，不是论文查询约束复刻，也不是全选择率曲线。少量scope、empty scope及极低选择率明显影响指标；已分开macro/global并计空查询。多绑定/DAG不在WIKI验证范围内，NODE对重复全局ID及非递归多绑定仍有限制。局部HNSW单独RSS、统一索引存储尺寸、重复运行方差及同召回超参数网格仍未测。

下一步应优先补同召回参数扫描与按scope/候选节点数量分桶，验证局部索引优势是否延续到大scope，再考虑共享搜索/减少局部调用；本轮没有运行新优化，也没有证据声称新颖性。

## 8. 后续新增轮次：autoresearch harness setup（未优化）

为新索引/算法研究建立固定入口`bash autoresearch.sh`，本轮仅搭建评测，没有实施候选算法。沿用完整WIKI与相同456查询，默认局部全HNSW；在3个独立进程分别构建索引并跑同一workload。下面是该轮新增实测，不覆盖前述单次两类基线。

| 指标 | 实测值 | 汇总口径 |
|---|---:|---|
| 主指标recursive e2e | 6.638942 ms | 3次query-mean的中位数 |
| recursive p95 | 23.720180 ms | 3次p95的中位数 |
| recursive macro recall | 0.999559 | 3次最小值 |
| recursive global pair recall | 0.999320 | 3次最小值 |
| non-recursive e2e | 0.005186 ms | 3次query-mean的中位数 |
| non-recursive recall | 1.000000 | 3次最小值 |
| index build | 69.841400 s | 3次中位数，沿用executor构建计时 |
| peak RSS | 19,742.082031 MiB | 3个被测进程最大值 |
| recursive QPS | 150.626415 | 1000/主指标 |
| recursive e2e MAD | 0.025667 ms | 3次query-mean的绝对中位差 |

三次recursive mean分别为6.664609、6.594552、6.638942 ms。完整`bash autoresearch.sh`退出0，按约定输出主/副`METRIC`行。六项回归测试通过；非法参数子进程exit=1且不输出METRIC，实际返回ID的重复、越scope、虚报质量及缺失query行均被质量检查拒绝。

本轮新增独立NumPy GT，枚举真实scope并扫描原始float32距离，保存2349个GT pair对应的ID；C++只追加计时外的`result_ids`输出，不改变搜索算法。评估器基于实际返回ID核查质量，而不是只信自报hit。输入SHA-256固定，recursive macro/global recall均需≥0.999，non-recursive recall需为1，non-recursive mean≤0.025ms，单进程RSS≤24GiB、index build≤180s。每次重复均需过门槛。

冻结合同与局限见[基准协议](experiments/autoresearch-harness.md)，观测汇总见[baseline JSON](data/autoresearch_harness_baseline.json)，独立GT见[reference JSON](data/autoresearch_reference.json)。原始日志与逐查询CSV在本地`production/nested-attr-vector-search/work/autoresearch_trie/run-6i9heh4v/`，未提交大数据/索引。harness固定输入但墙钟存在噪声，三次重复不构成跨硬件置信保证。

下一步是初始化会话后再提出、查重并实验新机制；不能仅把精确局部top-k合并、质心排序、换Flat或改名写作创新。TrieHI的公开预印本仍须引用；本轮没有完成新的文献查重或新算法验证。

## 9. Autoresearch 静态DSQ迭代

正式baseline与harness setup的观测分开登记；每个run均使用冻结评估器与3个独立进程。这里的性能改进不等于新颖性通过。

| run | 机制 | recursive mean中位数(ms) | macro/global recall | 决定 |
|---|---|---:|---|---|
| 1 | 原始局部HNSW正式baseline | 6.649231 | 0.999559/0.999320 | baseline |
| 2 | 每query复用局部top-N结果缓冲，预留全局top-k heap容量 | 6.555309 | 0.999559/0.999320 | 保留工程对照 |
| 3 | DFS scope-run单份连续布局 + 单次全局top-k精确扫描 | 2.142991 | 1.000000/1.000000 | 保留强工程对照 |
| 4 | 四向量交错scalar累加，共享query载入 | 2.606194 | 1.000000/1.000000 | 丢弃，较run3慢21.61% |
| 5 | chunk64部分距离提前终止，再完整L2 refinement | 2.807973 | 1.000000/1.000000 | 丢弃，较run3慢31.03% |
| 6 | DFS scope-run + tile8转置 + 跨向量AVX2 | 1.856979 | 1.000000/1.000000 | 保留布局/内核对照 |
| 7 | tile8共享global cutoff，每64维检查所有合法lane后共同提前终止 | 1.739233 | 1.000000/1.000000 | 保留探索性机制 |
| 8 | 截止检查改为每256维 | 1.815224 | 1.000000/1.000000 | 丢弃，延迟提前终止 |
| 9 | 截止检查改为每16维 | 1.740295 | 1.000000/1.000000 | 丢弃，无可靠收益 |
| 10 | 子树质心前8维符号编码，重排兄弟子树 | 1.798043 | 1.000000/1.000000 | 丢弃，较run7慢3.38% |
| 11 | 直接绑定内部按8维符号编组，顺序物化packed payload | 1.127503 | 1.000000/1.000000 | 保留，需分离物化顺序消融 |
| 12 | 保留顺序物化，但关闭局部几何编组 | 1.158137 | 1.000000/1.000000 | 消融后恢复run11；编组净差约2.65% |

run2相对baseline降低约1.41%，不改变索引或召回；是消除逐节点临时分配的工程对照，**不宣称算法创新**。non-recursive mean=0.005055ms，build=69.0099s，peak RSS=19,742.234MiB。尚未测试动态维护或跨数据集泛化。

run3保留`--node-algorithm hnsw`原局部HNSW/Flat路径，新增静态single-binding `ScopeRunIndex`。每绑定仅一份packed向量；recursive/local区间分别覆盖子树/直接绑定，内部节点直接绑定不丢失。查询的range获取在原directory计时内，扫描及合并在原search计时内，不移动计时边界。默认DSQ候选为scope-run；历史`dsq_scan`默认仍使用原局部索引，显式scope-run扫描参数不适用时拒绝。CLI实跑mode0/1/2在scope>k、scope<k、子树及空节点均命中10/2/7/0，旧HNSW可运行，多绑定重复global ID被拒绝。

run3相对正式baseline降低约67.77%，build=5.29815s、peakRSS=16,031.953MiB，non-recursive mean=0.002883ms；全部2349个独立GT pairs命中。尚无新颖性认证：DFS-to-range是标准归约；[iRangeGraph §3](https://arxiv.org/abs/2409.02571)已有范围共享图，[Curator §4.2](https://arxiv.org/abs/2601.01291)已有子树连续ID及临时谓词索引。这里先把bulk exact scan作为强对照，不能把布局改造直接称为新算法。已核本地正文`2409.02571-irangegraph/paper.md:407-520`与`2601.01291-curator/paper.md:450-500,650-715`；经典VA-file正文读取仍未完成。搜索覆盖了vector similarity join/inner-product join的相邻分区机制后再查filtered ANN；未找到等价组合不作为新颖性证据。

run6每8个物理相邻向量转置成dimension-major tile，一次query广播服务8条距离累加；区间边界tile可以多算距离，但只允许scope内ID进top-k。转置在索引build计时内，采用单tile scratch原地重排，不增加第二份持久向量。相比run3再降13.35%，相比baseline约降72.07%；build=6.19698s，peakRSS=16,032MiB，non-recursive mean=0.003906ms（较row布局回退，但仍过门槛）。独立GT全部命中；额外CLI独立Python排序验证非对齐local/subtree边界、最后partial tile及空scope。其SIMD与AoSoA布局仍按工程对照记录，不据此宣称学术创新。

run7将原run5逐向量“部分距离后再完整refine”改为tile内SIMD累加直接延续，仅当8条合法lane均已超过同一global top-k cutoff才跳过余维；非scope lane显式屏蔽。相比run6降低6.34%，相比run3降低18.84%，而逐点run5反而变慢，形成一次机制消融。全部2349个独立GT pairs命中，build=5.92129s、peakRSS=16,032.035MiB。浮点margin是工程保守容差，不据此宣称形式化float等价保证。共享截止反馈与partial-distance本身都有前序，该tile执行组合尚未完成最近前序的完整逐步骤排查，**新颖性未确认**。

run8/9是截止反馈检查频率消融，均未比64维取得可靠收益。run10保持每个子树区间但按子树聚合几何sign code重排兄弟，查询无效开销反而增加，已回退。

run11仅在各节点的直接绑定内部按前8维符号code分组、以global ID打破同code平局，不改变拓扑或scope；所有统计仅用base向量，不使用query/GT。构建同时由“按输入binding随机写packed”改为“先填全局ID映射、再顺序写packed”。两项变化耦合，所以1.127503ms不能全归因于几何编组：需以相同顺序物化、关闭几何排序的消融分离。该run相比run7低35.17%，build=5.35924s、peakRSS=16,031.922MiB、non-recursive mean=0.003153ms，独立召回全部为1。排序/空间编组本身仍有大量经典前序，新颖性未确认。

run12给出关键反证：关闭局部几何排序、保留顺序物化后仍为1.158137ms，所以不能把run11相对run7的35.17%全部算作几何编组收益。两者控制比较的净差只有2.65%，尚需独立重复与不同硬件确认；其余差异与构建物化/缓存及编译效应的具体因果尚未剥离。最佳保留run11，完成配置上限12/12，无修改评估器、输入hash、质量门槛或计时边界。

### 9.1 本轮结论及研究级别

- 保留实现：`src/scope_run_index.h`的静态single-binding范围索引；每节点直接绑定内部几何编组、DFS interval、单份tile8 AoSoA、global top-k cutoff反馈。构建只读取base/目录/bindings，不读取query或GT。
- 相对正式局部HNSW baseline快**5.897倍**（6.649231→1.127503ms），观测macro/global recall由0.999559/0.999320到1/1，构建70.8704→5.35924s，peakRSS19,742.180→16,031.922MiB。不能把全部性能差距归因于某个新机制：大量收益来自批量扫描与物理布局，均有成熟前序。
- 最强机制证据是tile共享截止反馈相对于tile exhaustive的6.34%改进，以及逐点提前终止反而变慢的消融。局部几何编组的独立收益小，不能夸大。
- **当前级别是有收益的候选索引/执行机制与工程强基线，不是已通过新颖性审查的新算法。** 已知前序包括Curator的连续子树ID、iRangeGraph的范围组织、Packing的块组织、VA-file/partial-distance与通用SIMD布局；本文未完成所有最近前序正文与引用闭包。
- 明确退化：single-binding/static，不支持DAG多标签；没有MOVE/MERGE实现。DFS物理布局在目录移动后可能要重排向量与更新区间，**不继承TrieHI廉价重挂的维护保证**。新提出的“稳定extent/tile所有权与范围cover解耦”只能作为下轮待查重问题，不能把常规间接层/rope改名当创新。
- 完整WIKI但仍只有456条合成约束query；12轮均观察同一workload，存在选择偏差，未运行独立holdout或第二数据集。没有GPU/多线程/动态更新结果。mode4浮点margin未作形式化证明，观测召回1不等于所有输入下精确保证；mode0/3是不提前终止的穷尽对照。

### 9.2 证据与复现

全部12轮[数值CSV](data/autoresearch_iterations.csv)/[JSON](data/autoresearch_iterations.json)，[最佳run完整汇总](data/autoresearch_best_summary.json)，最佳3次[逐查询0](data/autoresearch_best_repeat_0.csv)/[1](data/autoresearch_best_repeat_1.csv)/[2](data/autoresearch_best_repeat_2.csv)。`experiments/autoresearch_run_01.txt`到`autoresearch_run_12.txt`保留实际benchmark stdout与本地原始产物目录。没有把推测补成实验结果。

统一入口仍为`bash autoresearch.sh`，评估器冻结；最终默认候选为scope-run/mode4/local geometry。直接CLI可用`--node-algorithm hnsw`复现旧局部HNSW/Flat，`--node-algorithm scope-run --scope-mode 0`复现row exhaustive，mode3为tile exhaustive；`--scope-local-order original`关闭局部几何编组。`dsq_scan`仍默认旧索引，新增scope-run不伪装支持nodeTopN/limit语义。

结束前构建最终保留源码并实际CLI验证mode0..4：独立Python距离排序核查scope>k、scope<k、内部节点绑定、非对齐区间、partial tile及空scope；另用35条128维、强远近分离数据覆盖tile cutoff及尾tile，返回ID完全符合独立期望。临时fixture已移除。大数据/索引不入库。

交付时为保持旧caller的复现语义，`--executor all3`默认仍选择原局部HNSW，单独`--executor node`默认选择候选；未改冻结harness的node选择。修改后再次构建，六项现有Python回归通过；1,200×4d真实CLI烟测确认all3输出`NODE-hnsw`、node输出`NODE-scope-run`，各四查询，候选命中该小fixture全部GT。该变更仅影响非基准all3的默认选择，不是第13轮性能优化。
