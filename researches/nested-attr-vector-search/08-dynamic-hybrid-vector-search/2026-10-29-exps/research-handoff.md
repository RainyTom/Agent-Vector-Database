# 研究交接：全部现行实验、历史尝试、负结果与证据缺口

证据截止：2026-10-29，实验/源码基线 commit `19ad280`，分支 `autoresearch/trie-20261028`。本文是供另一模型独立判断的交接材料，**不是既定下一步计划，也不是新论文贡献声明**。本次仅盘点已有材料、核对关键源码/CSV/JSON和运行调度器 `--help`，没有新增 ANN 性能实验。

## 0. 先读结论和交接新增审计

1. 当前最完整的实测是**动态三臂维护策略对照**，不是已成立的新索引：A NodeANN、B PE-ONLINE动态适配、C PE-OFFLINE动态适配。
2. 当前正式计时是 **162次原始WIKI完整子树/配对拓扑的两遍GT实验**。只有root3626在已测Q/U之间观察到成本排序翻转；root410/root828三点均A较低，但不能补画未测区间的交叉。
3. 旧420次动态实验、早期原树162次inline实验真实运行过；其GT/操作记录仍有用，**计时已降为诊断**。计时外逐查询GT读取原始向量，会不对称影响扫描与图内向量副本。
4. 更早的FAISS/扫描布局/HLG/LES等结果是另一实验协议，不能和新hnswlib数据拼同一前沿。部分旧路线已经停止，部分只是未实现；停止原因不等于整个问题被否证。
5. **本次盘点发现旧文档还有实质性误述，不能只把旧报告原样交给下一模型：**
   - WIKI大scope离线路由中，质心最短前缀完整覆盖oracle节点时的ITEM节省中位数为**50.50%**，不是0%；0%对应全部查询组。这个前缀依赖GT，不是在线可实现收益。
   - 旧块球剪枝代码把**欧氏距离下界**与**squared-L2的GT阈值**直接比较。记录的0%不能当作正确统一距离协议对整个剪枝方向的否证。
   - 新静态 `nodeann_research.cpp` 的HNSW距离/hops读库计数，但底层默认未开启计数，因而不是完整访问量；**动态执行器不同，已显式开启底层计数并用CountedL2统计真实距离调用**，不能连带撤回动态距离数。
   - 静态CSV不支持“B1全面快于B3”：D6 balanced为0.049013 vs 0.039264ms，D12 balanced为0.051185 vs 0.033547ms。
   - 静态报告“33条大scope均值77.9ms，占91%”与所列456条均值2.631690ms不自洽；需要回原始run核对来源/分母，**本次未重算出替代值**。
6. 不应承接的旧断言包括：C1路由整个方向已关闭、MERGE没有研究价值、MVCC无收益、所有维护O(1)、PE-OFFLINE所有更新O(depth)、扫描达到算法下界、只剩共享图、已有充分新颖性证据。

**阅读顺序：** 本文 → [当前状态](scheme-status.md) → [原树/两遍GT协议](wiki-topology-protocol.md) → 对应原始JSON/CSV → 必要时回源码。旧文中的推荐顺序只代表历史决策。

### 路径缩写

- `R = researches/nested-attr-vector-search/`
- `D = R/01-directory-aware-acceleration/`
- `L = R/07-laminar-edge-sharing/`
- `E = R/08-dynamic-hybrid-vector-search/2026-10-29-exps/`
- `C = codes/directory-aware-vector-attr-search/`
- `P = production/nested-attr-vector-search/src/`

本文覆盖01–08方向及其已记录主要实验家族、实现变体、失败和候选。逐配置/逐重复全部数值以列出的CSV/JSON为准；不在正文粘贴数十万条逐查询日志。ignored work、远端归档和缺失原始输入的可用性另行说明，不能假定git clone自带所有原始数据。

## 1. 用户问题与不可改变的约束

### 1.1 问题

ITEM含固定embedding、一个直接层级节点、普通类别/数值属性、可选多值标签。查询给q、层级递归子树或非递归直接节点范围、普通属性谓词及k，返回**同一时刻满足全部约束的向量Top-k**。希望支持属性修改、ITEM跨节点、子树重挂、插删，最终还需考虑冲突MERGE和并发一致性。

`scope(u)`只是当前目录谓词选出的ITEM集合，不是某种新图：recursive为u及其后代直接集合的并，nonrecursive只为u直接集合；`eligible`是在scope内继续满足普通属性组的集合。目录条件始终AND。不要把二者基数混用。

### 1.2 硬约束

- 不能因为整scope精确扫描快，就把它改成研究主方案；**允许小直接节点扫描**。exact用于GT/诊断不等于采用其为新算法。
- 索引与查询实现语言统一C++；Python只生成数据/trace、调度、离线分析/制图。历史Python/NumPy探针必须标为离线诊断，不当作同协议在线效率。
- 新建检索索引使用hnswlib，不用FAISS建ANN/Flat/IVF；FAISS仅离线k-means可用。涉及量化必须IGP-style VQ+residual SQ；当前正式动态实验无量化。
- 原始FP32 squared-L2统一 `C/src/raw_l2_space.h` → hnswlib `L2Space::get_dist_func()`。不能另写质心/MST/GT距离来比效率。
- hnswlib固定revision `ca426729609b3221563047ceb269cc1213e0d376`。主数据WIKI和SIFT1M；合成属性、合成层级、子集与全量必须分清。
- 先算法/执行计划/维护策略，再工程优化；SIMD、物理布局、线程池不能作为主要研究贡献。
- 所有臂相同输入/trace/可见性、相同质量门、同一计时边界；失败点保留。论文数字只作参考，不能冒充同机作者实现对照。
- 用户先前明确认可HNSW/图可作为研究基础；不能仅因“用了图”就否定新意，也不能仅拼组件就宣称新意。

## 2. 现行实现：三个动态臂究竟做什么

### 2.1 存储、查询、更新

| 臂 | 存储 | 查询 | 无向量变化的ITEM移动 | 子树重挂 |
|---|---|---|---|---|
| A NodeANN维护策略 | 稳定节点树、直接posting、每大直接节点一个局部HNSW；小节点无图 | 枚举目标/后代非空直接节点；每节点ANN或小集合扫描；全局Top-k合并 | 目标插入/恢复，源图标删；阈值跨越或死比触发同步维护 | 保持节点身份/局部图，改变父子关系；树检查等仍计时 |
| B PE-ONLINE动态适配 | 完整路径键→直接posting；一张全局HNSW | 展开路径前缀，合并直接posting；全局HNSW in-filter | 两次direct成员修改+catalog；不改全局图 | 改写受影响子树路径键；保留direct posting |
| C PE-OFFLINE动态适配 | 路径键→含后代inclusive posting；一张全局HNSW | recursive直接引用inclusive；nonrecursive用父inclusive减孩子inclusive；全局in-filter | 新旧独有祖先成员修改；不重复删加共有祖先；不改全局图 | 路径键改写+受影响祖先内ITEM成员维护 |

这些是**本仓库的动态适配**，不是作者原实现。B/C用 `std::map<string, std::set<Id>>`，不是最佳压缩bitmap实现。A不是“所有ITEM都在一张基础图”：原始向量表含所有潜在ID，但只给超过阈值的直接集合建局部图。B/C才有全局图。**MST没有参与当前420/162动态三臂。**

A按同一ef搜所有被访问局部图，局部k上限为min(k,直接集合大小)，再合并；没有在线相关性路由、不等预算、Tree-TA或自适应fan-out。精确局部Top-k可合成全局Top-k是经典分解性质；当前近似性来自局部ANN，不是已证明的新召回界。

### 2.2 成本：不能写成普遍O(1)

记N总ITEM、M目录、d平均深度、f目标后代节点数、C过滤后候选、U更新频率、k结果数，b受影响ITEM、s重挂目录、a新旧路径独有祖先数。

- A单ITEM移动：目标插入/恢复+源标删+元数据+触发的局部重建。HNSW插入、墓碑与阈值转换不是常数成本。
- B单ITEM移动：direct集合/目录键操作；当前为树集合及字符串查找，不是哈希O(1)。
- C单ITEM移动：a个祖先成员操作及集合/键查找；普通V/T、插删、批量和R不能全部叫O(d)。
- B的R至少涉及s个路径及字节；C还涉及约ab成员变化。A保留图只说明不搬图，不说明树合法性、路径辅助结构、版本发布免费。
- A查询包括目录枚举、所有局部搜索/小扫描和合并；B/C包括候选解析、全局导航及过滤。只测directory latency不能代替端到端ANN。
- 尚无上述HNSW过程的已证明通用最坏时间保证；维护成本主要作为实测函数核算。

### 2.3 实际支持/未支持

| opcode | 已实现语义 |
|---|---|
| Q | 当前提交状态的查询 |
| V | 修改category/numeric；**不是embedding更新** |
| T | 替换tag mask |
| M | 单活ITEM改直接节点 |
| B | 冻结源子树全部活ITEM，再全部直接绑定到子树外目标；不是重挂 |
| R | 整棵子树改父节点，保留ITEM直接节点身份 |
| D | 逻辑删除、成员移除、图标删 |
| I | 激活reserve新ID或恢复旧ID；每个ID的向量固定，旧标签统一unmarkDelete |

谓词：类别等值、数值闭区间、四种tag集合语义（query⊆ITEM、交集非空、相等、ITEM⊆query）；普通属性组AND/OR，目录始终AND，禁用字段不参与OR。当前64-bit标签mask，实验用16标签取3；不是无限字符串标签索引、任意嵌套布尔AST/NOT或完整任意数值域接口。

未实现：embedding改变/不同向量复用ID、冲突MERGE、MVCC、多写者事务、base+delta在线合并、后台压实、动态缓存/物化策略、中央Sample-ANN路由、完整在线不等ef协调。

## 3. 当前正式实验：162次两遍GT原树对照

### 3.1 数据与变量

原始WIKI：1,941,679条1024维向量、468,031目录、363,467非空直接目录、最大深度31；148个目录直接ITEM>512。源 `C/data/wiki_full/{base.f32,test.f32,wiki_tree.txt,wiki_bindings.txt}`；binding第i行对应向量i。cohort保留完整原始子树所有ITEM/节点/边，不随机下采样ITEM。

| source root | ITEM | 目录 | 非空直接目录 | 初始局部图 | 最大直接集合 | native相对最大深度 |
|---|---:|---:|---:|---:|---:|---:|
| 410 | 11964 | 2382 | 2117 | 0 | 284 | 14 |
| 3626 | 11869 | 151 | 143 | 2 | 3843 | 6 |
| 828 | 48403 | 4076 | 3054 | 11 | 3028 | 12 |

root410/3626分别在无大节点/有大节点两层中，按ITEM数最接近12000、ID打破平手选择；计时前冻结。root828为预留放大完整子树。三者不是随机代表性样本；跨cohort向量语义和密度都不同，不能当只改N的缩放实验。

每cohort配对：native保留原始父边；balanced相同ID、直接占用、向量、普通属性，仅parent(i)=floor((i−1)/4)。共享字面M/Q事件流。root与nonrecursive合法集合相同，**wide/narrow递归合法集合会变化**，不能要求其GT跨拓扑相同。

固定：M16、efConstruction200、seed100、统一efSearch1024、扫描阈值512、死比0.2；纯M，ceil(0.5N)不同初始ITEM被移动一次，目标初始非空直接节点且不同于源。普通属性不更新。Q/U名义0.01/0.1/1，三重复、两拓扑、三臂：小cohort108+放大54=162。

实际M/Q：410为5982 M、60/598/5982 Q；3626为5935 M、59/594/5935 Q；828为24202 M、242/2420/24202 Q。唯一覆盖率约50%，不是每秒更新率。456个query向量池，高Q/U重复使用；低Q/U只用59/60/242个不同ID。

五query家族各约20%：root-recursive、broad-recursive、narrow-recursive、nonempty-nonrecursive、empty-nonrecursive。普通属性合成cat8、numeric0–999、16标签取3，tag四语义及AND/OR另行轮换。一个seed20261029。

### 3.2 两遍GT和质量定义

先同一C++执行器逐事件生成**查询当时**的精确Top-k ranked IDs；不是初始静态GT，也不是ANN输出。oracle结束后，再从相同初态重放三臂三重复，计时后读取并核验tape。data/trace/binary/tape/stamp SHA绑定，损坏拒绝复用，resume不重写成功stamp。被测查询不能用oracle的C决定是否跳过/取k。

- 每查询从canonical ITEM/父链核查当前scope/额外谓词，独立于被测posting；测量阶段不再额外读向量算GT。
- GT/搜索/小扫描均同一hnswlib原始FP32 squared-L2。
- 宏召回：非空GT用hit/GT；正确空结果的query recall=1。pair recall=Σhit/ΣGT，空GT不加分母。
- 每次run的macro与pair recall均≥0.999，invalid/duplicate/state violations为0才过门。**这是run级聚合质量，不是每条query召回≥0.999或所有结果精确。**
- 串行同步提交，Q看到此前成功更新。零staleness是该串行语义，不是并发快照证明。
- [INFERENCE] canonical表/谓词与被测执行器仍共享部分实现；独立于posting不等于外部独立状态机已排除所有共模错误。

### 3.3 全部正式总服务成本点

单位秒，3重复均值。T=Σ查询端到端服务时间+Σ更新服务时间；同步维护计入，初始构建、GT、trace解析/输出等不计入T。不是整个进程墙钟吞吐。表中Q/U为名义档，实际比值用3.1的Q/M。

| root | topology | Q/U档 | A T(s) | B T(s) | C T(s) |
|---|---|---:|---:|---:|---:|
| 410 | native | .01 | .0169 | .4231 | .3865 |
| 410 | native | .1 | .1584 | 3.7748 | 3.5018 |
| 410 | native | 1 | 1.7753 | 37.8445 | 34.9648 |
| 410 | balanced | .01 | .0163 | .4103 | .4057 |
| 410 | balanced | .1 | .1588 | 3.7975 | 3.4811 |
| 410 | balanced | 1 | 1.6144 | 37.7450 | 33.7712 |
| 3626 | native | .01 | 1.9437 | .4145 | .3634 |
| 3626 | native | .1 | 2.2835 | 3.8220 | 3.5066 |
| 3626 | native | 1 | 5.8001 | 36.6335 | 35.9066 |
| 3626 | balanced | .01 | 1.8899 | .4252 | .3620 |
| 3626 | balanced | .1 | 2.3238 | 3.8049 | 3.3693 |
| 3626 | balanced | 1 | 5.9929 | 36.2802 | 33.3623 |
| 828 | native | .01 | 3.0150 | 7.4089 | 6.7329 |
| 828 | native | .1 | 8.8391 | 72.3761 | 66.3006 |
| 828 | native | 1 | 64.2580 | 728.6458 | 657.1234 |
| 828 | balanced | .01 | 2.9970 | 7.3392 | 6.7536 |
| 828 | balanced | .1 | 8.4857 | 72.0395 | 66.0164 |
| 828 | balanced | 1 | 62.4570 | 723.7925 | 656.4425 |

root3626：两拓扑×三重复×对B/C的12个比较，均在实际Q/U **[0.0099410278,0.1000842460]** 两端翻转；不拟合精确交叉点。root410/root828每个cohort的36个比较点均A较低，无观测交叉；410全程0局部图，不能作图算法获胜证据。

原始主证据：[small results](data/dynamic_triad/wiki-topology/tape-main-ef1024/results.json)、[scale results](data/dynamic_triad/wiki-topology/scale/tape-main-ef1024/results.json)，以及各自 `analysis/crossing-audit.json`、`query_slices.csv`、`update_slices.csv`、`workload_totals.csv`。p50/p95/p99、目录/ANN时间、选择率、局部图数、fan-out、距离数均在这些文件中；不能用本表总量替代分层读数。

### 3.4 维护与空间：成功/失败必须一起看

- root410：0→0局部图，图插删/墓碑重建/阈值转换均0。
- root3626：2→2局部图；每轮5次真墓碑重建、0转换。9527次ANN插入中9457来自重建，普通插入70；标删2555。结束图内2669活点/188墓碑。native三ratio平均更新约319–321µs/M，明显高于B约1.7–4.9、C约3.6–9.9。
- root828：11→5局部图；17次真墓碑重建、6次图→扫描、0扫描→图。旧combined compaction_count=23**不等于23次墓碑重建**。ANN插入17190中重建17137、普通53；标删4998，结束图内4113活点/468墓碑。
- root828 native Q/U≈.1：更新总量2.426s，其中墓碑重建2.367s，约97.6%；更新p95=3.163µs、p99≈6.228µs，平均100.233µs/M。root3626也约97.6%来自重建。**稀有长事件不一定进入p99。**

root828 native Q/U≈.1，3重复；延迟/构建取均值，RSS取最大：

| 臂 | 更新mean µs | 更新p95 µs | 更新p99 µs | 构建 s | peak RSS MiB |
|---|---:|---:|---:|---:|---:|
| A | 100.233 | 3.163 | 6.228 | 1.817 | 281.17 |
| B | 4.861 | 9.585 | 12.019 | 45.484 | 408.54 |
| C | 20.068 | 32.584 | 41.006 | 47.763 | 424.05 |

A在这个cohort更省RSS；历史合成树却有相反结果，不能普遍化。index_bytes为HNSW预计序列化大小+显式metadata估计，排除原始共享向量；不是实际落盘/总RSS。未测真实磁盘写放大和后台压实。

C metadata写次数：410 native/balanced=68420/61001；3626=34682/34822；828=287111/255005。A/B各为每M三次：17946、17805、72606。最大深度下降不保证C写数下降，LCA及源/目标分布也决定独有祖先数。

### 3.5 质量、失败点及计时限制

- 正式162运行：721656次Q、1950426次U；最小macro/pair recall=.9995868/.9992260；非法/重复/状态违规0。
- 18 oracle真实GT距离调用114183079次，162 benchmark GT距离调用0。
- 与inline逐条比对全部721656 Q/1950426 U的GT/result IDs、状态、ANN距离/hops与维护计数一致；81组拓扑配对检查通过。只是记录字段等价，不是完整执行轨迹证明。
- ef256的12次小pilot中4个B/C失败：root410同一root query30、eligible4228，漏GT ID1055。所有臂统一ef1024后12小pilot、6 scale pilot通过；4096回退未触发。
- 两遍pilot12次通过；resume复用4 oracle+12 benchmark；单独篡改tape副本被SHA拒绝。
- ef1控制：oracle ANN pair recall≈.9879518未达门，但精确GT仍完整计算23272次；后续benchmark保留低召回失败且GT调用0。证明没有把ANN结果充当GT。
- 宏召回包含空GT=1，必须结合pair recall和非空/范围分层。三重复SD不是CI；只有一个seed、一个机器。每阶段最多2 benchmark进程，未绑核，small制图与scale末段短暂重叠，不称隔离计时。
- 机器：Ryzen7 5800X、16逻辑CPU、32MiB L3、WSL2；GCC15.2.0，Python3.13.16。测量binary SHA `e51a31368ceba046dbb6a29917d29e51718409a22b8562c9381c9525906a8a7c`；源码/构建见 [build记录](experiments/wiki-topology-build.json)。

正式六图为 `figures/wiki-topology/tape-main-ef1024_root{410,3626,828}_{frontier,scope}.png`；[当日报告§6.5起](report.md)嵌入/链接，已打开检查。该162只覆盖纯M，不代表其他类型/全量WIKI/SIFT1M/并发已完成两遍验证。

## 4. 当前全局过滤执行器的已知弱点及尚未做的控制

动态 `C/src/dynamic_triad.cpp:381–410` 使用同一 `searchGraph`：手动上层贪心，底层调用 `searchBaseLayerST<false,true>`，预算 `max(args.ef,k)`；CountedL2记录真实距离，hops显式含底层展开。静态runner不是这条插桩路径。

固定hnswlib [底层代码](https://github.com/nmslib/hnswlib/blob/ca426729609b3221563047ceb269cc1213e0d376/hnswlib/hnswalg.h#L335-L460)：结果堆只收合格活点；未满ef时邻居可继续扩展，距离界停止要求堆大小=ef。因此C<ef会耗尽底层可达图，不要求全图连通，距离次数还含上层。

旧inline的一项实际导航诊断：root410 native Q/U1，1196次nonrecursive-nonempty平均scope6.112、eligible1.888，C距离调用12014.3、hops11969，接近11964规模全图；工作量在两遍复测中逐行相同。这不是目录解析快慢可解释的差距。

**尚未实验，不能当作下一步已经确定：**

- 候选基数感知停止、候选入口/路由、bitmap过滤、其他filtered ANN强基线均未在新动态协议完成对照。
- 只设 `ef=min(ef,C)` 不一定修好：当前实际调用仍 `max(ef,k_passed)`；C<传入k时堆仍填不满。并且即使可停止，稀疏合格点也可能需要大量全局导航。
- 当前日志的eligible C在计时后由GT/元数据审计得出，不能直接喂给在线方法当免费oracle；应真实计入谓词解析、计数、集合物化成本。
- B/C已有std::set成员测试和临时候选并集/差集，未证明是优化充分的公共基线。相同ANN后端不等于所有过滤执行计划都已公平优化到位。
- 若强化同功能模块，A局部图与B/C全局图必须使用同一规则/内核，不能仅给拟议方法换执行器。
- 既有B/C已体现“全局HNSW+过滤”，但**没有另做第四个强全局filtered ANN系统，也没有新协议下完整pre/in/post-filter与作者系统矩阵**。

这里的停止弱点不证明“修完就一定更快/交叉消失”。上一轮将其列为唯一优先问题只是当时建议，交给下一模型重新权衡。

## 5. 历史动态三臂：420次inline实验全清单

### 5.1 实际完成矩阵

| 家族 | 实际设置 | 运行数 |
|---|---|---:|
| screening | SIFT/WIKI各2000初始+200reserve；每数据21配置，mixed覆盖0/.1/.2/.5/.8，七更新类型；depth3/7、uniform/Zipf/shift交互 | 42配置×1重复×3=126 |
| crossover | 各12000初始+1000reserve；depth3/uniform/mixed；覆盖.1/.2/.5/.8×Q/U .25/1/4，另Q-only | 26×3×3=234 |
| move-crossover | 各12000；纯M，唯一覆盖.5，6000 M，Q/U .01/.1/1，60/600/6000 Q | 6×3×3=54 |
| pilot | 两数据12000，mixed覆盖.2、Q/U.25 | 2×1×3=6 |
| 合计 | 2523684次query执行；每数据仅64 query vectors反复使用 | 420 |

合成树8叶，80% ITEM在叶，20%内部；depth7通过单链扩展保持叶数。cat8/num0–999/tag16取3；SIFT阈值1024/WIKI512，ef256。四scope家族各25%，含固定空nonrecursive；**非空nonrecursive只做语义回归，未纳入这420性能矩阵**。

覆盖率为受更新过的初始活ITEM并集/N，不是操作数/N。Zipf指数1.2；shift前后热点不同，12:1权重；唯一覆盖约束会耗尽热点候选，不能称始终经验Zipf。mixed的V/T/M/D/I轮换，结构B/R单列。driver各jobs2，历史有两个driver重叠最多4进程；非隔离。

### 5.2 成功、失败和边界

| 观测 | 具体数字 | 能支持什么 |
|---|---|---|
| 纯M交叉 | Q/U.1 SIFT A/C=2.041/1.118s、WIKI=6.213/3.271s；Q/U1=5.539/11.550、18.528/31.912s | 各重复低两档A输、高档A赢；仅该inline协议(.1,1)夹区 |
| 图维护写放大 | 6000 M，A SIFT/WIKI插入36526/25506，combined维护56/24次；B/C零ANN写 | 重建重插不可忽略；旧combined尚未分纯墓碑/转换 |
| 批量B负例 | WIKI3次B、64Q：A p95查询323µs<C549µs，但总量A/B/C=280.763/22.977/22.222ms；A约C12.6倍，SIFT约4.64倍 | 查询快可能被维护反转；不是R |
| R操作 | 5次R，WIKI A/B/C metadata写15/24/5552；均0 ANN写 | 受控15节点树的真实计数；不是全操作O(1)证明 |
| mixed无交叉 | 26配置均值A最低；C/A SIFT2.07–3.01、WIKI1.78–2.77 | mixed含新向量reserve插入，不代表纯属性移动 |
| 深度 | mixed80% uniform depth3→7，C写8383→12742，A/B均5600 | 本trace的成员操作变化 |
| 空间负例 | WIKI纯M Q/U.1最大RSS A/B/C=161.38/110.44/111.31MiB | A不是无条件省空间 |

420均达macro/pair≥.999，最低约.99957917/.99934964，非法/重复/状态0；但inline计时不能作最终无偏优越性。主driver3600s中断后复用226、补8，234主矩阵最终完成；这是执行中断不是算法失败。支持但未跑的factorized套件/dry-run不能额外计数。

证据目录：`E/data/dynamic_triad/{screening,crossover,move-crossover,pilot}/` 中 `results.{json,csv}`、config/commands/manifest及 `analysis/`；`stream-audit.json`保存跨Q/U同更新流/覆盖前缀等检查。复现：[dynamic-triad-reproduction](experiments/dynamic-triad-reproduction.md)。原始逐事件CSV在 `E/work/dynamic_triad/` ignored。

### 5.3 原树inline历史单独归档

不混420：小pilot ef256 12次（4失败）、小ef1024 pilot12次、small主108次、scale pilot6次、scale主54次。后者旧源码checkpoint `4dcf430`，small/scale主计时均已降为诊断。它们与后来的两遍162不能相加视作独立同协议重复。两遍pilot12、ef1控制1另列；4096只是未触发预案。

## 6. 动态方案C1–C7及更早探针：到底做到哪里

### 6.1 候选状态

| 候选 | 已尝试 | 未完成/不能声称 |
|---|---|---|
| C1 层级联邦路由+不等预算 | oracle分布、质心/半径/小样本离线排序、GT依赖预算、top-m探针 | 无完整在线路由、不等ef执行器、动态摘要维护、同质量端到端新方法 |
| C2 中央Sample-ANN | 每节点≤4样本到q的精确最小距离评分 | 没建中央样本ANN、没做ANN投票/局部刷新成本；samplevote名不代表真正投票 |
| C3 MOVE/MERGE/回收/可见性 | 规模采样、T/C churn、并发探针、摘要模型；后来三臂M/B/R等 | 冲突MERGE三策略、后台合并、并发staleness分布未实现 |
| C4 Tree-TA | 设计、若干粗糙层级探针 | 无TA停止算法/条件保证/实际对照 |
| C5 四集合执行 | 当前C++四语义及AND/OR、GT/回归 | 无反向包含专用索引或完整复杂布尔加速 |
| C6 MVCC/快照 | 有缺陷的节点锁探针 | 无MVCC、完整fan-out快照；“无收益”撤回 |
| C7 热点物化 | 生成过热点/shift负载 | 无自适应物化/撤销算法或专门对照 |

`stage2-candidates.md`中的“高新颖性/关闭/当前主线”是历史判断，不是已核实领域空白；`stage3-formalization.md`把V/T全当跨节点churn也已不符合现实现。T/C/R的R多为初始构建比较，不是完整动态第三臂。

### 6.2 oracle与路由诊断

WIKI227个recursive非空query，33个scope≥10k。大scope组的oracle节点中位10，而scope非空节点中位4722；只访问真正GT所在节点的ITEM节省中位99.3914%，但不可在线预知。

| 大scope组指标 | 质心 | 距离减半径 | 每节点≤4样本最小距离 |
|---|---:|---:|---:|
| top10节点覆盖oracle节点 | 20.24% | 8.40% | 62.27% |
| top1%节点覆盖oracle节点 | 42.30% | 27.07% | 67.88% |
| 完整覆盖oracle节点的最短前缀ITEM节省中位 | 50.50% | 13.52% | 66.13% |

全部query组的前缀节省中位0%；不要与大scope组混淆。样本占全语料39.68%，不是免费小摘要。以上覆盖率不是ANN Recall@10，前缀长度由GT反推，不是在线停止。

达到GT recall .999所需访问ITEM比例中位：WIKI质心49.50%、半径86.48%、样本33.87%；SIFT D4/D6/D12 balanced质心85.46/81.59/83.18%，D4 skewed99.05%。SIFT各≥10k query仅10/5/11/7条。旧WIKI“1.29倍”、SIFT“1.01–1.22倍”来自距离次数模型，非统一C++在线计时、非普适收益上界。

层级top-m=1/2/4/8的大scope ceiling recall=0/.909%/1.515%/4.848%。**代码摘要只覆盖节点直接ITEM，并跳过直接为空但子树非空的内部节点**；目标可能根本不可达，不能否证正确inclusive摘要的层级路由。

证据：`E/experiments/{oracle_routing_probe.py,recall_budget_curve.py,hierarchical_routing_probe.py}`；[oracle JSON](data/oracle_routing_wiki.json)、`data/budget_curve_{wiki,d4_balanced,d6_balanced,d12_balanced,d4_skewed}.*`、`data/hier_route_wiki_m{1,2,4,8}.*`。

### 6.3 MOVE/MERGE规模采样

1000次WIKI采样，无实际MERGE/建图/服务计时。源子树目录中位3598、ITEM21252；源/目标直接ITEM之和中位6、p90=38、max11101。

所谓conflict只是两直接集合大小之和，**没检测同名相对路径碰撞，也没递归合并**。所以“中位6→MERGE无研究价值”撤回。计划中的惰性双图/重建/后台合并三臂没有跑。源码 `E/experiments/merge_conflict_sizing.py`，数据 `data/c3_sizing_wiki.json`。

### 6.4 局部T/C churn实测

统一hnswlib原始距离，但非完整scope查询、非严格同更新流：WIKI≥128节点1142/409737 ITEM；SIFT D4≥128节点4485/976771 ITEM；D6 skew≥256节点311/792217 ITEM。T只标删，C死比>.2重建；主ef64，另WIKI T ef8/16。

| 数据/臂 | recall起→终 | 局部查询µs起→终 | 终dead比例 | 增量维护s / 另计重建s |
|---|---|---|---:|---|
| WIKI T64 | .988039→.995412 | 42.98→79.27 | .444207 | 73.8125 / 0 |
| WIKI C64 | .988039→.994510 | 43.32→47.94 | .110072 | 60.0469 / 52.8075 |
| D4 T64 | .999973→.999984 | 10.85→19.64 | .444339 | 54.7998 / 0 |
| D4 C64 | .999973→.999993 | 10.76→12.95 | .111790 | 45.9495 / 46.9625 |
| D6skew T64 | .999743→.999791 | 22.52→59.09 | .442508 | 113.748 / 0 |
| WIKI T8 | .770355→.854286 | 19.10→34.01 | .444207 | 74.2395 / 0 |
| WIKI T16 | .860736→.924606 | 23.74→42.39 | .444207 | 74.2666 / 0 |

校正：C自身终点维护应为WIKI112.8544s、D4 92.9120s，不能把T臂维护加C臂重建。C每步扫描全部big_nodes检查阈值的外层成本也未完整计入这些局部列。

alpha=done/total_moves，total_moves=.8×初始indexed ITEM；失败尝试也可能推进done；不是唯一ITEM覆盖。两臂共用query/update RNG，压实会影响后续选点，因此不能给可靠动态交叉点。“60–80%覆盖应全量重建”“墓碑永不伤召回”都不能继承。代码 `C/src/churn_probe.cpp`，数据 `E/data/churn_*.csv`共7文件。

### 6.5 并发探针：运行过，但正确性结论撤回

17个保存文件：conc/conc4/conc5/hot各4协议（add-del/del-add×validate0/1），另hot无更新控制。每次3000Q。互异fan-out修正后的conc5窗口约add-del1.54µs、del-add173–185µs；hot约15–16 vs811–967µs。conc4四协议均73.333次重复/千query，疑为早期重复采节点；当时独立源码快照未找到。

当前可见 `C/src/churn_concurrent.cpp` 对node_of的读写没有共同锁/原子同步；逐节点锁不能给整个fan-out单快照；候选拼接统计不是同状态全局Top-k，也没有精确GT测漏检。不能用低异常数、窗口比或validate=1证明一致性/MVCC无收益。数据 `E/data/conc*.csv`；所有这些时间均为有缺陷的历史探针观测。

### 6.6 摘要回收：模型，不是ANN实验

WIKI/SIFT D4树、合成Cat32+Num16桶+Tag128共176位，rho=0/.8；churn0/.1/.2/.5/.8是事件/N、有放回，不是唯一覆盖。OR/count/epoch、age10/100/1000/100000，主200行；另WIKI纯ITEM移动attr-change-frac0控制50行。脚本不打开base.f32。

- WIKI rho=.8终点：陈旧位.350578；OR/count探测比1.196131；候选节点比例.404270/.301997；按ITEM加权探测/真匹配5.882872/4.918250。
- 纯M控制：陈旧位.349817，探测比1.205004；不是R子树重挂。
- epoch age100000：可信位.050241、FP .016291，激进模式漏真值均值.935618；保守模式等价OR是实现语义所致。
- D4 rho=0初始174.08/176位、候选.997222；rho=.8初始161.30、候选.898889，终OR/count1.058623。不能混为同配置。
- OR24B/count352B/epoch728B每节点是解析空间模型，不是实际Python数组RSS。更新5/10/5次bucket-write亦为逻辑模型；checkpoint会跳过当前无匹配谓词，样本数并非一直200。

代码 `E/experiments/summary_reclaim_probe.py`，数据 `data/summary_reclaim_{curve.csv,summary.json}`及`*_attrfrac0.*`。无在线ANN集成/真实后台回收/溢出协议，不能说问题已解决或整个回收方向无价值。

## 7. 新hnswlib静态NodeANN：已做、成功与计量缺口

**标签不同：** 这里B0=纯局部HNSW，B1=小节点扫描hybrid，B3=整scope exact/GT；不是动态A/B/C。

SIFT1M六变体：4096固定叶，D4/6/12分别分支8/4/2，总节点4681/5461/8191；balanced/Zipf1.2，80%叶绑定、20%内部祖先，与向量独立；各1000Q，recursive/nonrecursive各半。WIKI真实全树456Q。固定M16、efC200、seed100，ef16–512，构建4线程，查询单线程。

Tscan microbench冻结128维1024、1024维512；WIKI只有148直接节点需图，覆盖9.25%ITEM。B0每实例固定65536 mutex，按40B推算363467图仅锁表约952.8GB十进制/887.4GiB。40GiB cap下18任务exit−9，是该实现固定开销/资源失败，不是所有NodeANN都不可行，不能误写成捕获bad_alloc。

最初259任务jobs10产生严重争用，222 OK；后clean仅单rep。`frontier_agg_clean.csv`仍混source=pass1/clean，**必须逐行看source/n_reps**；clean不是全曲线三重复。

| ef64 clean数据 | B0/B1 mean ms | B0/B1 build s | B0/B1 RSS MiB |
|---|---|---|---|
| D4 balanced | .060782/.022397 | 10.075/3.287 | 12953/734 |
| D4 skewed | .021940/.016169 | 82.576/80.811 | 12952/1334 |
| D6 balanced | .104555/.049013 | 9.242/2.153 | 14908/614 |
| D6 skewed | .019638/.013690 | 78.819/70.692 | 14907/1221 |
| D12 balanced | .096744/.051185 | 9.191/1.505 | 21749/635 |
| D12 skewed | .019755/.012492 | 78.941/65.658 | 21746/1241 |
| WIKI | 不可行/2.631690 | —/21.525 | —/约8770 |

这些ef64点召回1。WIKI B1 ef16/32约.99955，≥64为1；B3约2.32ms。balanced和skewed B1没有完整clean全ef曲线，只有ef64点。B1比B0好不等于比B3全面好：D6/D12 balanced的B3分别.039264/.033547ms，反而更快；B3仍只作诊断，不转主方法。

| D1–D6旧判决 | 当前可保留边界 |
|---|---|
| D1小图固定开销 | 有源码/内存直接证据 |
| D2目录分区无几何收益 | 判词过强；只证当前小集合scan/图交叉，不是目录完全无几何收益 |
| D3merge放大否定 | 本workload merge≤.6%、目录≤2.5%，不泛化所有fan-out |
| D4局部近似遗漏 | 低ef损失随ef改善；不是理论保证 |
| D5大scope尾部主导 | 有尾部/扫描的定性观察，但77.9ms、91%需核对；不能直接沿用数字 |
| D6局部启动主导否定 | 本分解启动<3%；不否证能减少ITEM访问的共享导航 |

**距离计数缺口已核源码：** `nodeann_research.cpp:493–502`读 `searchKnn` 的metric差；库底层模板collect_metrics默认false，公开searchKnn未开启。现静态HNSW距离/hops漏level0，不能拿这些列证明真实总距离减少。动态 `searchGraph` 已 `<false,true>`+CountedL2，不受同一缺口影响。

O1 global heap、O2球order/skip、O3 global-cutoff-aware local HNSW在新runner未实施；新静态B2a–c同全局hnswlib目录表示对照仅在计划。代码 `C/src/{nodeann_research.cpp,raw_l2_space.h,scan_threshold_bench.cpp}`、`scripts/gen_sift_hierarchies.py`；证据 [静态CSV](../../01-directory-aware-acceleration/2026-10-29-exps/data/frontier_agg_clean.csv)、`D/2026-10-29-exps/experiments/{run_nodeann_matrix.py,aggregate_runs.py}`、`data/tscan_bench_*`。

## 8. 更早静态路线：保留历史，不作现行公平前沿

### 8.1 原论文/目录表示复现

PE-ONLINE/PE-OFFLINE/TrieHI本地实现早期用FAISS/CRoaring。SIFT100k synthetic wiki recursive目录均值1683.2/4.5/4.0µs；nonrecursive2.2/10.1/10.1µs。全量WIKI目录recursive1964.93/2.57/3.22µs，nonrecursive.95/44.21/50.12µs；MOVE38.1/14.9/2.75ms；MERGE .077/.431/.120ms。目录/DSM测量不依赖后来的向量转换bug，但一般约束为合成，本地数值节点路径不等于生产名称字典。

早期 `data/real/test.f32` 曾int位模式错转、457约束对456向量越界、NODE重复scope解析、hit/10非标准recall。污染范围未逐实验核实，旧400k/1M ANN数字不能与修复后连线。SciHTC真实标签结构修正为830概念、深6、26冲突父；64维向量为随机占位，不能称真实语义检索。OpenViking QA/Table VI–VII未复现。

修复后全量WIKI，456Q（227rec/229nonrec）、原始FP32、单线程单次；recursive：

| 历史配置 | macro/global recall | mean ms |
|---|---|---:|
| TrieHI+FAISS HNSW ef2048 | .212384/.308634 | 20.091 |
| TrieHI+IVF np256 | .467083/.547247 | 6.770 |
| NodeANN全局部HNSW ef64 | .999559/.999320 | 6.839 |
| NodeANN局部Flat | 1/1 | 4.305 |
| TrieHI+IVF np1024 | 1/1 | 25.126 |

目录表示固定执行器/参数命中一致；目录枚举93.279µs≈1.36%，不是唯一瓶颈。all3 RSS35.492GiB同时含多个索引，不是NodeANN净内存。证据 `D/2026-10-26-triehi-reproduction/`、`2026-10-26-real-dataset-validation.md`、`2026-10-27-exps/data/wiki_full_*`、`2026-10-28-exps/report.md`。

### 8.2 scope-run全部14轮

完整WIKI旧FAISS协议，同456Q、每轮3进程。仅静态single-binding DFS物理区间，不支持动态R/MERGE/DAG；违反当前主线限制的工程/全扫描收益不能转为现行新索引结论。

| run | 尝试 | recursive mean中位ms | 结果 |
|---|---|---:|---|
| 1 | 局部HNSW起点 | 6.649231 | 起点 |
| 2 | buffer/heap预留 | 6.555309 | 约1.41%工程收益 |
| 3 | DFS连续scope-run、单global heap穷尽扫描 | 2.142991 | recall1；历史强工程对照 |
| 4 | 四向量交错累加 | 2.606194 | 更慢，丢弃 |
| 5 | 每64维partial后完整refinement | 2.807973 | 更慢，丢弃 |
| 6 | tile8 AoSoA/AVX2 | 1.856979 | 布局对照 |
| 7 | tile64维共享cutoff | 1.739233 | 比6降低6.34% |
| 8 | 每256维检查 | 1.815224 | 丢弃 |
| 9 | 每16维检查 | 1.740295 | 无可靠收益 |
| 10 | 子树质心8维sign排序兄弟 | 1.798043 | 丢弃 |
| 11 | 节点内sign编组+顺序payload | 1.127503 | 两机制耦合 |
| 12 | 关编组、保留顺序payload | 1.158137 | 几何净差仅2.65% |
| 13 | binary16影子粗排开发 | 失败 | capacity0 SIGSEGV；另曾payload错排命中10→3 |
| 14 | binary16粗排+前k+8 FP32 rerank | .817029 | 三次观测recall1；nonrec .00619ms，反而比11慢约96% |

run14 p95≈2.3479ms、build9.02s、RSS19824MiB。浮点margin与固定k+8均非任意输入精确性证明。binary16不是当前获准IGP量化路线，不能作为合法现行起点。

源码 `C/src/scope_run_index.h`、旧 `repro.cpp`；数据 `D/2026-10-28-exps/data/autoresearch_iterations.*`、`efficiency-gap-run14.json`；报告 `efficiency-target-baseline-gap.md`。这些优化不构成新的动态维护策略。

### 8.3 过滤选择率/几何相关性

FAISS SIFT100k/1M，旧基准，不转移为hnswlib参数保证：

| 尝试 | 结果与限制 |
|---|---|
| random谓词list/bitmap/HNSW/post-filter | HNSW-only交点约s=.0605(100k)/.0194(1M)；100k s=.001 list192313QPS、HNSW36.1、post10.2；不是普适阈值 |
| 相同可行集list vs bitmap | s=.001/.01 list约快2.50/1.89倍；双表示维护空间未完整计费 |
| cluster-far | HNSW ef32768在s≤.3仍约50–87% recall，PathSeer ef4096约31–66%；仅这些压力设置失败 |
| cluster-near | q换簇中心，HNSW ef32约98.1–99.5%，交点约.012；人为易负载 |
| part_flat | 仅s=.3快15%，低s慢 |
| part_gather | 10条/桶smoke，仅s=.3快26%；非正式全矩阵 |
| partition-HNSW | 10条/桶smoke333–1205QPS，慢于exact；非全部分区方法否证 |
| IVF+bitmap | random十桶100k≤1%扫描、5–20%IVF、≥25%HNSW；far常需np1024全列表，near被夹住 |

PathSeer为固定扩展参数本地适配，不是完整作者调参流程；更宽扩展曾遇fork溢出，未完成全部网格。代码 `P/selectivity_bench.cpp`；资料 `D/2026-10-26-selectivity-sweep/`。

### 8.4 其他几何/量化候选

- 直接节点质心B路由：86条candidate≥16查询，B=1/16/64/256/1024/4096/16384的GT覆盖10.12/48.37/66.51/86.05/97.67/99.88/100%；B16384仍访问78.45%向量。这是候选覆盖，不是local ANN最终召回。
- 节点球best-first+exact诊断：非平凡组裁48.15%节点但只9.45%向量；全质心float64约2.978GB；没测在线摘要净时间，无通用安全证明。
- 51节点几何抽样：同节点对距/随机约.797/.841/.899；多球半径改善有限，只限该样本。
- 256块/256cell球剪枝：单q397、scope925829，记录各slack剪枝0%。**源码欧氏lb与平方tau单位不一致，且正slack更保守；修正后是否仍0没有重跑。** 不支持“所有box/逐对象误差界/剪枝已关闭”。
- IGP VQ+residual SQ尚无真实codec/性能；WIKI 2bit约497MB、加中心等约509MB只是解析空间模型。
- scope公共均值/低秩公共加性校准因排序差分抵消而撤回；不否证差分残差相关等其他scope-aware量化。相关probe全量未跑，12点smoke/NaN不作证据。
- geometry cell×directory rank/interval、wavelet、bit-plane、global refinement queue、动态rope均为设计，无性能点；SQ8/PQ64等旧建议已被当前IGP-only合同取代。

入口：`D/2026-10-28-exps/experiments/{node_routing_diagnostic.py,node_geometry_probe.py,probe_scope_pruning.py,scope_quantization_probe.py}`；`data/node_routing_*`、`node_geometry_probe.json`、`scope_prune_probe.json`、`igp_quantization_memory_model.json`；`igp-quantization-assessment.md`、`pareto-index-mechanisms.md`。

## 9. HLG/全局基础图/MST：之前的问题对应哪套方案

旧HLG意图是一张全ITEM基础近邻图，加按层级组织的连接边，查询在合法scope内导航；与当前A局部索引不同。

MST的职责是**连接支架**：直接集合内部MST+子簇代表点MST，使层级子集合有连接边。它不是自动的近邻质量/低stretch保证，也不是因为连通就能保证有限ef召回。不是说整张基础HNSW只有n−1条边；n−1指新增层级连接树。

已实施：连接边构造、组合代表点、MOVE维护；全量1941679 ITEM新增1941678边，其中直接内部1578212、上层363466；12线程90.8s（Prim76.9s），新增度mean2/max66。

修正过的设计错误：只有代表MST不足以连直接集合；原边数公式错误；整簇medoid会扫描大scope，改组合代表。

G3历史：1064 MOVE，中位22.9ms；<1k/1k–10k/≥10k约24.0/26.0/20.7ms；重建核对0 mismatch。**比旧TrieHI2.75ms慢约8–12倍，不是获胜**；913 MERGE跳过；MOVE后DFS/eID区间失效、重编号成本没计。相关性不等于O(1)证明。

G1/G2全量未完成：当时全量基础HNSW没产出；12点smoke曾有越scope/反序/结果堆混用bug，修复后未重跑，旧CSV已删。旧FAISS邻接+自写double距离不合新合同，约8GBoverlay和旧smoke已清理，保留小CSV/JSON。**新hnswlib HLG G1/G2没有做**。

源码 `C/src/hlg_tool.cpp`；资料 `D/2026-10-28-exps/{hlg-theory.md,report.md}`、`data/hlg_{build_summary.json,move_summary.json,move_timing.csv}`。受限度MST/shortcut/统一合法入口/预算新搜索只停留在候选，`on-shortcut`开关不代表完整实现。

## 10. 02–06冻结设计：没实现不等于实验失败

| 方向 | 原设计 | 状态与已知重合/缺口 |
|---|---|---|
| 01原D0按需scope编译 | 稳定ID块、直接/物化/cache盈亏切换、批量过滤 | 停独立立项；Curator lazy subtree/temporary index/cache很近；后来01实验不能被“原D0未实现”概括 |
| 02 DAG增量scope | U bitmap+计数例外、0↔1传播、push/pull、epoch | 停止；Counting/DRed/Enzyme已有；无独立空间/输出敏感维护界 |
| 03 prefix-coupled图 | 固定基础图、自底向上共享连接、祖先加权修补、scope入口 | 暂停；iRangeGraph/JAG接近，连通不推有限ef质量；未实现原MVP |
| 04渐进认证路由 | 摘要调度、球/投影盒完整前沿、预算细分 | 停止；CertEx/SPI近，距离误差界不是Recall/QA证书 |
| 05版本目录 | COW绑定拓扑/内容、不可变ANN段+delta、manifest发布/GC | 暂停；与ChronoVec/湖仓snapshot/MVCC重合需澄清；无实现 |
| 06继承ACL | deny-overrides、授权前沿、组指纹/版本、新鲜度 | 停止；Zanzibar等已有，非ANN语境差别尚无独立增量；无实现 |

文档为R内各方向README及 `alphaxiv-novelty-audit.md`。预设10k–50k、p95降15%等都是验收目标，不是成绩。`directions.md`“所有方案未实现”只适用原设计集，不适用后来的01/07/08。

## 11. 07 LES共享图：独立历史线的完整主要尝试

### 11.1 机制与语义

路径：逐scope HNSW（PSG）→收level0→按source/activation/target共享、run/varint→保留scope独立upper/entry→查询。多标签scope未必laminar；共同最小激活元可能多个，因此不是“一物理边永远只存一次”，共享并图也不一定等于原逐scope图。内部paper.md不是发表论文，旧唯一激活/Exact保证已被后续审计收紧。

| 尝试 | 保存的成功/负结果 | 限制 |
|---|---|---|
| PSG逐scope/B4 | SIFT100k，85/1365节点约94698/.9570、74706/.9641 QPS/recall | 与低质量或不同预算基线不可直接算胜负 |
| PFS exact | 191914QPS、recall1、p95 11.3µs | 不是LES Exact成绩；scope小 |
| filtered HNSW | ef384 1282QPS/.9524，ef512 991/.9642 | FAISS+bitmap本地适配 |
| ACORN两适配 | 394/.8479、620/.8395 | metadata/γ等未完整，不能否定作者系统 |
| LES初版Subtree | ef32/48/64=15694/.9967、11908/.9992、9940/.9999 | 旧104MB后纠为全目录144.3MB；4.5s不含底图 |
| activation runs | metadata26.775→6.491MB，完整144.314→124.029MB；13387704边解码一致 | 局部8.8倍非全索引比 |
| 距离序/三角剪枝 | 0/5745081，0%剪枝 | 该dist16机制无收益，v2删除 |
| direct-direct Exact beam | 初739违规；strict后0违规、recall.9269、59空 | 不能用边代替direct posting；旧37/45QPS应37310/45078 |
| 五补充L2负载 | PSG近同recall快1.65–3.59倍；SIFT100k 51904/.9801 vs LES18142/.9802 | 25/50/100k同SIFT前缀+MNIST/Fashion，仅3独立语料；旧2次取最好 |
| 全局degree cap24 | recall .33–.40，仅710/1000填满 | 失败，跨scope连边被删 |
| 每activation桶cap8/16/24 | cap16约29973QPS，较25179增19%，104.5MB | 仍约PSG .40倍 |
| 共享向量PSG强对照 | M16 62.3MB、M12 53.6MB；原约147MB含重复向量 | 不能只和弱复制向量版本比空间；同图转换输出逐位一致 |
| LES-v2 | 124.1→73.6/69.0MB，r95 35084、r98 23504QPS | 仍落后共享PSG |
| 11稠密语料 | glove100 r95 1.29倍/.94空间；glove2001.90倍/.77；lastfm r98 1.17倍；nytimes PSG未达r95 | 8个angular/dot语料转minmax-u8-L2代理；原生SIFT/MNIST/Fashion落后，非通用SOTA |
| E0容量/entry复用 | 输出一致；单次fixed12 29.1→23.5ms | 执行优化 |
| v3 varint | 73.6→53.3MB，1010797 runs全量解码、同ef输出一致 | 无损表示证据较强 |
| cardinality adaptive beam | ef24 22646/.9880→27666/.9807 | +22%伴随降recall，不是固定质量净增22% |
| 预取向量 | fixed24 45.2→56.3ms | 负结果，移除 |
| scope<1000改exact | 31.0 vs30.4ms | 无收益 |
| S1完整邻接interning | 1034631引用/1019476唯一，唯一率.985；补字段≥88.16MB未含lookup | 只静态空间分析，无查询实现；不否定全部块字典共享 |
| B1 scope桶ef预算 | 留出aggregate部分达标，见下表 | 经验校准，无逐query/逐桶保证 |

### 11.2 最后留出/归档与撤回

历史1000Q重分fit/val/test400/300/300，15交错block、batch wall。这些并非从未参与方法选择的新query，block只测计时波动。

| 配置 | test recall | QPS | 判定 |
|---|---:|---:|---|
| LES fixed14 | .958333 | 33372 | 与M12 fixed40同质量 |
| M12 fixed40 | .958333 | 41443 | 快1.242倍；53.58MB略大于LES53.27，非严格同空间 |
| LES policy r95 | .955333 | 36712 | M12 fixed40对它快1.129倍，不是1.24倍 |
| LES policy r98 | .981333 | 24641 | aggregate达r98 |
| M12 policy r98 | .979000 | 30683 | 未达r98，不能参同质量胜负 |
| M16 fixed48 | .980667 | 35090 | 达r98、快1.424倍，但更占空间 |
| M12 posthoc ef72 | .9853 | 23029 | test后单次，无配对block，隔离补充 |

失败：LES fixed12=.9473、旧adaptive=.9760；M16 r95策略=.9453未达标。最大scope≥10000桶148条，LES r95=.939189、M12 fixed40=.941216，聚合达标不等于大桶达标。“M12 r98快1.21倍”“全档严格支配”已撤回。归档225轮=15配置×15block，摘要报告0违规/重复/空；ids/timing二进制留远端，本地未重新算距离/资格。

后续用户曾要求重启Pareto扫描；实际只把旧225记录画成15点，没有新ANN性能实验。M8/12/16/24/32、efC/ef、多seed、新query、54/64/80/128MB网格仍待执行。A1多入口/fiber anchor、JAG联合构图、部分scope共享、R1并集重跑未实施，不是“唯一剩余路线”。仍缺真实多标签层级/完整Exact路径/1M/动态维护/强作者系统同设置对照。

源码：`P/{baseline.cpp,les_build.cpp,les_query.cpp,prep_ccs.py,faiss_filtered.cpp,run_five_l2.py,run_b1_blocks.py,b1_calibrate.py}`。证据 `L/2026-10-20-exps/experiments/{les-prototype.md,exact-gap.md,five-l2-benchmark.md,les-closeout-ee90400.md,pareto-sweep-plan.md}`，`data/b1_{blocks_20261020.csv,blocks_summary_20261020.json,closeout_audit_20261020.json,archive_20261020_*,splits_manifest_20261020.json}`。该线旧指标/量化协议不满足当前原始FP32 hnswlib合同，不能直接转为当前动态结果。

## 12. 文献与新颖性：已知重合，不是“67篇全文核完”

相关问题背景：[TrieHI作者稿](https://arxiv.org/abs/2606.16903)。本地 `stage1-literature.md`/search-log记录动态19、多属性20、联邦28，共67篇/族候选；存在元数据、版本和未读全文缺口。本次没重新联网精读67篇，不能把历史措辞转成新验证。

| 议题 | 需要面对的已有工作/范式 | 当前不能宣称 |
|---|---|---|
| 分片路由/预算 | Taily、Rank-S、Dynamic Shard Cutoff、CRCS、向量分片路由、Quake/VADER等 | 不能因文本/flat与层级不同就说ANN不等预算首次 |
| 层级路由 | Lu–Callan、SHiRE、Hierarchical BM25、SHIRTTT等相邻先例 | 简单失败探针不否证方向；部分新稿仅题录 |
| 物化/过滤 | ELI、Curator、ACORN、UNIFY、Filtered-DiskANN | C7/临时scope过滤并非明显空白；本地适配不等于作者系统 |
| 共享图/范围 | iRangeGraph、UNG/LSSG、JAG、RNSG、Packing等 | 单绑定DFS可归约range；多标签scope非laminar；MST连通非新意证据 |
| 更新与并发 | EMA、Curator、FreshDiskANN、SPFresh、ChronoVec、湖仓snapshot/MVCC | 名字/软件README不证明性能；并发缺陷探针不能否认MVCC |
| ACL | Zanzibar等 | 授权集合/新鲜度/缓存并非无人研究 |
| 集合反包含 | LSH Ensemble/set-containment join×ANN待展开，UniFilter部分全文缺失 | 不能说没有先例或已不可能 |
| 量化/几何组合 | CAPS、iFVS、Extended RaBitQ、RaBitQ-SSD等 | 组合rank/cell/误差界不自动构成新贡献 |

历史修正：Filtered-DiskANN只定性讨论插入、删除留future work；FreshDiskANN未核实正式VLDB版；SPFresh记录SOSP2023；SeRF为SIGMOD/PACMMOD2024；MCI自述静态。新颖性同义词、引用链、工业多库/税onomies重挂、反包含等尚未充分排查。这里仅转述本地核查状态，不认证每篇完整内容。

总入口：`R/{novelty-analysis.md,overlap-refinement.md,alphaxiv-novelty-audit.md,directions.md}`、`E/stage1-literature.md`、`R/08-dynamic-hybrid-vector-search/search-log.md`；持久总索引 `papers/papers.jsonl`、`papers/relations.jsonl`。旧“唯一/没有任何工作/高置信空白”不能继承。

## 13. 代码、证据和复现地图

### 13.1 现行动态链路

| 职责 | 路径 |
|---|---|
| 三臂/谓词/状态/GT/tape/维护计量 | `C/src/dynamic_triad.cpp` |
| 唯一原始距离 | `C/src/raw_l2_space.h` |
| 构建/回归 | `C/build_dynamic_triad.sh`、`C/tests/test_dynamic_triad.py` |
| 合成层级/更新流 | `C/scripts/generate_dynamic_triad.py` |
| 原始完整子树/配对流 | `C/scripts/generate_wiki_topology_triad.py` |
| 两阶段调度/哈希/resume/质量门 | `C/scripts/run_dynamic_triad.py` |
| 通用/原树分析 | `E/scripts/{analyze_dynamic_triad.py,analyze_wiki_topology.py}` |
| 历史图 | `E/scripts/plot_dynamic_triad.py` |
| 已执行命令与限制 | [动态复现](experiments/dynamic-triad-reproduction.md)、[原树复现](experiments/wiki-topology-reproduction.md) |

源数据：WIKI源见3.1；SIFT source/六变体manifest与逐次命令见D的生成器/复现记录。忽略目录不随git clone分发。当前manifest含本机绝对路径，换机器必须按同生成命令重建并核对hash，不能假定原路径可用。

正式manifest：

- `E/data/dynamic_triad/wiki-topology/manifest-wiki-topology-s20261029-q456-f7046c642b34.json`
- `E/data/dynamic_triad/wiki-topology/scale/manifest-wiki-topology-s20261029-q456-f34c73d7cae4.json`

正式输出：`.../wiki-topology/tape-main-ef1024/`与`.../wiki-topology/scale/tape-main-ef1024/`；各含config/commands/results/oracles及analysis。raw prefix记录在results中，对应 `E/work/dynamic_triad/wiki-topology/`。源审计为 `source-audit.json`、small/scale `provenance-audit.json`。历史inline是 `main-ef1024/`，不得覆盖或错当正式。

### 13.2 可执行入口和陷阱

当前checkout初始化固定hnswlib submodule、准备原始数据后，可构建 `bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh`。历史测量另存 `build/dynamic_triad_gt_tape`，避免覆盖旧binary；新构建默认binary也支持tape。历史3项行为回归及真实两阶段/损坏/resume控制已通过；本交接轮不重复跑测试。

本机已有manifest的一个新输出复测模板（不是本轮已运行的新实验）：

```bash
EXP=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/scripts/run_dynamic_triad.py \
  --manifest "$EXP/data/dynamic_triad/wiki-topology/manifest-wiki-topology-s20261029-q456-f7046c642b34.json" \
  --binary codes/directory-aware-vector-attr-search/build/dynamic_triad \
  --gt-mode precomputed --ef 1024 --k 10 --jobs 2 --repeats 3 \
  --tracked-root "$EXP/data/dynamic_triad/wiki-topology/new-replay" \
  --work-root "$EXP/work/dynamic_triad/wiki-topology/new-replay"
```

scale用其manifest和另一个新输出目录。请显式指定 `--gt-mode precomputed`：driver默认仍inline；显式ef1024，不能默认256再拿失败点计胜负。`--select`是glob/套件名，不是`--config-regex`；输出参数`--tracked-root`，不是`--output-root`。`--dry-run`只规划不生成hash/tape，未知值为null；`--resume`要求输入/binary/原始输出全部hash相符。不同源码/设置不能假复用旧结果。

本轮实际运行 `run_dynamic_triad.py --help` 成功，核实了上述选项；没有新增检索实验。GT模式审计只比较非计时行为；不能用其证明跨阶段时差全由缓存导致。

### 13.3 标签命名易错表

| 上下文 | 标签 |
|---|---|
| 当前动态 | A NodeANN / B PE-ONLINE / C PE-OFFLINE |
| 当前静态hnswlib | B0纯局部图 / B1小节点hybrid / B3 exact诊断 |
| 07 LES | B0可指exact，B4/PSG逐scope图，B1也指ef预算策略 |
| 更早churn | T标删 / C压实；不是动态C PE-OFFLINE |
| trace | B是批量ITEM移动，V不是embedding更新，R不是MERGE |

## 14. 交给下一模型的开放问题，而非既定路线

以下是待判断问题，按依赖列出，不代表收益排序：

1. **证据修正够不够？** 静态完整距离计数、D5尾部来源、球剪枝单位修正、路由分组数字需要处理到什么程度，才能用来选择算法？是否需要独立状态机oracle补共模风险？
2. **全局filtered ANN基线是否太弱？** 候选基数/停止、候选入口、位图/集合成本、pre/in/post-filter如何在同协议下加强；单改ef是否只是已知控制而非贡献？
3. **真正的层级联邦路由还有多大机会？** Oracle大scope机会与简单摘要有限性同时存在；应否先修inclusive摘要、找更好样本索引/预算机制、量化路由成本，而非沿用旧关闭结论？
4. **动态维护本身能否构成研究贡献？** 同步阈值策略、预算受限重建、base+delta、批量/冲突MERGE或并发版本协议，哪项有明确差异、可证伪收益和完整维护计费？不能先假定A高更新胜。
5. **全局/局部/热点混合计划是否值得？** 目前没有自适应选择/物化实现；需与ELI/Curator等充分区分，计入build、撤销、热点迁移和空间。
6. **HLG/LES等共享图是否重启？** 应先判断新颖性、强共享向量PSG/范围图对照、有限预算导航与完整动态成本；不能只证明连通或局部压缩。
7. **怎样扩数据/负载才有辨识度？** 当前162只有纯M/50%覆盖/一个seed/三个子树；更低Q/U、真实属性相关性、多次更新同ITEM、不同更新类型、全量/多seed、热点迁移、并发应按假设选择，避免盲铺笛卡尔积。
8. **论文目标是什么？** 新索引、新搜索计划、维护协议还是动态策略实测？现证据能支持后者的受限研究，尚不能认证新索引优于公开基线；也不能凭负结果倒推出领域没有研究空间。

建议下一模型给出：先纠正哪些事实；保留/暂停/重启哪些方向及证据理由；最少的判别实验与失败判据；公共基线/相关工作缺口。**不要求接受上一模型推荐的唯一下一步。**
