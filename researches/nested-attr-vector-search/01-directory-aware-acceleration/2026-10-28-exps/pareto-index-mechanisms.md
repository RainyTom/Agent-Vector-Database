# 面向 Pareto 前沿的目录约束索引：机制筛选与可证边界

日期：2026-10-28。范围：single-binding 目录树上的单查询 squared-L2 top-k；recursive scope 含自身直接绑定，non-recursive 仅直接绑定。沿用原始向量、冻结GT、global ID tie-break、精确过滤及去重。不是 vector join。

**状态：研究推导与前序筛查，未实现或运行新索引。没有新 Pareto 性能点。** 本轮没有训练、编码、建图、查询或维护实验。离线准备优先GPU，正式查询CPU，实验由用户执行。

## 1. 先定义要改善的前沿

### 1.1 不能只打败原局部 HNSW

相同冻结 harness 的三个独立进程结果中，原局部HNSW正式run1为 recursive mean 中位数6.649231ms，观测macro/global recall为0.999559/0.999320；当前强扫描候选scope-run为1.127503ms，观测recall均为1。后者是已调过同一456条workload的静态候选，不是独立holdout上的泛化结论。

| 可比历史点 | recursive mean ms | p95 ms | 整进程峰值 MiB | 构建 s | 资源/能力边界 |
|---|---:|---:|---:|---:|---|
| 局部HNSW，正式run1 | 6.649231 | 24.175 | 19742.180 | 70.8704 | 同一CPU harness；局部索引净大小未单独计量 |
| scope-run，保留run11 | 1.127503 | 3.547671 | 16031.922 | 5.35924 | 静态single-binding；无MOVE/MERGE；浮点提前终止未作所有输入形式证明 |

来源：[正式迭代CSV](data/autoresearch_iterations.csv)、[最佳候选完整记录](data/autoresearch_best_summary.json)。原始两类DSQ基线及单次/三次口径见[前轮量化调研](igp-quantization-assessment.md#2-基线到底完成了什么)。

以上是**进程峰值**，不等于查询常驻内存、净索引或向量payload。原global HNSW/IVF的完整同召回网格未跑完，不能称现有记录已经构成完整Pareto前沿。

### 1.2 三种评价分开

1. **质量约束下的查询前沿**：固定raw GT，recursive macro与global recall均≥0.999，non-recursive recall=1；比较 `(CPU mean/p95, steady search RSS, owned index bytes)`。必须额外报告实际recall，不能藏掉0.999与1的差异。
2. **严格原始top-k合同**：另列所有输入上的候选覆盖、确定距离界、排序/tie和终端精度要求；“本次recall=1”不等于理论精确。
3. **维护/构建前沿**：训练、编码、H2D/D2H、构图、序列化、启动单列；动态前沿要求MOVE/MERGE语义和更新后检索正确。未支持维护的静态候选不能与TrieHI的完整动态能力混为一条优势曲线。

一项新方案可以是“更省内存但稍慢”的新非支配点，不必同时打败所有指标；若说**支配**某基线，则必须在同质量、同资源定义、同能力合同下各指标不更差，至少一项更好。随机噪声与不同测量次数也不能当作支配证据。

## 2. 前序审查改变了候选优先级

### 2.1 核心直接前序

| 原始来源 | 本轮核到的机制 | 不能再直接称为创新 |
|---|---|---|
| [CAPS，arXiv:2308.15014v1](https://arxiv.org/html/2308.15014v1#S5)，2023预印本；正式发表书目本轮未核实 | §5.1–5.3、Algorithms1–2：几何partition后按属性细分；CAPS-BLISS2用filtered-neighbor labels学习partition | 几何×属性双层分区、属性感知训练和过滤后partition搜索的宽泛组合 |
| [Extended RaBitQ，arXiv:2409.09913v1](https://arxiv.org/html/2409.09913v1#S4.SS2) | §4.2：先扫最高位，不能排除才读其余bits，复用中间内积 | bit-plane渐进读取与延迟精化本身 |
| [RaBitQ-SSD，arXiv:2610.02652v1](https://arxiv.org/html/2610.02652v1#S4.SS3)，2026-10-02 | §3、§4.3 Scheduled：RAM部分码、候选按页聚合、全局LB优先队列、发起读前以最新cutoff再筛 | 全局精化队列、块级调度、阈值变紧后延迟拒绝本身 |
| [iFVS，arXiv:2607.22922v1](https://arxiv.org/html/2607.22922v1#S2) | 固定PQ codes、query+filter条件码本/评分 | 目录条件校准的宽泛主张；见前轮专项核查 |

这些论文的完整作者顺序、版本、已读章节与待核事项将在本轮日期化source audit中记录；这里只摘与本机制直接相关的内容。不是全部算法已精读或已本地复现。

### 2.2 背景与不可混用的前序

- [Curator](https://arxiv.org/abs/2601.01291)本地正文§3.1–3.2：共享hierarchical k-means、per-label ID buffers与Bloom，无需复制向量；[iRangeGraph](https://arxiv.org/abs/2409.02571)已有区间族共享图。DFS-to-range不新。
- [ADSampling](https://arxiv.org/html/2303.09855v1) §3与§5.2：渐进维度比较、首批/其余数据分离；其概率错误和gap依赖不能省掉。部分距离非负提前终止则是确定性PDScanning基线。
- VA-file原始出版信息已核，但原始PDF正文仍未成功精读；不能编造其Algorithm/定理编号。桶box界在下文独立推导，不冒称原文逐字结论。[原始出版入口](https://sigmod.org/publications/discs/1999.1/disc/vp_a_quantitative_analy.htm)。
- [FCVI](https://arxiv.org/html/2506.15987v1) §3.1明确把filter matching推广为连续相似度，Algorithm1重排使用vector/filter混合分数。它是相邻背景，**不能不改语义就作为本项目严格scope过滤+原始L2的直接替代**；不能引用其速度声称同问题前沿。

## 3. 保留的统一工程引擎：几何cell × 目录区间

此处是强工程候选，不先宣称学术新颖性。统一表示和查询规则，不按数据集大小/选择率在多种索引之间切换。

### 3.1 结构

1. 在corpus上建立一个全局coarse quantizer，给每个对象一个固定cell符号 `g(i)∈[0,K)`。GPU可训练/assignment/编码；不使用测试GT训练。
2. 按目录DFS把对象排成逻辑序列 `A[0..N)`；一个目录的recursive scope是 `[l_u,r_u)`，local直接绑定区间是其前缀 `[l_u,l_u+n_local(u))`。
3. 每个cell的posting按该目录顺序排列；低比特codes只有一份，对象原ID可用于返回与raw精排。**不为每个祖先保存向量或码副本，不分配m×K的目录–cell端点表。**
4. 简单对照用每cell两个二分查找；输出敏感对照可用cell符号序列的wavelet/rank结构定位非空cell及其posting子段。后者是既有序列/范围数据结构，不能仅改名立项。

### 3.2 精确裁剪引理（独立推导，非新贡献）

对cell z，令 `rank_z(p)=#{j<p:g(A[j])=z}`。目录scope `[l,r)` 在cell posting中的合法对象恰好是连续区间

`[rank_z(l), rank_z(r))`。

证明：cell posting就是从全DFS序列稳定筛选出符号z；位于l之前和r之前的对象个数分别是两端rank，筛选不会改变相对顺序。所有cell区间互不重叠且并集恰为scope，所以过滤/去重可精确保证，含内部节点绑定、空scope和非递归模式。

这解决**范围覆盖**，没有保证“只probe几个最近cell”还能覆盖top-k。若为了精确停止，可以用实际覆盖所有cell对象的区域界；若用固定nprobe/beam近似路由，则单独报告候选召回损失。

### 3.3 查询规则

- 获取目录区间，按rank剪出非空cell子段，不遍历每个目录节点、不逐节点调用独立ANN。
- 以合法候选建立全局top-k cutoff；cell/块/对象可以按有效下界排序或streaming处理。
- 只访问scope内对象；有确定界时排除不可能进入top-k者，其余继续读更高精度或raw。
- 完全相同的覆盖和终端精度下，先比较streaming立即精化，再比较块级LB延迟调度；不要一开始叠加多层planes、双队列和新路由，造成机制不可归因。

### 3.4 空间账（解析模型，不是实测）

N=1,941,679、d=1024、K=1024，十进制MB。模型暂计一份uint32原ID、静态wavelet理想bits、FP32中心及逐cell逐维实际残差min/max，未计目录catalog、rank额外结构、工作区、raw和构建峰值。

| 最大常驻码精度 | codes MB | 上述codes+元数据 MB |
|---|---:|---:|
| 2bit/维 | 497.070 | 519.847 |
| 4bit/维 | 994.140 | 1016.916 |
| 8bit/维 | 1988.279 | 2011.056 |

**渐进读取不等于渐进占用**：若2bit基础层和其余6bit都放RAM，总码量仍按8bit计，额外planes约1491.209MB，不能继续声称索引仅0.5GB。若尾码/raw放SSD，应报告冷热I/O与实际resident pages；若raw常驻，codes是额外内存。精确原始距离所需raw不能在RSS账中消失。

wavelet理想bits仅约2.427MB，不是整个动态序列的成本；对象locator、原ID、原始码/向量地址需要几个映射数组，必须按实际实现逐项计量。

## 4. 下界、精化和保证

### 4.1 确定box界与概率估计界必须分离

若真实原坐标满足 `x_j∈[a_j,b_j]`，则实数算术下

`L_box(q,x)=sum_j max(a_j-q_j,0,q_j-b_j)^2`，

`U_box(q,x)=sum_j max((q_j-a_j)^2,(q_j-b_j)^2)`（所有端点有限时）。

它们是到**可能区域**的界，不是重建点ADC距离。残差分位桶的最外侧可能无界；有限UB需真实全库覆盖的range或overflow表示，不能使用训练样本min/max冒充。模型中的逐cell真实min/max正是为此计入。GPU离线求范围仍须保证其覆盖与保守舍入。

若保存 `rho_i >= ||x_i-x_hat_i||`，则球界为

`L_rad=max(0,||q-x_hat_i||-rho_i)^2`，`U_rad=(||q-x_hat_i||+rho_i)^2`。

不能写成“平方距离减半径平方”。每精度层用各自有效误差，不能把8bit半径套到2bit重建。box和球没有一般的统一强弱关系。

RaBitQ、Extended RaBitQ和Split-RaBitQ的估计误差界是**概率界**；不能当作对冻结索引和任意查询永远成立的确定证书。RaBitQ-SSD默认更紧的alpha-quantile阈值还主动牺牲部分候选；精确合同不得照搬。

### 4.2 全局cutoff的充分正确合同（独立推导，经典性质）

- 全scope每个对象处于“未展开但被有效区域覆盖”“持有有效对象区间”“已精化/返回”“已安全排除”之一；有限beam漏掉的对象不能从证明里消失。
- 任意k个不同合法对象的有效UB的第k小值可作tau，真实第k距离不大于tau。取得raw距离能进一步收紧。
- 只在 `L_i>tau` 时直接丢弃对象；相等需要按全局ID处理，不能随意改成 `>=`。
- 所有未返回对象的LB都无法超过返回集合中最坏有效UB对应的顺序时，可确定成员；若还需精确排序/距离，继续精化返回对象。

一个简单的anytime证书：若返回集合R含k个原始距离已知的对象，所有未返回scope对象均有有效LB，令 `L_*` 为其最小值，则R中距离严格小于 `L_*` 的t个对象一定属于真实top-k，因此recall至少t/k。证明：没有未返回对象能排在这些对象前，而R里至多k-1个其他对象。ties须用距离/ID字典序处理。这个证书不是新颖性主张；只覆盖已建立**全域覆盖与确定界**的执行，不能套到普通ANN的已访问队列。

### 4.3 有损码的不可辨识反例

若一个量化桶包含0.1和0.9，给两个不同ID都保存同一个桶码。原数据可以是 `(ID1=0.1,ID2=0.9)`，也可以交换两值；两份码完全相同，但q=0的真实最近邻不同。因此固定有损码本身不能对任意数据/查询保证原始top-k。8bit读全后仍有损也一样。可行终端是raw、无损尾码，或查询区间已经足以分离；不能把“读完所有有损bits”写成恢复原始距离。

**最坏退化**：所有对象下界都很松或top-k margin任意小，最终仍可能扫描/精化全部scope，再额外支付码扫描和队列成本。比如只知sign的box均包含原点，对q=0所有LB为0；这是box-only反例，不能推广为带真实范数等所有增强表示都失效。

## 5. 真正待验的研究变量：目录负载感知的全局分区

这是**研究者综合提出的假设，新颖性尚未充分排查**，不是CAPS或IGP作者明确future work。CAPS已做filtered-neighbor监督partition；Curator已共享几何树。因此不能仅说“partition看属性”就算新贡献。

### 5.1 更具体的目标

令 `n_{u,z}=|S(u)∩cell(z)|`，`t_u=#{z:n_{u,z}>0}`。考虑一个全局训练目标

`J(g,C)=D(g,C)+lambda * F(g)`，`F(g)=sum_u w_u*t_u(g)`。

D是明确指定的几何/量化失真代理；w使用预先固定的scope-family权重，或独立训练期trace，**不能用已反复观察的456条测试GT选择w/调参**。lambda是离线建索引的全局参数，不是查询时按选择率换索引。

假设：在可接受失真/候选召回损失内减少scope的cell碎片，能降低定位、LUT、batch浪费和精化读流，从而比“先标准k-means再加filter”的强工程对照取得新非支配点。没有假定该目标一定比CAPS等已有属性感知学习更强。

### 5.2 增量代价引理及其边界

一次把对象x从cell a改到b，a!=b，只有其绑定节点h(x)的祖先scope成员计数发生改变。改动前的计数记n，则

`Delta F=sum_{u in Anc+(h(x))} w_u * ([n_{u,b}=0]-[n_{u,a}=1])`。

证明：新cell原来为空才增加一个非空cell；旧cell原来只有x才减少一个；其他scope不含x而不变。已有计数的前提下，Delta F求值/更新沿根路径O(h)。**这只是所定义代理目标的增量性质，不是全局最优训练算法或新查询复杂度定理。**

计数本身不免费：非零(u,z)最多 `min(m*K,N*(h_max+1))`，若使用稀疏结构还要计key/allocator；离线峰值计量后可丢弃这些训练计数。不能藏成m×K在线索引。

### 5.3 为什么t只是代理，而不是实际块读数

若每cell合法对象在posting里形成一段，物理块可容B个对象，则合法段长度n_z的触碰块数最多 `ceil(n_z/B)+1`。因此

`P_scope <= n_scope/B + 2*t_scope`。

它说明t控制一个过读上界项，**不证明减少t一定减少实际P或CPU时间**。改动一个对象的cell还可能改变整个posting后续对象的块对齐，影响不含x的scope；所以§5.2的O(h)更新不能直接宣传成“真实cache-block目标也O(h)”。

还有两个反例风险：

- lambda过大让所有对象挤到一个cell，t最小却破坏几何/残差质量和coarse剪枝；压缩与refinement成本可能反而上升。
- 若码扫描或高精度精化占总时间绝大部分，减少cell初始化仅改善小头。现有原始NodeANN目录枚举仅占1.36%，因此本机制必须证明新的压缩引擎确实存在fragmentation/读流瓶颈，而不是再优化已知小头。

只保留两个非零lambda探索点加lambda=0对照；不能无限扫参数刷冻结workload。必须与CAPS式属性感知partition或同等强度、同训练预算的对照比较，差别仅在分区目标，而不是换codec/距离/机器。

## 6. 动态目录：逻辑cut/paste，不声称免费MOVE

静态DFS数值rank或物理posting在MOVE后失效。不能因为payload没变就宣称索引已经支持动态目录。

一个独立待验表示是：目录enter/exit标记和对象token形成逻辑序列；对象cell符号与码位置固定，MOVE对应合法子树片段的逻辑cut/paste。动态rank/wavelet结构把scope映射到各cell子序列，cell内ID rope保存逻辑次序，移动不搬整份码payload。

**重要边界：**

- 这首先是动态序列/range-reporting数据结构组合，不是新发明rope或wavelet tree。
- 只有底层真的支持高效bitvector split/concat和稳定anchor rank，才可推导整段移动成本。若逐token删插或移动连续数组，仍可能与子树点数线性。
- 一个朴素可分析上界会触及O(K)个wavelet节点；每节点split/concat若O(log n)，则是 `O(K log n)` 级，而非O(log n)或O(1)。前置分支rank、cell ID rope与locator维护也须计入。此处是条件性设计上界，**不是已核实可用库的接口或实测复杂度**。
- K大、小scope、碎片多时，动态结构常数和物理跳转可能压倒压缩收益。理想bitvector bits远不等于动态树全部内存。
- MERGE的直接对象迁移、冲突节点递归调和、删除/插入也须定义；不能只支持MOVE便声称相对原基线完整动态Pareto优势。初始静态比较和完整动态比较分开。

暂不优先实现复杂动态wavelet：先证明静态压缩引擎的scope碎片/精化调度净收益，再决定动态层是否值得承担。

## 7. CPU成本模型：看实际读流，不只数survivors

理想读字节可写为

`B_q ≈ n_scope*h_meta + (d/8)*sum_l n_l*Delta_b_l + 4*d*n_raw + B_posting + B_queue`。

实际时间另有scope/rank、coarse、LUT、距离界、队列和随机访问成本。一次全候选heap排序可能引入O(n_scope log n_scope)，streaming则没有这一项，不能把队列默认当优化。

两个必须记录的放大因素：

- 每cell独立补齐32条batch的评分槽数为 `sum_z 32*ceil(n_z/32)`，可比合法对象数大近32倍；不能只算Ns个codes。
- 若额外planes按32对象块读取，独立存活率s对应块触碰率为 `1-(1-s)^32`。s=1%仍会触碰约27.5%的块。这是独立模型算术，不是本地实测或对任意布局的定理。

单级b-bit扫描后有r比例对象读raw时，理想流量相对原始FP32约 `b/32+r`。要节省字节至少需该式小于1；要加速，还需节省的读流覆盖rank/LUT/界/调度开销。不能由“16倍码压缩”推导“16倍搜索加速”。

## 8. 最小验证矩阵与停止门槛（未运行）

| 顺序 | 固定项 | 唯一主要变量 | 用途/否证 |
|---|---|---|---|
| A | 原输入、GT、CPU线程、资源定义 | 独立测原局部HNSW、局部Flat与scope-run | 建立steady RSS、净索引和同质量前沿；不把旧peak当新resident |
| B | 同一geometry cells/codec/合法候选覆盖 | cell内逐项selector vs sorted-range裁剪 | 得到真正强的压缩工程对照；已有Faiss能力应复用，不故意用慢filter |
| C | 同一码、range布局、终端raw/精度合同 | streaming立即精化 vs跨cell块级延迟精化 | 只有raw/cache块读取和总延迟都下降才保留调度 |
| D | 同K、bits、训练/编码预算、search引擎 | lambda=0 vs两个预注册非零lambda | 隔离scope碎片正则；若仅MSE/t改善、真实读流/recall不改善则停止 |
| E | 同精度与候选覆盖 | 先MSB再尾码 vs直接读全码 | 对照Extended RaBitQ，明确渐进读不减少总驻留bytes |
| F | 完整MOVE/MERGE与更新后查询合同 | 稳定token/动态rank vs重新物化 | 仅在静态机制通过后考虑；逐token更新不伪称子树规模无关 |

- 质量先过原冻结门槛；精确型另外验证候选覆盖、界、raw终端、浮点/tie。概率ANN与确定型分表。
- 先争取与scope-run相比≥15%的recursive latency下降且内存不增加，或steady总内存下降≥25%且latency不增加；这是**候选优先级门槛**，不是声称已经达到。其他有价值的非支配折中点也保留，但不能称全面支配。
- non-recursive、tiny/large scope、非空cell数、p95全部单列，不能靠查询权重掩盖退化。区分CPU启动/加载与warm/cold查询，精排raw的驻留/页缓存必须统一。
- 新增query/独立未看过的数据用于泛化；12轮已看过的456条不能再划分后冒称真正未见holdout。新GT可GPU离线准备并独立核对，已有冻结GT不重写。
- 新图、GPU离线训练、GPU-built CAGRA都单独记录来源与构建成本，不强求复现另一张图的0.2124。

**最先停止的情形**：大scope仍需精化几乎全部raw；survivor少却实际块读/latency不降；tiny scope初始化吞掉收益；只有漏cell、概率界或降低recall才快；收益只来自换codec/SIMD/布局而不是所研究变量。

## 9. 下一步边界

目前可交付的是上述机制、证明/反例、前序地图和验证合同，不是已运行的新索引。尚无这些新机制的集成CLI，因此不提供虚构`--wavelet`或`--lambda`实验命令。下一实现单元应先是**静态scope裁剪＋单级量化streaming精化**；按用户要求在实现及冒烟就绪后提供完整GPU离线、CPU搜索命令，由用户运行。

优先级：工程前沿用低复杂度共享表示争取，算法研究聚焦“分区目标—scope碎片—真实精化读流”的可归因差异。动态结构与多层全局队列不提前堆叠。没有任何部分被表述为已确认的新颖机制、次线性所有输入保证或已取得的Pareto改进。
