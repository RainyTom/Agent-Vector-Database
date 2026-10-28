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

证据按上一日启动的实验日期保留；目录整体归入 `01-directory-aware-acceleration/`，不覆盖原始内容：
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

## 10. 用户新问题：递归查询能否只搜索有希望的Node？

### 10.1 创新边界与算法语义

用户明确要求：**SIMD、物理布局均属于工程优化，不算创新贡献。** 第9节保留为性能强基线与工程证据；后续不能以这些改造独立立项。质心排序、球下界、best-first分支定界也都是已有方法，以下只做诊断，不认定创新。

对候选Node的直接绑定集合$D_v$，保存质心$c_v$和覆盖半径$r_v=\max_{x\in D_v}\|x-c_v\|_2$。实数算术下

$$L_v(q)=\max(0,\|q-c_v\|_2-r_v)\le\min_{x\in D_v}\|q-x\|_2.$$

从已经找到的至少k个不同合法对象得到当前第k距离$\tau$。只有$L_v(q)>\tau$时才能安全排除整个Node；相等时还需处理tie，不能直接剪。按$L_v$的best-first顺序访问并收紧$\tau$，直至最小剩余下界也超过$\tau$。这不是离线预知top-k，而是查询时逐步证实“不需要再搜哪些Node”。

ANN返回的k个合法候选也能形成真实第k距离的**上界**，但ANN访问frontier或返回最小距离不是未访问对象的安全下界。节点剪枝正确也不代表节点内部ANN没有漏结果；要保证全局精确top-k，局部搜索也需精确或独立验证。仅选质心最近的B个Node属于近似routing，不能保证召回。

### 10.2 实际诊断设置

完整WIKI原始向量、既有树/bindings、冻结独立GT与227条recursive查询，输入hash不变。本轮**未修改候选索引、评估器或基准**；不是第13轮autoresearch优化。节点统计仅使用corpus构建，query和GT只在评估阶段使用。

- 对全部363,467非空Node建立float64质心与覆盖球，Python构建统计耗时42.227s；质心payload 2,977,521,664 bytes，半径2,907,736 bytes。未测压缩摘要或生产C++构建时间。
- 质心排序预算B=1/2/4/8/16/32/64/128/256/512/1024/4096/16384；统计选中Node能够覆盖的GT pair。**这是节点routing能达到的召回上限，不是内部ANN实测recall。**
- 另实际执行球下界best-first，逐个Node做精确局部距离扫描，在线维护global top-k与$\tau$；GT不参与选Node和停止。float64距离处理原float32向量，半径向上nextafter并增加1e-10 margin，不构成通用IEEE浮点认证。
- 单独报告候选Node≥16的86条查询，避免大量只有1–3个候选Node的简单查询掩盖问题。该组候选节点中位数1,010，最大138,993；真实top-10的所属Node数中位数**9**，没有一条top-10全落在同一个Node。

### 10.3 观测结果

| 按质心距离只选最近B个Node（86条非平凡递归查询） | GT pair覆盖率 | 全部top-10均覆盖的query比例 |
|---|---:|---:|
| 1 | 10.12% | 0.00% |
| 4 | 25.00% | 0.00% |
| 16 | 48.37% | 9.30% |
| 64 | 66.51% | 23.26% |
| 256 | 86.05% | 46.51% |
| 1024 | 97.67% | 89.53% |
| 4096 | 99.88% | 98.84% |
| 16384 | 100.00% | 100.00% |

该组每个query均有10个GT，因此macro/global覆盖率相等；全部227条的macro与global不同，完整值见CSV。固定B=16384并非高效策略的证明：它累计访问66.74%的候选Node实例、78.45%的向量。

球下界best-first在227条及86条组内均达到观测macro/global recall=1。86条组累计裁掉**48.15%的候选Node访问实例**，但仅裁掉**9.45%的向量扫描量**；全部227条分别为48.12%与9.44%。这些是跨query累加口径，不能解释为唯一Node比例或延迟降幅。真实第k距离的oracle阈值诊断给出的可剪Node数与该best-first执行一致；oracle只用于上限对照，不用于实际选Node。

解释：可以跳过很多小Node，但大Node的覆盖球松，仍需访问大多数向量。还需要测摘要查询成本、局部ANN调用成本与端到端净收益，不能从“少访问一半Node”直接推出“延迟降一半”。

### 10.4 新颖性初筛：已有直接前序

[Quake: Adaptive Indexing for Vector Search，OSDI 2025](https://www.usenix.org/system/files/osdi25-mohoney.pdf#page=8)，§5、Eq.7–9、Algorithm1（PDF含封面页8–9）已做：估计分区含近邻的概率、优先搜索高概率分区、以当前第k距离在线更新估计，并在估计recall达到目标后停止。其模型包含**uniform-density假设**及Voronoi半空间近似；这是recall估计机制，不是本轮确认的任意数据精确保证。本轮只核相关正文，完整理论/实验及最近引用链待精读，不新增假造的完整论文记录。

因此“每Node预测概率+自适应搜索多少Node”的宽泛想法已经有强前序，不能直接作为新贡献。与本任务的具体差异是：目录Node按语义/拓扑分组，不是由最近质心定义的Voronoi单元，直接套用Quake几何概率模型的假设未成立。**这只是待检验差异，不等于新颖性通过。**

更窄的待研究问题：在不可重新聚类的目录Node内，多模态向量分布下如何获得比单球更紧、比全维质心更便宜的节点级证据，并分配跨Node和节点内ANN搜索预算，在明确误差定义下控制global pair recall？多球/混合模型、学习router、federated shard selection等仍需逐一排查；不能把普通统计模型组合直接改名创新。原D3停止决定不变，本轮不将经典分支定界重新立项。

### 10.5 复现与范围

运行：`production/nested-attr-vector-search/work/venv/bin/python researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/experiments/node_routing_diagnostic.py`。本轮已实际运行成功，未创建永久单元测试（研究诊断，不改生产检索路径）。

证据：[逐query诊断](data/node_routing_queries.csv)、[预算覆盖率](data/node_routing_centroid_budgets.csv)、[球界/访问量汇总](data/node_routing_summary.json)。未测端到端ANN加速、独立holdout、第二数据集、多绑定/DAG或动态维护。下一步只有在更便宜、更紧的节点决策机制与Quake等前序形成实质区别后，才进入新算法实现与同recall比较。

## 11. 审稿人视角：树-图协同、双空间剪枝与图嫁接的可检验性

用户提出以"TrieHI只做元数据层"为突破口，设计目录拓扑与向量图协同的新索引，含三点：层级图对齐、双空间包络剪枝、图嫁接维护。本节按审稿标准核对表述，并用现有全量证据检验前提。**没有实现新图索引**；以下是已跑数据与公开前序，不是对新方案的背书。

### 11.1 三点主张当前不可直接成立

| 主张 | 主要问题 |
|---|---|
| "给定选择比下有界连通度/单调路由收敛" | 对**任意**子树约束，无条件连通度上界不成立：可构造子树内点与图邻居完全分离的数据。要么条件化（密度/分布假设并给出反例边界），要么按子树加边——后者是StitchedVamana类已发表机制。单调收敛在一般图同样只有概率性结论。 |
| "双空间包络+双重三角不等式剪枝" | 球/盒界与metric-tree、VA-file、CertEx投影下界都是既有技术；d=1024时球界很松（见11.3），收益需实测而非默认。 |
| "O(1)结构重构/图嫁接" | 成本是O(boundary)而boundary可能随子树线性增长；且必须先证明"不修图"会造成质量损失，否则嫁接没有靶子。TrieHI的MOVE已是亚毫秒级（2.75ms/op实测），元数据侧并无数量级空间。 |

### 11.2 实测：过滤图的失败形态与精确扫描的支配区

全量WIKI、ef=2048、nprobe=256。PG/IVF 的681行来自**同一次 all3 运行的三种目录表示，各227条递归查询**，不是3次独立重复；下表记录数也不是唯一查询数。scope-run 的独立3次来源见§9。分桶 recall 使用标准 `hit/min(10,scope_card)`，与新增[存量结果审计](data/baseline-completion-audit-2026-10-28.json)一致。按scope大小与锚点深度分组：

| scope条目 | 记录数（3种表示） | PG recall | PG e2e | IVF recall | IVF e2e | 精确scope-run e2e |
|---|---:|---:|---:|---:|---:|---:|
| 1–9 | 330 | 0.035 | 19.68 ms | 0.304 | 2.06 ms | **0.003 ms** |
| 10–99 | 111 | 0.041 | 20.13 | 0.349 | 3.01 | **0.008** |
| 100–999 | 27 | 0.033 | 21.74 | 0.422 | 4.98 | **0.050** |
| 1k–10k | 114 | 0.463 | 20.78 | 0.737 | 11.90 | **0.563** |
| ≥10k | 99 | 0.758 | 24.32 | 0.845 | 25.08 | **7.064** |

按锚点深度（recursive）：

| 深度 | 记录数（3种表示） | scope中位 | PG recall | PG e2e | IVF recall | IVF e2e |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 186 | 9,816 | 0.629 | 22.40 | 0.810 | 18.75 |
| 2 | 102 | 30 | 0.129 | 20.88 | 0.380 | 5.28 |
| 4 | 150 | 4 | 0.032 | 20.46 | 0.336 | 2.79 |
| 9 | 243 | 2 | 0.040 | 19.45 | 0.322 | 2.24 |

**两点修正必须先说清**：
1. 论文"更深约束→PG延迟上升"在本复现中表现为**召回塌陷而非延迟上升**：ef固定时PG耗时基本持平（19.5–22.4 ms），但越深召回从0.629掉到0.032。要恢复召回必须放大ef，那才会推高延迟。把机制写成"延迟随深度上升"与我们的数据不符。
2. 分类讨论的**难点区其实不在小scope**：scope<1000占69%查询但只占0.4%的递归总时间；≥1000的31%查询占**99.6%**时间。小scope下精确扫描只要3–50µs，任何近似图在这里都没有存在意义。

由此，图结构唯一可能有意义的区间是**大scope尾部**：中位16,755条目、精确扫描7.06ms、带宽受限。若某结构能以≥0.99召回把这一段压到1–2ms，平均延迟才可能从1.13ms继续下降。这是可辩护的目标，也是与TrieHI对比的正确靶点。

### 11.3 实测：语义目录Node在向量空间几乎不可分

对363,467个非空Node抽样（每规模档18个，seed 20261028），构建期统计，不用query/GT：

| Node规模 | 样本 | R1（均值球半径） | R1/全局平均对距 | 同Node平均对距/同规模随机子集 | 4球最大半径/R1 | 8球最大半径/R1 |
|---|---:|---:|---:|---:|---:|---:|
| 50–200 | 18 | 0.819 | 0.671 | **0.797** | 0.981 | 0.958 |
| 200–2k | 18 | 0.894 | 0.733 | **0.841** | 0.984 | 0.966 |
| 2k–20k | 15 | 0.932 | 0.764 | **0.899** | 0.987 | 0.983 |

（全局平均对距1.2207，单位归一化向量，d=1024。）

含意：
- 目录Node**确实比随机子集紧一些**（0.80–0.90），但仍有随机集合80–90%的分散度；随Node变大迅速趋近1（2k–20k档0.899）。
- **多球几乎不收缩半径**（4球仅降1.3–1.9%，8球降1.7–4.2%），说明Node内点云接近各向同性，k-means无法切出紧凑球。这与11.2的实测一致：球下界best-first只裁掉**9.45%**的向量扫描量，虽然裁掉了48%的Node访问实例。
- 因此"树-图同构"若以**几何可分离性**为前提，在WIKI 1024维上缺乏支撑；更不能用"深层目录=更细几何尺度"做无验证的层对齐。HNSW上层是为对数跳数服务，绑定到语义深度还可能破坏跳数性质。

### 11.4 前序映射：三点里哪些已被覆盖

| 用户设想 | 已发表直接前序 | 剩余可辩护差异 |
|---|---|---|
| 树+图联合组织（层级对齐图） | [Grant](https://arxiv.org/abs/2610.11563)（属性平衡树+叶子图+概率节点验证，条件性次线性界）；[Graph Range Filters](https://arxiv.org/abs/2607.00727)（过滤图上的distance-aware labeling）；[iRangeGraph](https://arxiv.org/abs/2409.02571)（segment-tree共享elemental graphs）；[Curator](https://arxiv.org/abs/2601.01291)（层级分区+临时索引） | 语义目录树≠数值区间；但"树+图"组合本身不再是新贡献 |
| 修复图连通/路由退化 | [ACORN](https://arxiv.org/abs/2403.04871)（谓词子图遍历+邻域扩展，概率性可导航分析）；[Filtered-DiskANN](https://doi.org/10.1145/3543507.3583552)（StitchedVamana按标签拼子图、FilteredRobustPrune覆盖感知剪枝） | 无条件的"有界连通度"对任意子树不可能；条件化定理需给反例边界 |
| 双空间自适应剪枝 | metric-tree/VA-file经典界；[Quake APS](https://www.usenix.org/system/files/osdi25-mohoney.pdf)（§5，Voronoi半空间+uniform-density概率）；CertEx投影下界 | d=1024下球界实测很松（11.3），需换机制或限定维度 |
| 每查询选哪些Node/预算 | Quake APS；[ELI](https://www.vldb.org/pvldb/vol19/p617-yang.pdf)（按选择比弹性选子索引，弹性因子）；Curator预测子树 | 机制层已被覆盖，需在目录语义上有实质增量 |
| 图随MOVE/MERGE演化 | Filtered-DiskANN/ACORN/iRangeGraph/ELI/Curator均假设静态过滤；TrieHI只做metadata | **这是真正的空白**，但需先证明"不修图"会掉质量 |

### 11.5 建议：先做三个"杀实验"，再决定是否投入图设计

1. **MOVE是否真的破坏检索质量？** 在真实HNSW图上，移动子树后不修图，对锚定该子树的递归查询测recall变化，并统计跨子树边界边占比（boundary/|E|）。若质量几乎不掉，图嫁接就没有必要；若边界占比接近全图，O(boundary)也只是把全图重连改个名字。这是"创新点3"的生死线，成本约一次建图+若干样本子树统计。
2. **大scope尾部是否有图的机会？** 在scope≥1k的71条查询上，用子树内局部图（或Grant/iRangeGraph式结构）对比精确扫描的7.06ms，目标≥0.99召回。若做不到，图路线在整个workload上都被精确扫描支配。
3. **语义目录是否在别的数据集上可分？** 用同一cohesion协议测第二个数据集（如产品目录/代码库/arXiv双命名空间）；若cohesion仍≈0.9，就不要把几何包络写进主张。

通过1与2后，可辩护的贡献轮廓是：**面向层级语义scope的执行器选择与一致性维护**——小scope精确扫描（成本∝|S|·d，可证精确）、大scope专用图结构（须实测连通与召回）、MOVE后图-目录一致性协议（须证明质量不回退）。这仍不是"新定理+新索引"的顶会长文，但它是可验证、可复现、有明确空白（维护）的问题；反之，先写"有界连通度定理"再找数据，风险是审稿人用反例和Grant/ACORN一票否决。

### 11.6 本轮证据与局限

- 数据来源：[scope/深度分桶分析](data/autoresearch_iterations.csv)上游逐查询CSV为`2026-10-27-exps/data/wiki_full_all3_dsq_perquery.csv`与`data/autoresearch_best_repeat_*.csv`；网络图新增[节点几何探测](data/node_geometry_probe.json)与其[脚本](experiments/node_geometry_probe.py)，[节点路由诊断](data/node_routing_summary.json)与[脚本](experiments/node_routing_diagnostic.py)。
- 几何探测为抽样（51个Node）与float64近似，非全量；diam首轮因对角线inf产生Infinity已修正后重跑，报告只用修正值。
- 未实现任何新图索引，未测MOVE后图质量，未测第二数据集，未与Grant/iRangeGraph/ACORN/ELI/Quake做同设置对比。
- 结论仅针对本workload与本嵌入模型（bge-m3 1024d，合成约束锚点）。

## 12. HLG：层级血统连通图（单索引 + 可证连通 + 有界维护）

理论与证明：[hlg-theory.md](hlg-theory.md)（G0 门槛载体）。工具：`codes/directory-aware-vector-attr-search/src/hlg_tool.cpp`（子命令 `index/build/check/bench/move`）。构造汇总：[data/hlg_build_summary.json](data/hlg_build_summary.json)。

### 12.1 已测结论（只写实测）

1. **连接边构造在全量 WIKI 上实测 $|E_{\text{conn}}| = n-1 = 1{,}941{,}678$**，与定理 1(ii) 的等式一致（不是不等式）；建边墙钟 **90.8 s**（12 线程；其中精确 Prim 76.9 s、质心 2.9 s、代表点 1.2 s、数据加载 9.7 s）。相对 M=16 的粗略无向边预算（$nM\approx31.07$M）占 **6.25%**。
2. **维护（G3）通过**：dsm_wiki_ops 的 1064 个 MOVE 实测，≥10k 子树中位 **20.7 ms**、<1k 中位 **24.0 ms**，比值 **0.86**（门槛 ≤3）；$\log_{10}\text{move\_us}$ 与 $\log_{10}|S(u)|$ 相关 **−0.08**，与 $\log_{10}\sum_{a\in\mathcal A}k_a^2$ 相关 **0.96**。即耗时确实由两条根路径的簇数决定，与被移动子树规模无关（定理 2 的定性预测得到数据支持）。
3. **连通性（G1）与检索（G2）尚未测**，缺一份 1.94M×1024 的 HNSW 图（前 10 万点串行建图实测 1.55 ms/点，外推约 50 min；索引只在最后落盘）。命令已交出（[§12.6](#126-未测项与待执行命令)）。**因此本轮不给出“连接边恢复召回/连通”的任何结论**——已测事实里没有这一条。

### 12.2 方法与对计划的两处硬修正

定义、构造、证明见 [hlg-theory.md](hlg-theory.md)。摘要：定理 1（对任意目录 $u$，$G_0[S(u)]\cup E_{\text{conn}}$ 连通；$|E_{\text{conn}}|=n-1$；给出度数上界）、定理 2（MOVE 只影响 $\mathcal A\subseteq\{p,w\}\cup\mathrm{Anc}(p)\cup\mathrm{Anc}(w)$ 的 MST，代价与 $|S(u)|$ 无关）、命题 3（连通不蕴含跳数/召回，含反例）、命题 4（scope 常驻入口 + 连通 ⇒ 非空 scope 不返回零结果）。

计划里的三处必须修正之处（本轮的主要产出之一，均已写进理论笔记并落到实现）：

| 计划原文 | 问题 | 修正 |
|---|---|---|
| 每节点只在**簇代表点**上建 MST | 局部簇 $L(u)$ 内部无连通保证；反例：叶子 $u$、$L(u)=\{x_1,x_2\}$、基础图无边 ⇒ $S(u)$ 不连通 | 补 $E_{\text{loc}}=\bigcup_{|L(u)|\ge2}\mathrm{MST}(L(u))$，WIKI 上 **1,578,212 条**（占 $E_{\text{conn}}$ 的 81%，不可省） |
| 边数 $\le n-\#\{\text{非空节点}\}$ | 该式给 1,473,212，实测/正确值 1,941,678，**伪** | 正确为 $|E_{\text{conn}}|=n-1$（等号，已证 + 实测） |
| 代表点 = 整簇 medoid | MOVE 后重算需扫 $S(a)$（路径上可达 $10^6$）⇒ 定理 2 的"与 $|S|$ 无关"失效 | 改组合式：$r_{\text{up}}(u)=\arg\min_{x\in K(u)}\lVert x-\overline{S(u)}\rVert$，$K(u)=\{r_{\text{loc}}(u)\}\cup\{r_{\text{up}}(c)\}$，重算 $O(k_u d)$ |

编码细节：eID 按 DFS 先序重排，使 $S(u)$ 为连续区间 $[lo,hi)$、$L(u)$ 为该区间前缀，scope 判定 O(1)，且 `scope_card` 可直接与冻结 GT 对账（冒烟数据 0 不匹配；全量待 G2 独立核验）。

### 12.3 实例结构（实测）

| 量 | 值 |
|---|---:|
| 节点 $m$ / 绑定条目 $n$ | 468,031 / 1,941,679 |
| 非空节点 $A$ / 直接绑定非空 $Z$ | 468,031 / 363,467 |
| 最大深度 / 最大出度 | 31 / 408 |
| $k_u$ 最大 / $\sum k_u$ / $\sum k_u^2$ | 408 / 831,497 / 7,294,109 |
| 上层边 $\sum(k_u-1)$ / 局部边 $\sum(|L|-1)^+$ | 363,466 / 1,578,212 |
| **$|E_{\text{conn}}|$** | **1,941,678 $=n-1$** |
| 新增度数 max / mean / 非零点占比 | **66** / 2.0000 / 100%（1,941,679 点全部 ≥1 条） |
| 有连接边的节点数 | 254,383 |

$\sum k_u^2=7.29\times10^6$ 且 WIKI 最大 $k_u=408$；实测逐节点精确 Prim 为 76.9 s，说明本实例建边可用精确欧氏 MST，无需退化到近似生成树。

### 12.4 冒烟（合成 7 节点树 / 12 个 4 维点，M=2）

`check`：节点 2 的 scope 基础图 **6 个连通分量**、节点 5/6 各 2 个；加连接边后**全部节点 $\text{components}=1$**（$E_{\text{conn}}=11=n-1$ 条）。有向可达比例按入口计：基础图 0.167–1.0，HLG 全 1.0。

旧版 `bench` 的 scope-strict 两臂得到集合命中：`on` 为 6/6、2/2、6/6，`on-noconn` 为 1/6、2/2、1/6；但同次 `off/self` 输出含 4 个 scope 外 ID，暴露其把 traversal beam 与 filtered result heap 混用的实现错误。该错误已在 `hlg_tool.cpp::search_layer0` 修正（两堆分离），同时修正结果由远到近排序及空 non-recursive scope 无入口却触发 fatal 的问题；用户要求停止实验后未重跑，故旧 CSV 已删除、上述检索数字**不作为 G2 证据**。修正版需先按 §12.6 的 smoke 命令复核。

### 12.5 G3 维护（实测，`data/hlg_move_summary.json`、`data/hlg_move_timing.csv`）

| 子树规模 | 样本 | MOVE 中位 | 均值 | p90 | 中位受影响节点 | 中位 $\sum k_a^2$ | 中位置换边数 |
|---|---:|---:|---:|---:|---:|---:|---:|
| <1k | 468 | **24.0 ms** | 33.9 ms | 74.1 ms | 56.5 | 54,869 | 1,184 |
| 1k–10k | 317 | 26.0 ms | 36.7 ms | 85.5 ms | 55 | 57,974 | 1,179 |
| ≥10k | 279 | **20.7 ms** | 28.6 ms | 70.0 ms | 51 | 49,359 | 1,061 |

- 门槛：$\text{median}_{\ge10k}/\text{median}_{<1k}\le3$ ⇒ 实测 **0.86，PASS**。
- 一致性：1064/1064 次 `scnt` 与从零重算一致；结尾 `--verify-rebuild` 对全树重算 $\text{rep\_up}$、质心和子树计数，三个 mismatch 计数均 **0**（说明定理 2 的受影响集合 $\mathcal A$ 完备）。
- **绝对代价要诚实说**：TrieHI 的 MOVE 实测 2.75 ms/op（§11.4 引用行），HLG 中位 22.9 ms/op，**慢 8–12 倍**。HLG 的增量只有"代价与被移动子树规模无关"这一条，不是"更快"。主要成本是两条根路径上高扇出节点的 MST 重建（根 $k=408\Rightarrow k^2/2\approx8.3$ 万次距离/节点）；若要压绝对值，方向是受影响节点的**增量** MST 更新（删/插一个代表点）而不是重算，或按计划 contingency 对高扇出节点做二叉分组。
- 未覆盖：图侧 MERGE 维护未实现（913 个 MERGE 被跳过，故树序与 repro 的 DSM 流不同）；eID 先序区间在 MOVE 后失效（元数据重编号不在本工具内，测的只是连接边维护）。

### 12.6 未测项与待执行命令

前置：`export LD_LIBRARY_PATH=$PWD/deps/PathSeer/faiss/build/faiss`；根目录运行；`--chunk` 取 1941679（**必须一次 `add` 全部点**：faiss 每次 `add` 用 `RandomGenerator(789)` 重排插入序并在锁下并发连边，分块会得到不同图实例——实测 20000×32 上 chunk=20000 vs 137 有 482,252/660,896 个邻居槽位不同）。

| 步 | 命令要点 | 判定 |
|---|---|---|
| 冒烟 | 用现有 `work/hlg/smoke/{hnsw.index,overlay.bin,reps.bin}` 依次跑 `off/self`、`off/faiss`、`on` | `out_of_scope=0`；两个 off 臂 recall 差 ≤0.02；`on` 三查询 hit=6/6、2/2、6/6 |
| 建图 | `hlg_tool index --data …/wiki_full --out work/hlg/hnsw_full.index --dim 1024 --M 16 --efc 200 --chunk 1941679` | 产出 ≈8.1 GB 文件；参数与 repro 的 PG-hnsw-M16 完全相同 |
| **G1** | `hlg_tool check … --per-bucket 25 > data/hlg_structure_check.csv` | `hlg_gt1==0`（定理 1 必然；非 0 即实现 bug）、`base_gt1>0` |
| **G2** | 4 个 bench 臂（`off/self`、`off/faiss`、`on`、`on-noconn`），ef 网格 16…2048 | `on` 的 recursive macro 比 `off` 高 ≥5 pp；`on` 在 ef=128 ≥0.60；`off/self` vs `off/faiss` 差 ≤0.02 |
| 核验 | `experiments/verify_hlg_results.py`（numpy 独立重算 scope 与命中） | `scope_card_mismatch=0`、`out_of_scope=0`、`duplicates=0` |

### 12.7 诚实边界（本轮必须写清）

- **连通性定理不算新贡献**：单绑定 + 静态树在 DFS 先序下就是区间族，RNSG（arXiv:2603.12913v2，PVLDB 19 页面）的 structural heredity 已给出比"连通"更强的保证（含 monotonic search-ability）。本轮未与 RNSG 做同设置对照，因此"连接边是否优于直接上 RNSG"**无证据**。
- 未做多绑定/DAG；未做真实发布方 constraint（锚点仍是 seed=20261026 的合成约束）。
- 图实例不可复现（并发插入 + 每次 add 重排），归档 0.2124 属于另一实例，故 G2 的 0.2124 只作参考，不作硬判据。
- `on-shortcut`（命题 3 的跳数消融，α=8）目前只有 bench 侧开关，**构造代码未写**；待 G1–G3 通过后补，不预先声称结果。

### 12.8 本轮新增文件

- 理论：[hlg-theory.md](hlg-theory.md)
- 代码：`codes/directory-aware-vector-attr-search/src/hlg_tool.cpp`（新建）、`build.sh`（追加 hlg_tool 编译）
- 数据：[data/hlg_build_summary.json](data/hlg_build_summary.json)、[data/hlg_move_summary.json](data/hlg_move_summary.json)、[data/hlg_move_timing.csv](data/hlg_move_timing.csv)
- 脚本：[experiments/verify_hlg_results.py](experiments/verify_hlg_results.py)
- 未提交（体积大，在 `.gitignore` 的 `work/` 下）：`work/hlg/overlay_full/{overlay.bin,reps.bin}`；`work/hlg/hnsw_full.index` 因建图中止、尚未产生。

## 13. 文档目录整理（未运行研究实验）

- **目标与约定**：当前 Directory-Aware 研究文档统一归入 `researches/nested-attr-vector-search/01-directory-aware-acceleration/`；本方向 README 作为入口，上级 `index.md` 只做导航。
- **归档范围**：整体迁移 `2026-10-27-exps/`、`2026-10-28-exps/`，以及 2026-10-27 的 CCS 背景、CCS 问题定义、层级检索方案报告和配套 `assets/`、`scripts/`。原总导航的定义/基线正文保存在 `historical-hierarchical-topk-notes.md`；本方向详细检索记录移入 `search-log.md`，vector-join 总日志仅保留关联入口。
- **追溯与代码路径**：保留日期目录、实测 CSV/JSON 和原始日志；`.command.json`、`.time.txt` 中的旧路径是运行时证据，不改写成新路径。同步更新当前命令、交叉链接、脚本仓库根定位与 autoresearch 的结果/输入清单路径。源码、大数据与临时索引不随文档迁移。
- **验证**：检查 26 份 Markdown 的 190 个本地链接（含 5 个章节锚点），无失效目标；修复旧复现报告中 3 处已失效的帕累托图引用，改用现存的分命名空间图。7 个相关 Python 脚本语法检查通过，18 个路径常量解析并定位到现有目标；只求值路径配置，未调用建图、检索、维护或图像生成函数。
- **结论与下一步**：此次仅归档，不新增实验数字、不改变 G1/G2 待执行及 G3 已测状态。后续实验由用户执行，助手提供目的、完整命令、资源与判定标准。此前命令中的输出根需改为 `R=researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps`。

## 14. 基线完成状态与 IGP 量化/GPU 调研（无新性能实验）

- **当日目标**：核实各基线是否跑完，判断 IGP 量化可迁移部分，按用户要求规划“离线 GPU、在线 CPU”的探索流程。详细书目、源码、内存账、候选假设与未运行验收设计见[专项调研](igp-quantization-assessment.md)。
- **存量结果**：原 all3、局部 Flat、IVF 全列表三个进程的 command 记录均 exit=0；A 类三种目录表示×全局 HNSW/IVFFlat、B 类局部 HNSW 和精确对照已完成。NodeANN 原单次6.839 ms、harness setup三次中位6.639 ms与正式run1三次中位6.649 ms分开记录；scope-run是另一个静态候选。没有已运行的量化基线，HLG全量G1/G2仍待用户执行。
- **计量修正**：§11.2 的 PG/IVF 681行是三种目录表示各227行，而非三次独立运行。分桶标准recall数值核对一致，未改原CSV或已测数值，不再将这681行当作独立重复。
- **原始来源**：IGP（SIGIR2025，Zheng Bian / Man Lung Yiu / Bo Tang）沿用ColBERTv2的VQ+残差SQ；推荐2bit/维。其图建在float质心上，当前CPU实现对有损重建向量算MaxSim，不是原始FP32精排，也没有证明缩短本项目全量HNSW构图。
- **解析式内存账**：WIKI FP32 payload约7953 MB，2-bit残差码约497 MB；加VQ ID、假设1024质心与weight约509 MB，未含图/目录/raw精排/scratch。现有NodeANN不为每个祖先复制一份向量；基线同时保留原始base与局部索引向量，净内存需独立计量。均为算术推导，不是压缩实测。
- **设备与工具**：只查询设备及包/构建元数据，看到RTX3090 24576MiB、driver591.86；现有Faiss为CPU构建，研究venv未发现所查GPU Python distributions。没有执行GPU计算、训练、编码或检索。[环境记录](data/igp_gpu_environment.json)、[内存模型](data/igp_quantization_memory_model.json)。
- **继续查新**：新核iFVS arXiv:2607.22922v1 §2–4，其固定PQ codes、按query+filter校准码本是直接前序；LOPQ的局部量化、SymphonyQG的量化图/隐式精排也不能再直接命名为创新。原先“固定codes + 低秩scope误差摘要”的均值校准假设已在理论上撤回（见下条），当前唯一待测问题是固定bit下scope对齐粗量化能否提高差分残差相关并降低翻转率。尚未充分排查，不宣称新颖性成立。[前序核实边界](data/quantization-prior-art-audit-2026-10-28.json)。
- **下一步与限制**：先明确离线GPU工具链和CPU评分/格式合同，再接全局压缩对照，最后评估NodeANN共享码本与scope对齐量化；当前没有集成量化CLI，不给虚构参数命令。新实验均由用户执行，训练/传输/构图/序列化与CPU查询分别计时；GPU新图不得冒称原CPU HNSW图。IGP源码复用许可未核实，优先按论文独立实现。
- **文档验证**：最终一轮检查 28 份 Markdown 的 220 个本地链接（含 8 个章节锚点）无失效目标；5 份新增 JSON 可解析且内存算式自洽；该方向 7 份 Python 脚本语法全部通过。验证只做路径/语法检查并跑过一次小样本冒烟，不执行研究实验。
- **续：一条设计被否证**：补读 [2609.09854](https://arxiv.org/html/2609.09854) 正文后发现，DSQ 的 top-k 只在同一 scope 内比较，公共加性偏差在排序差分中必然抵消，因此“按 scope 做均值/低秩均值校准”是排序 no-op；该假设已撤回，目录级摘要内存账标记为 refuted。真正控制翻转的是差分残差 $\nu^2=K_{xx}+K_{yy}-2K_{xy}$。修订方向改为：固定 bit 下测 scope 对齐粗量化是否提高 scope 内候选残差相关并降低翻转率，需同时报告 $\rho$、标准化 margin 分箱翻转率、recall 与峰值 RSS。原文 Table 4 显示 int4 在最难 20% margin 仍翻 10.1%，加比特不移动边界。详见[专项调研 §10](igp-quantization-assessment.md#10-决策层面的理论修正与修订方向本轮新增)。
- **续：证书前序**：本地 VADER 报告（arXiv 2609.38053v2）为 filter-aware GBDT 召回预测 + 早停，按 partition 粒度预测但无逐查询证书，$R_{QUT}$ 0.16–0.35；conANN/DARTH+ 本轮只见其参考文献条目、正文未读。可能残留的缝隙是“目录 scope 作 block 的证书 + MOVE/MERGE 失效”，**未证实**，不得据此声称新颖。
- **续：预检脚本**：新增 [scope_quantization_probe.py](experiments/scope_quantization_probe.py)，用质心-only 重建作为最粗代理量化器，比较同 scope 与跨节点候选对的残差相关，可在写任何 codec 之前否证 §10.3 方向。已用 `work/hlg/smoke`（12 点/4 维/4 中心）跑通全流程，输出字段与判定规则符合预期；该冒烟每组仅 2 对、相关系数为 NaN，**不是研究结论**。全量预检未运行，命令交用户执行。
- **续：属性频繁更新方向初筛**：`arxiv.org/abs/2604.22171` 实为 **MCI**（Ye 等），非 “Filter Index Updates”；其 Table 1 把 Filtered-DiskANN 的 “Upd.” 标为 ✗，但附录 A 自述 MCI 当前仍是静态构建。EMA（arXiv:2606.00734v1，提交 PVLDB 未核实接收）已支持一般属性过滤 + 动态更新：边级 Marker、属性变更只 OR 一跳反向边、删除 20% 触发 patch / 50% 全量重建；其 OR 单调不可撤销是阈值重建的结构成因（本文推断）。数值 range 的动态索引已有 DIGRA/RangePQ+/Peng PVLDB 18(10)（仅据 MCI 引用，未读）。故“解耦 ANN 与属性索引”只能作基线；可辩护窄缝收敛为**祖先闭包谓词 + 频繁 MOVE/重标注 + 快照一致性 + 可撤销祖先聚合**，且与本项目已测的 TrieHI MOVE 2.75 ms、HLG G3 20.7 ms、DFS 区间 MOVE 后失效直接衔接。详见[专项记录](dynamic-attribute-updates.md)；本轮未实现、未运行。
- **续：重心收窄到重命名**：更新重点为 A 类“只改标识、不改成员集合”。代码核查发现原型用 `std::to_string(v)` 拼路径键（`src/repro.cpp:238-241`），**名称未物化**，且无 rename 路径——所以 $O(m_u)$ 的改名级联只是“名称写进键”这一设计的代价，ID 键设计下为 0。判据随之改为 R1 结果逐位不变、R2 向量侧写入量为 0、R3 旧名查询显式拒绝或别名、R4 成本界、R5 别名回收；引用计数撤销降为可选（仅服务 B 类）。重命名在已筛前序中未见专门讨论，但属系统/协议贡献而非新索引。详见[§9](dynamic-attribute-updates.md#9-重命名作为一等更新本轮修正优先于-36)。
- **续：主线回到效率（更新仅作动机）**：对 run11 三次逐查询 CSV 做再分析（无新实验），得出效率靶点：scope ≥10k 的 33 条查询（14.5%）占 **91.1%** 时间；**单条 `qi=397`（scope 925,829）独占 111.9 ms = 43.7%**；3 条查询即占 50%。这些大 scope 的有效吞吐稳定在 **28.7–33.9 GB/s**，即内核**带宽受限**，继续调布局的边际收益已很小（run11 vs run12 仅差 2.65% 可作同向证据）。因此唯一有量级空间的杠杆是**降低每候选字节数或可证跳过候选**。内部基线随之明确为 run11（1.1275 ms / recall 1.0 / p95 3.55 ms），而不是 NodeANN（6.639 ms）：新候选须 mean < 1.0 ms、recall ≥0.999、p95 < 2.0 ms、≥10k 桶 ≤2.0 ms、最坏单条 ≤20 ms。设计路线：先做 E1 微基准（FP32/SQ8/SQ4/PQ-64/1-bit 的 ns/候选与 GB/s，作为门禁），通过后用“粗码下界 + 精确重排”的两阶段**精确**扫描（每向量多存 4 B 误差界，共 7.77 MB 算术值）。已记录的负结果：球下界 best-first 只裁 9.45% 向量，故纯几何剪枝不作主路线。详见[效率目标](efficiency-target-baseline-gap.md)与[efficiency-gap-run11.json](data/efficiency-gap-run11.json)；本轮未运行新实验。

- **续：run 14 达成效率改进（binary16 影子层）**：按“带宽受限”诊断实现 `ScopeRunIndex` mode 5——为 tile 布局 packed payload 建 binary16 影子，扫描用 F16C 换算（保留 64 维共享截止反馈），再把粗排前 $k+8$ 名按全局 ID 回行主序 FP32 base 精确重算。冻结 harness 全量 3 次：recursive mean **0.817029 ms**（run11 为 1.127503）、p95 **2.3479 ms**（3.5477）、macro/global recall **1.0/1.0** 不变；代价 build 5.36→9.02 s、peak RSS 16,032→19,824 MiB，均在门槛内。non-recursive 0.00315→0.00619 ms（+96%，仍 <0.025 门槛），来自每调用的小 vector 分配与 18 元素排序，是下一个免费优化点。过程中修复两个真实 bug：用 tile 布局的 packed_ 重排导致向量错位（fixture hit 10/10→3/10），以及空 scope 时 `capacity==0` 读空 vector 触发 SIGSEGV（run 13 exit 139）。日志置信度 8.9× 噪声底；本轮未用召回换速度。详见[效率目标 §7](efficiency-target-baseline-gap.md#7-已达成的结果run-14冻结-harness-全量-3-次)。
