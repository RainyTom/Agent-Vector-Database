# WQR c1c24e8阶段收尾：保留停止决定，纠正证据强度

## 1. 决定与责任

**停止冻结WQR的扩规模和追正调参。** 研究端提出的固定四proxy、支持域资格投影方案，在服务器报告的两数据/高召回/已测L网格下没有建立所需查询优势。它不再默认作为下一篇论文主线；保留代码、机制正增量与属性/目录解耦组件。不要把这轮负结果再归咎于8649e55已经报告修复的错误，也不让服务器自行改算法。

但“完整论文门槛全部实测闭合”“全损失都来自代表”“固有1.8×索引内存”“维护无隐性成本转移”强于现有证据。停止一个未达到预期的候选，不需要把负面归因写成全称定理。

来源：[固定c1c24e8报告](https://github.com/RainyTom/Agent-Vector-Database/blob/c1c24e87631c8b519caa32b2819c017d6347119a/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/wqr-implementation-report.md)及用户后续均值纠正。完整读取runner、网格脚本、E0及相关索引源码；三份C++快照由commit patch重建并核Git blob SHA。本机不重跑已报告实验或失败，未修改实现分支。

`e1_aggregate.json`和逐query/update日志仅在服务器work/，本地glob无该文件，GitHub提交没有加入小聚合，本工具环境无已配置SSH主机。因此数字按服务器报告/用户纠正归档，**没有独立由CSV重新聚合80次运行**。不能宣称已完成该层验证。

## 2. 可以保留的事实

- 服务器报告E0九组通过、80次配置运行零非法/重复结果，并完成原初稿阻断修复。状态从“有缺陷的初稿”更新为“已实现且已报告实测的候选”；有限E0通过不等于所有状态转换已形式化认证。
- 支持域gate相对BlockGate有可观察增量，不是无操作。是否在所有L都为正须按原数据表述：L=C时各臂相同，不能写“所有L严格提升”。
- 已测L为1/2/4/8/16，报告只有16通过.999门。严谨表述是“冻结网格中仅全块点达标”；**L=9–15未测，不能说所有L<C必然失败或最小必要L恰好C**。本轮不补扫这些点追正。
- 用户纠正的均值为6052/5323μs；按这些报告值算时间增加13.6953%，距离调用减少40.3178%，进程峰值RSS比1.8230。均值与6326μs的p50不能混比。
- V/T/M/R不触碰几何图的结构设计和所报短窗零距离是正面组件证据。B/R screening、未触发重建的动态窗口不是完整维护压力测试。

## 3. 必须纠正的基线和质量口径

### 3.1 dynamic_triad arm C不是NodeANN

既有[三臂协议](../08-dynamic-hybrid-vector-search/2026-10-29-exps/dynamic-triad-protocol.md)§2明确：A=NodeANN局部节点图/小节点扫描，B=PE-ONLINE，C=PE-OFFLINE祖先inclusive posting＋全局filtered HNSW。dynamic_triad.cpp的构图/查询分支也一致。

报告的5323μs、9440次和113MB既然明确来自arm C，应标为**PE-OFFLINE动态适配**。它是有用的对照，但不是已完成NodeANN主基线。当前提交的run_wqr_e1.sh也只驱动G/B/W/E，未提供A基线命令或结果。若已有A结果，只需补交；没有则写“本轮未测”，不凭改名填上。冻结方案的主基线合同未因此满足；这不阻止因机制门失败先行停止。

### 3.2 表中的macro recall与coverage不可能直接同口径比较

runner源码wqr_research.cpp:285,292对空GT分别赋recall=1、coverage=1（合法空输出）；对非空GT，结果只能来自选中块，因此逐query有`recall <= route_coverage`。同一查询集合、同一权重聚合仍保序。

报告WIKI W臂L=1的macro recall=.469而coverage=.337，L=8为.878/.848。若两者同为全query macro，这不可能成立。**可能是coverage仅统计非空GT，而recall包含空GT**；约20%空GT可解释表面差异，但这是代数推测，未见聚合脚本，不能当实测。

需要从既有CSV重算并明确：总/空/非空query数，全query macro，非空macro，pair recall，非空macro coverage及pair coverage，conditional local recall排除-1 sentinel后的分母与值。保留现有结果，不通过改阈值解释差异。

### 3.3 相同coverage不等于相同路由，更不等于local无损

index.h:1064–1095精确参考沿用相同gate、P/L/tie规则；runner:303–310只比较GT落入选块的数量。CSV保存reference_blocks但不保存在线selected_blocks或proxy序列。两组选块不同也可覆盖同样GT；均值signed delta=0还可能正负抵消。

所以窄结论是“按服务器所报逐query检查，没有观察到相对固定proxy精确排序的GT覆盖差异”。ef64与256相同只支持已测ef增大没有收益，不能证明所有可调运行因素无效。要排除local损失，应从现有`cond_local_recall`和同口径recall/coverage核对，不能仅凭refCoverage相等。

“损失全部来自proxy而不在zone gate”也应收窄：精确参考本身使用同一个zone gate，不能独立排除该gate与proxy打分共同形成的限制。硬资格投影不丢每个合法对象的唯一代表，不保证截断排序会选到它。将失败对象称为**冻结的代表＋支持域gate＋前L块路由策略**更准确。

## 4. 时间、空间与维护的边界

### 4.1 时间是已报告执行上下文下的观测

runner:238–247先计时`index.query`，之后才做exact-reference和GT；没有直接把oracle计入当前查询e2e。但run_wqr_e1.sh:34使用14进程并发，且无CPU绑定/隔离记录。其他进程的离线参考、GT校验及输出仍可与被计时搜索争用资源。

因此13.7%是所报运行环境下的均值差，不能直接当作固定结构导致的隔离慢速比例；不需要重跑来否定或确认该观测。没有组件计时/profile时，“192条bitplane和16个heap吃掉全部距离节省”是机制解释候选，不是已定量分解的事实。位切片实际执行量还取决于启用字段，不能把192条固定相加当每query实测。

### 4.2 1.8×是进程峰值，不是固有索引空间下界

runner:123保留`const auto base`，:168按const引用传入build，index.h:865又执行`vectors=base`。在该进程中，除了index canonical与图payload，还保留**一份额外loader base**。WIKI这份数组为11869×1024×4=48,615,424 bytes，属于可避免的拥有权复制，不是WQR冻结设计强制要求的第三份原始向量。

runner:425–431读取整个进程生命周期ru_maxrss；包括加载、构建、GT cache、exact参考、workspace等。:409–424显式小计用indexFileSize和部分metadata，不是完整常驻空间，亦未穷尽capacity、容器和临时内存。基线runner保留一份base，而非同样再复制到index.vectors。

206/113MB观测保留，但报告“常驻索引1.8×且全是结构固有成本”必须撤回。**不得直接从206MB减48.6MB伪造修正RSS**，峰值发生时刻和其他分量尚未拆分。修复副本也不自动挽救L<C的质量结果；本轮停止决定不变。

### 4.3 G2成立的部分与未测部分分开

V/T/M/R不动图可以保留；不触图不等于总维护免费。报告明确E2窗口未触发local压实/proxy重建，R仅5次、B仅3次。因此不能从E0功能通过推导出长期churn峰值、重建尾延迟和窗口末债务全部过门。

`state_errors`在runner:282等于invalid+duplicate，不是独立状态机全字段一致性计数。canonical标量谓词/parent-walk与位图/treap求值独立，但使用index.items/parent，不是另行重放更新的shadow state。可称“当前状态下的资格交叉核对”，不能称所有操作效果独立认证。此处收窄证据，不凭空认定已测输出错误。

### 4.4 新发现的split容量缺陷已单独复现，不与E1路由失败混为一谈

index.h:751–781在旧块live=2048、加入新对象后，对2049个对象中位切分得到1024/1025两个子块，却均调用`buildBlockFromIds(..., B0)`分配1024容量。:646–655按容量分配slot/bitplane，再无检查写入所有对象，因此较大子块越界。

本机仅构造合法的2048对象满块，再经公开`apply_I`插入第2049个对象；未运行服务器原E0或性能网格。在未修改的c1c24e8头文件和真实hnswlib下，`-D_GLIBCXX_ASSERTIONS`运行输出`before insert: live=2048 capacity=2048 incoming=2048`，随后vector<size_t>索引边界断言失败，exit134。这是**新确认的实现缺陷**，不是以测试遗漏代替失败。未加检查的E0程序返回pass与越界写可能并存，不能用pass证明此路径安全。

只对已触发该split路径的运行质疑生命周期正确性；先用已有updates.csv的splits字段确定影响范围。若E1未触发，不能用该缺陷推翻其固定代表负结果；若触发，该run必须标记受影响，不能仅因invalid=0就继续认证。

此外runner:380–385的维护distance_calls只累计build_space，计算出的graph gcalls未使用且没有事件前基线，漏掉插入路由/图插入/重建距离。这不抹掉计时器内已经付出的墙钟成本，也不否定V/T/M/R本身没有图操作，但**维护距离总数并不完整**。丢失的事件级计数不能从已有CSV精确补造。

## 5. 研究判断

这轮最有价值的认识是：**支持域中存在合法对象，不等于固定代表到query的距离准确反映该域中最近合法对象。** WQR确实改变了准入集合，但没有随谓词改变代理向量或控制合法近邻与代理的失配。高召回下，一小部分被错误排后的块就足以使.999失败。

本次失败不否定Trie、bitmap、动态Euler、全部分区路由或所有条件表示；也不提供它们可独立成论文的证据。当前保留组件：精确资格接口、固定几何与逻辑归属解耦、机制对照和已修复部分；生命周期代码须先修上述split边界，不能作为已完全验证组件复用。撤下默认主线：固定四proxy＋固定支持域＋纯proxy距离前L块截断。

不自动改成r=8/16、自适应代表或扩大N。若研究端后续重开该族，需要重新给出针对上述失配的具体机制与误差/预算依据，而不是把增加代表数命名成新索引。本轮不承诺另一套尚未设计的新算法，也不将研究问题重新交给下游。

## 6. 发给服务器：证据收尾和定点修复，不新增性能网格

1. 停止扩规模和WQR参数扫描；不重跑已报告失败，不改算法追正。
2. 更正文档中的arm C名称为PE-OFFLINE；若未跑A，明确NodeANN未测。用均值对均值，保留p50为独立列。停止“全体L<C”“全部损失”“固有1.8×”等过强措辞。
3. 从已有80份结果导出小体积`e1_aggregate.json/csv`、聚合脚本、每run完成状态、命令/硬件/并发信息、输入/trace/GT和二进制SHA。保留大CSV在服务器，但将小聚合放入当日data/并提交，不放ignored work/当唯一证据。
4. 明确聚合分母，导出每配置总/空/非空query数、macro/pair recall、两种coverage、conditional local recall及其有效分母、signed delta的min/max/非零计数。从已有CSV即可完成；没有的在线proxy/选块序列明确未保存，不为补它另跑。
5. 核已有日志的最终queries/updates/summary尾部字段，不能仅看`ALL E1 RUNS DONE`或summary非空。脚本child bash不继承父set-e，失败后echo可能返回成功；非空summary又在replay开始前写入。这是批处理完成证据的风险，不是说已报告80次实际失败；从现有文件核实即可。
6. 维护窗口标注哪些回收/重建未触发、维护距离计数漏图内调用；空间标作进程峰值RSS并列出未计入结构小计的副本/元数据。不要捏造“扣副本后的实测空间”、完整维护距离或隔离性能。
7. 定点修复split子块容量：按实际child size和既定增长规则分配，1025对象不能放入1024 slots。只需此路径的失败前/通过后正确性回归，不重跑80配置追正。用已有split计数标记受影响run；容量修复不改变研究算法。
8. 输出修订report、数据manifest、小聚合与边界修复，提交推送。当前目标是可追溯收尾，不是重新选择路线、重做全套E0或进行新性能优化。

## 7. 本机实际完成与限制

执行了报告数字的比例计算及1296组有限集合构造验证`recall<=coverage`同口径不变量；另构造相同GT coverage但不同选块集合的反例。来源和计算在[data/wqr-c1c24e8-closeout.json](data/wqr-c1c24e8-closeout.json)，复现片段见[记录](experiments/wqr-c1c24e8-review.md)。三份源码blob核验通过；另编译并执行一次新split边界探针，未重跑服务器E0或性能网格。新增真实数据ANN实验0、实现改动0；新合成边界诊断1，复现越界退出134。

对E1统计结论的独立复核仍缺服务器小聚合与聚合程序；不因此撤回用户报告的负结果或要求盲目重跑。研究端状态更新为“已测冻结候选停止，证据收尾待补”，而不是“证明整个方向不可能”。
