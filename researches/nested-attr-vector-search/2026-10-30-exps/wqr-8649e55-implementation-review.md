# WQR 8649e55实现审计：先修复并闭合E0，再继续E1/E2

## 结论与状态

审计固定提交：[8649e55c3874b101a95edc0c2912601564790968](https://github.com/RainyTom/Agent-Vector-Database/commit/8649e55c3874b101a95edc0c2912601564790968)，分支wqr-implementation，父提交0837918。本地不合并或修改该分支的实现；仅提交审计、诊断及下一步指令。

**不能接受“WQR实现完成”或“E0完整通过”。** 可以接受“已提交可编译的部分实现，用户报告8对象fixture中执行V/T/M/R并得到3次正确查询”。本机未重跑用户的原fixture；提交仅修改build.sh、新增两个源码文件，没有提交该fixture、预期答案或E0验证脚本，无法独立核实其覆盖。

本机在该提交的原始头文件上运行新的反例，复现了硬资格、层级、ID映射、计数及生命周期缺陷，并在原runner上复现混合事件丢失。当前是**实现有缺陷且合同未闭合**，不是WQR研究机制已经失败。先修复、完成E0，然后不再询问是否继续，按冻结规格执行小规模E1/E2。

## 来源与复现可信度

从GitHub commit API取得完整新增文件patch，重建源文件并核Git blob SHA：

- wqr_index.h：`f93526dc4b8a62889ea5e09353f91a5623c55824`
- wqr_research.cpp：`6aed0cec179a4efdb6bdf07af641180e8bd95233`

源文件未修改；work/快照不提交。诊断使用现有公共raw_l2_space.h和已知固定revision的hnswlib副本。两名只读reviewer分别审核谓词/Euler及路由/runner/生命周期；他们未运行测试。下述“实际复现”来自父研究进程的新诊断，不来自静态评审推断。

初次ASan构建及加-no-pie的尝试都在进入main之前因shadow-memory映射冲突退出；**这是本机sanitizer环境限制，不记为被审程序崩溃**。随后改用UBSan和`_GLIBCXX_ASSERTIONS`执行。没有声称通过ASan检查。

[诊断代码](experiments/wqr_8649e55_review.cpp)、[完整命令](experiments/wqr-8649e55-reproduction.md)、[结构化结果](data/wqr-8649e55-review.json)。

## 阻断E1的缺陷

下列行号均指8649e55原文件。

| 优先级 | 源码定位 | 性质与后果 | 证据 |
|---|---|---|---|
| P0 | wqr_index.h:528–531 | scope通过时OR置位，重新放行已被类别/数值/tag拒绝的对象，违反硬资格 | 实测类别3查7仍放行；空tag overlap仍放行 |
| P0 | wqr_index.h:183–220 | 先插全部enter，再反序插exit，把兄弟目录错误嵌套 | 实测parent={0,0,0}时contains(1,2)=1，应为0 |
| P0 | wqr_index.h:718,858–864 | 图label是对象ID，callback却直接作slot | 实测仅含ID20/21的块，完整无过滤查询返回0个，应返回2个；高ID还可能越界，后者未运行 |
| P0 | wqr_research.cpp:119,139–143 | 第一条Q被读取后丢弃，之后的更新全部跳过，破坏事件顺序/状态 | 原runner实际输入Q/V/Q，只输出后一个Q且没有V记录 |
| P0 | wqr_index.h:937–940 | reserve ID的location无效，先索引blocks[UINT32_MAX]再判断 | 实测首次插入已有canonical但初始不活的ID，在checked vector处abort，exit134 |
| P1 | wqr_index.h:346–353,454–457,567 | numeric写入/读取bitplane顺序反转 | 实测OR且仅数值[0,10]，值5被拒绝 |
| P1 | wqr_index.h:447–448,559–560 | 单个-1端点被当无界，偏离现行闭区间合同 | 实测OR数值[-1,0]放行-2；此值编码回文，反例不依赖反序bug |
| P1 | wqr_index.h:703–706,835,881 | local计数对象未接入实际图；proxy计数不减基线，包含构建和前序查询 | 实测local返回2对象却报告0；两次相同query报告proxy4/7，真实本次增量均3 |
| P1 | wqr_index.h:928–948 | D不减live_count；恢复I强制zone0而非最近proxy/原正确归属 | 实测D后live_count仍2，应1；恢复zone从原1变0 |

P0为本审计“禁止进入性能归因”的等级，不是已经发现外部安全漏洞。

### 修复必须解决根因

1. **ExactMask：** 分开构造Live、Scope和Extra；Extra内部按启用字段做AND/OR，最后统一`Live & Scope & Extra`。不能只把一处`|=`换掉后继续使用原先已经AND掉字段的结果作为OR输入。当前OR分支被scope重置偶然掩盖，不把这个耦合错写成另一个已独立复现的OR错误。
2. **Euler：** 按真实DFS enter/exit事件顺序建token；兄弟节点必须在下一兄弟enter前闭合。保留cut后重算dst rank；不能改成每次R重新DFS以逃避规格。
3. **标签：** hnswlib external object ID必须通过块的id_to_slot查slot；不能靠重新给数据连续编号使反例消失。
4. **数值：** writer/reader采用一致MSB比较顺序；仅lo和hi都为-1时禁用，否则使用字面闭区间，不做单侧无界转换。
5. **计数：** 把counted space真正交给local图；按query前后差值或明确重置计数，构建/维护单独记录。不能用搜索返回对象数估计distance calls。

## 源码已证实但本轮没有独立运行的合同缺口

### Runner不是现行数据/事件协议

- `wqr_research.cpp:71–74`把现有ASCII bindings.txt按int64二进制读取；另要求category.f64/numeric.f64/tags.u64，没有读取正式attrs.txt中的alive状态。
- `:100`把对象owner数组作为目录parent传入，完全没有读取tree.txt。对象归属不是目录拓扑，即使数字碰巧可用也不是合法实现。
- `:146–147`的Q字段顺序不匹配dynamic_triad合同。正确顺序为：

  `Q qidx recursive anchor category lo hi tagmode tags combine`

- runner不验证声明事件总数和字段完整性；不能忽略未知事件。out流也未检查打开/写入失败。

应复用现有加载/事件语义；**不要把正式数据转换成当前错误runner的私有格式**。内部Euler允许用自父根约定，但必须从正式root=-1等canonical语义正确转换，不能用owner数组代替。

### 生命周期并未完成

- 虽然apply_B/apply_D/apply_I及dispatch都存在，但“有函数”不等于正确实现或完成测试；不能称B/D/I仅未测试。
- reserve首次I没有目标块选择；物理插入没有最近8个活proxy选块/最近zone。
- `slot_to_id`元素是uint32，初始化SIZE_MAX截断为UINT32_MAX，之后与64位SIZE_MAX比较找空槽不一致；编译已给出overflow警告。
- 缺少冻结规格要求的容量增长、满图压实、2B0+1临时集合split、墓碑重建、slot与registry回收、空块proxy回收及峰值空间计费。这些属于未实现，不假定已经实验失败。
- D/I没有完整状态前置条件校验；B没有按现行合同拒绝目标落在源子树内等非法操作。
- 空活集合构建也缺少分支：Partitioner返回一个空块，build仍选slot0并复制ids[0]（:722,737–739）。这是源码确认的无效访问路径，本轮未独立运行；须支持空索引及首个I建块。

### 构建/距离口径也偏离规格

- `VectorStore::l2`在:48–51手写scalar平方和，被分块、选代表和zone分配使用，违反所有raw距离复用公共内核要求。此项是源码合同缺陷；本轮未测其实际排序差异。
- `:722`假设slot0必为最小ID；按距离差分块后不成立。应显式按对象ID选farthest-first起点。
- runner硬编码L=4等参数，尚未提供冻结对照与L/ef矩阵；E1/E2和exact-proxy参考属于未实现，不是已有负结果。

## 发给服务器的继续执行指令

> 不在“先交付当前E0”和“直接做E1”之间二选一。当前版本先按缺陷审计修复并补齐E0，然后顺序继续E1/E2，不需要再询问是否继续。
>
> 1. 在wqr-implementation分支工作，保留8649e55原始版本及其局限，修订“实现完成/E0通过”表述。先读取本审计、de3180a的冻结规格和当前实施任务书，不能因实现分支缺少规格文件而自行缩减合同。
> 2. 优先修ExactMask、Euler、ID/slot、numeric和计数；改为正式tree/bindings/attrs加载及单循环按序replay。明确事件计数、版本、Q字段和两遍GT；不建私有有利输入格式。
> 3. 补齐B/D/I全部生命周期与回收/split，禁止zone0简化、新ID无效location、容量不足直接作为既定插入行为。完成现有规格，不加入新机制。
> 4. 用独立canonical标量资格与parent-walk对照；除原8对象fixture外，必须包括分叉树、内部owner、AND/OR、多tag/空tag、literal -1边界、负数与int64边界、非连续且跨bitmap word的对象ID、D后I和reserve首次I、对象数超过B0及2B0边界、proxy来源删除、全空资格和Q/更新交错。
> 5. 同一tape所有事件逐条执行；事件号/版本递增、查询数/更新数与声明一致。状态不一致先修，不报告为ANN漏召回。
> 6. E0闭合后运行原WIKI/SIFT小数据的NodeANN/GeometryRoute/BlockGate/WQR及Eager控制；固定数据属性、图和参数、先保存exact-proxy参考，随后执行动态窗口。不得重生成有利属性，不扩大规模、不另起编码路线。
> 7. 交付源码、构建、机器可读E0输入/预期/实际/独立GT、逐query/update证据、完整性能与空间维护口径及所有失败配置；项目约定提交推送。只有确实无法取得的数据或依赖才报告具体阻塞，先完成所有可达工作，不能因需要真实数据便停在3条fixture查询。
>
> 验收仍是原维FP32 squared-L2、精确硬资格、同质量macro/pair>=.999、invalid/duplicate/state=0。当前错误未经修复前，不运行可被误读为算法胜负的E1性能比较。

## 本轮结论边界

已运行的是不可变源码的故障诊断，不是修复后验证。没有在本地替服务器实现WQR，也没有WIKI/SIFT新实验；没有声称修复后的性能或论文可行性。1963μs只是用户报告的极小fixture构建时间，不能说明基线公平、动态成本或论文价值。
