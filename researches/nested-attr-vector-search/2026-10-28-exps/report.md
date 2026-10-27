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
