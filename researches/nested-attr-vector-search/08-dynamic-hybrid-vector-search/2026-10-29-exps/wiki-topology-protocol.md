# 原始 WIKI 层级：动态维护交叉的配对验证（2026-10-29）

## 1. 问题与事前预测

承接上一轮的唯一问题：**纯 ITEM 移动时，A（NodeANN 维护策略）与 B/C（PE-ONLINE / PE-OFFLINE 动态适配）的 Q/U 成本排序翻转，在原始 WIKI 层级上是否仍然存在？** 不是新索引/公开作者实现优越性实验。

- H1：上一轮 `(0.1,1)` 排序翻转区间不一定保留；若同一臂在新已测范围始终占优，报告没有观察到交叉，不外推一个阈值。
- H2：直接节点大小分布决定 A 是否真正维护 HNSW。若随机缩小数据后所有节点落到扫描阈值以下，不能把结果归因为“真实层级消除了图维护成本”。因此使用**完整原始子树**，保持其中每个目录的初始直接 ITEM 数，不做 ITEM 随机下采样。
- H3：在保持向量、直接绑定、节点数和更新流不变的拓扑配对中，A 的局部图维护事件计数应完全相同；C 的独有祖先成员写可以变化。若A计数不一致，先视为配对/实现错误，不用时间结果讲故事。

事前不指定哪个方案、哪类密度获胜。相关机制沿用 [动态三臂协议](dynamic-triad-protocol.md)、[基线定义](baselines.md) 和 [TrieHI 作者稿](https://arxiv.org/abs/2606.16903) 的问题背景；本轮没有新的文献新颖性声明。

## 2. 输入审计与确定性 cohort 选择

源：`codes/directory-aware-vector-attr-search/data/wiki_full/{base.f32,test.f32,wiki_tree.txt,wiki_bindings.txt}`。原始 binding 第i行对应 base 第i条向量；1,941,679条1024维向量、468,031目录、363,467非空直接目录，树最大深度31；148目录直接ITEM数>512。

选择规则在计时前冻结：分别在“没有>512直接目录”和“至少一个>512直接目录”两层中，选择**完整子树 ITEM 数最接近12,000**的根；平手按source node ID。不看查询或更新时间选择。

| cohort | source root | ITEM数 | 原始子树节点数 | 非空直接节点 | >512的直接节点 | 最大直接节点 | 相对最大深度 |
|---|---:|---:|---:|---:|---:|---:|---:|
| sparse | 410 | 11,964 | 2,382 | 2,117 | 0 | 284 | 14 |
| graph-present | 3626 | 11,869 | 151 | 143 | 2 | 3,843 | 6 |

这是两个**完整原始根子树 cohort**，不是全量 WIKI，也不是随机代表性样本；两个cohort向量语义与密度都可能不同，不能把跨cohort差异全归因为密度。根的原始绝对深度分别8/15；输出树截在cohort根，深度从0计，保留source ID映射及hash。

放大候选预留 source root828：48,403 ITEM、4,076节点、11个>512目录、最大直接节点3028；只有小规模正确性/质量门通过后才运行，不能提前写成已完成实验。源审计见 [source-audit.json](data/dynamic_triad/wiki-topology/source-audit.json)。

## 3. 配对控制：只改拓扑，不改直接节点占用

每cohort包含两份树：

- **native**：保留原始子树所有节点与父子边，重映射连续稳定ID；cohort根变为0。
- **balanced**：相同节点ID、相同节点数、相同直接绑定，但将parent(i)改为 `floor((i-1)/4)` 的4叉树。

两者共享同一批向量、属性值、所有潜在ITEM、逐条 M 和 Q 的字面事件流。因而不会像旧“8叶15节点合成树”那样同时改变局部图密度。A的初始/更新局部图应一致，B的单ITEM移动成员写数应一致；C的祖先差集维护不同。

**注意：** recursive scope的合法ITEM集合会随拓扑变化，不能声称两种拓扑的查询GT一样；root recursive与non-recursive的直接集合应相同。对照臂A/B/C在同一拓扑内仍须每查询GT完全一致。最终按实际scope/eligible规模和fan-out分层，不把不同合法集合的延迟直接解释为纯目录解析的速度差。

## 4. 维护、距离、计量：不改算法

维持 M16/efConstruction200/efSearch256/seed100、扫描阈值512、墓碑死比0.2、同一hnswlib `RawL2Space`；三个臂索引/搜索/GT仍为C++。只有新增计量分项：

1. 已有图、仍要图、死比触发：tombstone compaction；
2. 无图→图：threshold build；
3. 图→扫描：threshold drop。

三个分项计数与时间必须精确划分原 `compaction_*` 总量；一次触发只归入一类。分别计重建图插入次数。计量不能改变触发先后、建图顺序或移动插入后标删协议；不把阈值转换假称为纯墓碑压实。

## 5. 已冻结的主矩阵与查询负载

- 2个cohort × native/balanced × Q/U0.01/0.1/1 × A/B/C × 3次重复 = **108个计划运行**。
- 仅单ITEM移动；唯一初始ITEM覆盖率约50%，奇数N使用ceil，报告actual值。随机不放回选ITEM；目标均匀选初始非空直接节点且不同于源，避免悄悄把小节点全搬成大节点或填入作为空查询控制的目录。
- native/balanced使用完全相同的更新列表，各Q/U只插入不同数量查询；不共用query/update RNG。
- 发布的456个query vectors全部进入池，按ID轮换；低Q/U约60个查询事件无法用满456，必须报告actual distinct query vectors，不能把池大小冒充实际样本数。
- 五类query各20%：root recursive、宽recursive、窄recursive、当前非空non-recursive、保证不被移动填充的空non-recursive。宽/窄锚点按native初始规模桶选择并用于同一paired trace；各次查询记录当前真实cardinality。
- 标量/标签属性仍为合成（cat8、numeric0–999、16标签取3），四种tag集合语义与AND/OR独立于五类scope轮换。不是原始WIKI普通属性分布。

普通字段暂不更新，MERGE、子树重挂、向量更新、并发事务均不在本轮；不增加新维护策略同时改变主要因素。

## 6. 阶段门与输出

1. source行数/树边/绑定/向量形状、完整子树计数/原始ID映射一致；同cohort两种拓扑只有tree文件不同，trace完全相同。
2. 三类维护触发的行为回归通过；分项计数/时间/插入次数可加回combined总量；查询当前状态语义不变。
3. 小数据pilot先跑各cohort/拓扑/臂，逐查询原状态精确GT。macro/global pair recall≥0.999、invalid/duplicate/state violations=0；低质量失败原样保存。若ef256不足，另开所有臂统一ef1024对照，不做exact fallback或只给某臂偷偷升预算。
4. 主结果给查询、更新及总服务成本随actual Q/U的图，3重复SD；目录与ANN分开；给非递归非空、scope规模、fan-out分层，以及维护分项与逻辑写放大。
5. 不将服务时间和的吞吐称并发到达率吞吐。独立进程并行运行明确披露重叠，GT虽在计时外仍可能扰动下一查询缓存。公开论文只作背景。

执行命令、实测/失败与图追加到当日 `report.md`；原始CSV/trace/向量/映射保留在ignored work，小体积manifest/审计/结果提交。只有108运行完成与全部质量/配对检查通过，才把计划状态改为已完成。

## 7. 阶段门执行记录与已激活的扩展

- 真实源生成与独立逐FP32 bit核对通过；cohort保持全部原始子树ITEM/边，配对只改tree，字面trace相同。
- ef256的12个pilot中4个B/C点未过门：root410、query30的root scope有4228个eligible ITEM，漏一个GT结果。不是状态/谓词错误，也不能作为同质量速度胜出。失败点保留在 `data/dynamic_triad/wiki-topology/pilot/`。
- 按预登记规则，**所有cohort/拓扑/臂统一ef1024** 的12个pilot全部过门；主108运行使用ef1024。不能将本轮1024结果与上一轮256延迟直接相减后声称是“仅真实层级”的收益。
- 小规模正确性/质量门已通过，激活预留root828完整48,403 ITEM cohort，native/balanced × Q/U0.01/0.1/1 × 三臂 × 三重复 = **54个计划放大运行**。其向量语义/cohort也变化，不把它当严格固定数据的N缩放定律。
- 放大先使用统一ef1024做Q/U0.01的6个pilot；若不足质量门，另保留失败点，并对**该完整放大矩阵的全部臂/拓扑**统一ef4096重做质量门与正式测量，不给单臂加预算。此规则在读取放大pilot结果前登记；不引入精确兜底。

## 8. GT缓存干扰修正：两遍协议优先作为最终计时证据

发现测量风险：旧GT虽在计时外，但会读原始 `base` 向量；A小节点扫描复用该数组，HNSW则使用图内向量副本。因此旧inline-GT会不对称地扰动下一次查询缓存。**保留旧结果作诊断，不将其时间当最终公平性证据**；维护操作计数/GT/语义审计仍有效。

索引/搜索/维护算法不变，增加显式 `--gt-mode precomputed` 两阶段：

1. 每个data/trace/config/binary/k/settings身份，先用同一个C++执行器逐事件重放，**每次查询在当时状态计算精确GT**，保存ranked GT IDs/版本/scope/eligible等到真实queries CSV；不是静态初始GT，也不使用ANN结果作为GT。
2. oracle阶段完成后才测三臂三重复，追加 `--gt-in` 读取同一已核验tape；只在被测搜索/merge结束后读取GT，不用GT决定是否跳过、搜索k或候选预算。每查询继续独立核对当前canonical元数据/实际scope，但**不额外读向量计算GT**。

driver绑定data/trace/binary/tape/stamp SHA，cache改变拒绝复用；原oracle stamp复用时不重写。GT计算时间/距离调用另列在oracles.json，不把生成成本伪装为0或混进测量服务时间。测量summary与每查询的 `gt_distance_calls` 必须严格0，GT源调用必须真实存在。

两遍协议仍有metadata审计、文件I/O与多进程资源影响，**不是宣称完全消除了所有缓存效应或实现了隔离计时**。测量与oracle分阶段；本次小规模/放大driver顺序执行，每阶段最多2个benchmark进程，避免另一oracle阶段与正在测量的矩阵重叠。

原始3个cohort、两种拓扑、相同字节数据/字面trace、统一ef1024、相同三ratio、三重复，共**162个两遍协议正式计划运行**。结果必须与inline按event/version逐查询核对ranked GT/result IDs、ANN距离调用/hops、fan-out与更新维护计数；只有计时和oracle向量调用发生预期变化，不能偷偷改算法。旧inline源快照由commit `4dcf430` 保留；中断的放大诊断用该精确driver与原binary hash恢复，不拿新版driver配置假复用旧记录。

实际pilot已完成12个两遍测量：质量门全过，oracle共94051次真实GT距离调用，测量端共0次；4个oracle与12个测量的resume均成功。孤立变更的tape副本被SHA校验拒绝，原tape/stamp未变。另用低预算ef1（搜索至少k）控制，oracle的ANN recall约0.988未过门，但精确GT仍完整生成，测量的低召回失败被原样保留，证明GT不依赖oracle ANN召回；该控制不进入正式性能胜负。

## 9. 固定filtered HNSW的可定位弱点（不在本轮改变算法）

本轮固定的hnswlib版本为 `ca426729609b3221563047ceb269cc1213e0d376`。其 [`searchBaseLayerST`](https://github.com/nmslib/hnswlib/blob/ca426729609b3221563047ceb269cc1213e0d376/hnswlib/hnswalg.h#L343-L460) 在没有自定义stop condition的filtered分支中：

- 第379行只有结果堆大小达到ef，才允许距离界触发停止；
- 第425行结果堆未满ef时，所有首次访问的邻居均可进入扩展队列；
- 第438–440行只有未删除且满足谓词的点进入结果堆。

因此，若当前快照全图满足谓词的ITEM数C<ef，则结果堆始终不满；搜索会耗尽入口在底层有向图上可达的顶点，而不只是访问C个候选。该推论不假设整图连通，也不把距离调用数严格写成N：上层导航还会产生额外调用。root410的所有非空非递归scope最多284项，小于统一ef1024；其几乎全图访问有源代码机制支撑，不宜仅解释成数据集上的偶然慢点。

下一轮首先检验一个**更强执行器对照**：已知当前精确候选基数时，统一用于A/B/C的filtered HNSW能否避免“不可能填满ef”的过搜索。候选计数/谓词解析的全部成本仍须计入，继续使用HNSW与共同距离内核，不能改成大scope精确向量扫描；不能只替某一臂换停止规则。先检索已有filtered ANN的相同机制；这不是已证实的新索引或新颖性声明。本轮保留原算法，不能事后改ef再把收益归给维护策略。

## 10. 完成记录

正式两遍矩阵已完成162/162：small108、scale54；统一ef1024，全部达到macro/pair recall≥0.999，未启用4096回退。共721656次query、1950426次update；18次oracle GT距离调用114183079次，被测GT距离调用0。全部721656条query和1950426条update与inline的非计时行为逐行一致，81组拓扑配对检查无错误。

root3626两拓扑三重复对B/C均在实际Q/U [0.0099410278,0.1000842460] 夹区发生排序翻转；root410与root828在全部已测点A总服务成本更低，未观察到交叉。图与限制见 [报告§6.5起](report.md)，逐重复证据在两个 `tape-main-ef1024/analysis/crossing-audit.json`。这些是固定三臂的维护取舍，不是公开作者实现胜负或全量WIKI结论。
