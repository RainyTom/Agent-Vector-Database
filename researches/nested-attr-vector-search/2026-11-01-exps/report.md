# 2026-11-01｜a1db223定位复核：固定执行器可取，不能把逻辑交叠等同物理冗余

## 1. 结论

接受以DAG语义层为研究对象、固定ANN执行器的分层方法；不要求发明新的HNSW才能构成贡献。新的闭包表示、图成员组织与查询算法的组合可以成为索引贡献，但不能通过重新命名基线消除端到端比较责任。

此次核对发现三项影响下一步的修正：

1. **12,913/168,937=7.6437%是多父暴露比例，不是已测的树化漏答率。** 结构统计没有canonical parent策略、scope集合差或查询GT差。
2. **a1db223的B0/B1尚不是固定NodeANN局部图的现成对照。** 实际online/mat_all/mat_select走同一全局HNSW；partition_top才走分区图。报告中的五臂是目标骨架，不能说只加B2/B3就全部具备。
3. **当前每个对象只有一个direct label和一个物理分区，物理成员不重叠。** DAG逻辑scope交叠可减少重复scope展开/元数据取得，但不能仅凭交叠安全跳过含独占合格对象的图。

本轮只核读既有证据和代码、运行有限语义反例，没有新增ANN性能实验、没有替服务器重跑已报告数据、没有合并其分支。

## 2. 原文与数据的可采信范围

### TrieHI

已核本地原文 `papers/prefix-vector-search/2606.16903-directory-aware/paper.md` 985–1055、1806–1821、1860–1867：

- §V-A确实比较PE-ONLINE、PE-OFFLINE、TRIEHI，并在PG与IVF执行器上分别固定后端，报告directory-only与端到端等指标。
- WIKI-Dir确实选择每entry一条路径，构建canonical tree；ARXIV-Dir描述subject/temporal独立namespace。
- related work承认选择率规划与filtered ANN；future work包含mixed semantic/hierarchical cost-aware planning。
- 这支持“比较目录/元数据层是合理研究方法”，不支持“过滤ANN不适用”“不用任何适配同语义的强对照”或“所有图索引性能都无关”。

原文链接：[Directory-Aware Query and Maintenance](https://arxiv.org/abs/2606.16903)。本轮未新做领域查全，不认证DAG或planning是首次问题。

**两处数学措辞要收窄**：

- `Inc(v)=Local(v) ∪ ⋃ Inc(child)`同样适用于DAG；集合并自动去重。树特有的是唯一父路径和某些树局部维护方式，不是这个递推式。
- 保留单路径的canonical tree一般不能保持任意DAG scope；但复制路径/节点的树展开并保留对象ID可以表达同样答案，代价可能是冗余与膨胀。因此应写“TrieHI当前单canonical路径模型未覆盖多父语义”，而非“任何Trie/树表示都无法表达DAG”。

### SciHTC/CCS结构统计

来源：[scihtc-dag-structure.json@a1db223](https://github.com/RainyTom/Agent-Vector-Database/blob/a1db223/researches/nested-attr-vector-search/2026-11-01-exps/data/scihtc-dag-structure.json)。

已读数字：1929概念、2040边、103多父概念；186160行中可用168937，含`~`的17223行排除；12913篇继承集合触及多父，12446篇terminal本身多父。每对象仅一个terminal label，多标签对象未建模，new_ann_runs=0。

可以写“多父语义不是空问题，有7.64%的可用对象暴露于多父继承”。目前不能写“恰好7.6%的对象会被树化漏掉”或“查询召回损失7.6%”：

- 冗余边可以有多个父，但选取适当单父后祖先集合不变。
- 即便对象丢失某个祖先关系，根scope或不涉及该父的查询仍可能完全不变。
- 全对象资格损失与top-k Recall损失不同，后者还依赖查询向量、k和阈值附近对象。

下一次可仅用现有元数据补三项：选父规则；`A_D(x) \ A_T(x)`的对象数/关系对数；实际查询的`S_D(q) \ S_T(q)`。这是小型语义诊断，不要求建图，不新增数据集。

## 3. a1db223代码实际做到什么

源码：[dag_scope_ann.cpp](https://github.com/RainyTom/Agent-Vector-Database/blob/a1db223/codes/directory-aware-vector-attr-search/src/dag_scope_ann.cpp)、[dag_scope_common.h](https://github.com/RainyTom/Agent-Vector-Database/blob/a1db223/codes/directory-aware-vector-attr-search/src/dag_scope_common.h)。

| 组件/策略 | 实际路径 | 研究解释 |
|---|---|---|
| online | 从预计算`graph.descendants`取direct postings，构造对象scope；搜索全局index | 对象成员在线展开，但概念闭包已经预计算，不是每query在线遍历原DAG |
| mat_all | 预物化非空inclusive bitmap，构造同样scope；搜索同一全局index | 与online是有效的元数据层同执行器对照，不能称现成局部NodeANN臂 |
| mat_select | 部分物化，其他scope回退展开；同一全局index | 预算启发式，不是已经完成跨局部图覆盖调度 |
| partition_top | 查询scope后枚举eligible ID，经part_of统计分区，搜索全部非空分区 | 使用另一组物理图，是几何组织变化，不能直接混入“只换元数据”比较 |
| tau | 默认512，eligible≤tau时exact_search | 这些点不是纯图查询；比较要固定相同策略或令tau=0 |

`run_query(..., s==3 ? &part_graphs : nullptr)`是区分执行器的关键调用。partition_depth取足够大可退化成每概念一个分区，但仍须把online/mat_all等接到**同一套**局部图上，不能仅改臂名称。

其他当前边界：

- Workload.query是多个概念scope的**OR并集**；未提供独立category/numeric/tag Extra。用户主问题仍是“层级多属性混合过滤”，DAG-only应标机制诊断，不可悄悄取代完整任务。
- `load`按固定seed重新打散terminal与object ID，对外部fvecs的语义对应没有自动保证。应披露是概念与向量无关控制，还是来自同一论文的自然配对；不因metadata来自SciHTC就自动称完整自然语义数据。
- 当前统计里的fanout是含eligible对象的partition数，**包括exact路径**；不是总等于实际图调用。
- `Stats.e2e`包含空scope；macro只对非空。最终速度表需从逐query CSV分出非空延迟，不能将两种分母混用。
- `search_graph`手工上层下降没有更新metric_hops，当前CSV也没有完整距离/全层hops列。八对象probe计数可以作为其独立证据，不能替完整runner自动补上指标。

[nodeann-grouping-work.json](https://github.com/RainyTom/Agent-Vector-Database/blob/a1db223/researches/nested-attr-vector-search/2026-11-01-exps/data/nodeann-grouping-work.json)明确是8对象2D机制例，真实legacy NodeANN CLI未运行、无wall-time：3图变2图，但距离调用仍7，不合格距离0变2。接受该局部事实，不重跑来确认，也不推广为所有分组失败。

## 4. B3必须满足的覆盖条件

设当前合格集合为`F(q)=Scope_DAG(q) ∩ Extra(q) ∩ Live`，物理图成员为`P_i`，选择图集合J。结构覆盖要求：

`F(q) ⊆ ⋃_{i∈J} P_i`。

若当前图成员是互不相交的direct-owner/分区集合，任何满足`F(q)∩P_i ≠ ∅`的图都不能仅凭元数据交叠排除。少查其中一张，就可让该图中某个对象成为最近邻而破坏答案。

因此必须分清：

1. **逻辑scope重叠**：同一后代概念可从多个父到达，适合闭包去重、factorization和共享元数据。
2. **物理候选覆盖重叠**：同一对象出现在多个可搜索索引中，才可能通过另一张图覆盖它而减少图数。
3. **几何可排除**：即便图有独占对象，也可用有效距离下界排除；这是不同机制，当前没有该证据。

不改变图成员、也不引入几何界时，B3仍可省**scope展开、eligible ID到图的映射、重复候选生成和调度开销**。这个组合可以是索引贡献，但不能承诺必然更少图或更少几何状态。

local top-k合并同样有边界：在每张选中图返回精确local top-k且结构覆盖成立时，并集包含全局精确top-k；这是经典充分条件。把某图配额压到小于min(k,E_i)，仅靠选择率通常不保真；所有全局前k都可能在这一图。efSearch是近似搜索预算，不是覆盖证明，HNSW local top-k也不是精确oracle。

## 5. 调整后的最小对照，不展开五臂大工程

认可“NodeANN作为共同执行器”，但baseline应完整命名成：

- **B0：DAG在线对象成员展开 + 固定NodeANN执行器**。
- **B1：DAG物化inclusive + 同一NodeANN执行器**。
- **B3：新DAG表示/调度 + 同一NodeANN执行器**。

三者必须共用相同图成员、图边、插入顺序、距离核、k/ef、候选过滤与小集合扫描规则。B3的目标仍是超过B0/B1系统的端到端效率，不是把NodeANN降为组件后无需胜出。

**B2放到元数据语义实验，不先建ANN臂。** 使用canonical tree自身GT可检验其内部正确性，同时以DAG GT报告语义覆盖损失；不同答案域的时延不能作为同质量竞品胜负。称“canonical-tree语义控制”，不要称完整TrieHI复现。

B4全局过滤HNSW保留历史参考，当前不强制重跑。若要使用现有全局online/mat_all作为第一步，也可直接承认它们固定的是全局执行器，先完成该层比较；不要花时间为匹配新命名而重建全部图。

### 下一步唯一小任务

在已有数据/图上，先比较**旧对象级映射与新概念级调度**是否得到同一个合法图集合，并分别计scope/dispatch/search/merge/e2e。单direct-label且图固定时，预计算概念闭包bitmap与concept→graph映射，可以在概念域消除重复scope展开；无Extra的诊断不必先枚举所有eligible对象来找图。有Extra时仍须真实求值，跳过图必须由完整资格的空交证据支持，不借用GT。

这是结构建议，**未实现、未获得新性能结果**。不要求它先减少图数，只要求在相同资格、相同搜索和相同质量下减少端到端成本。初次继续固定ef20，三遍驻留查询，一个既有数据单元；不启动维护或扩规模。后续是否有足够独立论文贡献，由与经典DAG闭包/bitmap、物化视图和查询覆盖方法的具体差异决定；本轮不新增查全声明。

## 6. 已运行的有限核验与复现说明

在Python中枚举4个拓扑有序节点的所有64种DAG（6条可能前向边的全部子集），每个概念直接放置一个不同对象：

- 对每节点检查`Inc(v)={v}∪⋃Inc(child)`与可达集合，共256项，错误0。
- 对每个非根选编号最大的父构造canonical forest。17种DAG存在触及多父的对象，但该选父策略没有丢失任何祖先成员关系，证明多父暴露不等于必然漏答。
- 单独构造菱形R→A/B→C，C上对象在选B路径后丢失A归属，但根scope无损。
- 实算两图跳过反例与每图配额1合并top-2反例；均得到与全局精确结果不同的结果。

数字仅为有限语义检查，不是SciHTC漏答数、ANN召回或性能实验。证据：[data/dag-positioning-review-check.json](data/dag-positioning-review-check.json)。复现：`itertools.product((0,1),repeat=6)`枚举`itertools.combinations(range(4),2)`边集；DFS可达定义inclusive；选每节点max(parent)；比较每对象祖先集合即可。无需服务器真实向量，不建立永久回归测试或新生产代码。

本轮新增这份独立复核及小证据，不覆盖a1db223的原报告或论文笔记。关键待补：实际canonical语义差、当前全局/局部执行器接线统一、保留Extra混合条件，以及真正的DAG组合机制查重。

## 7. 后续理论方案：DAG-CoPack

按用户要求停止继续铺设诊断矩阵，直接完成[共访分组与图内联合过滤设计](dag-copack-design.md)。方案不修改HNSW搜索核心：按层级scope共访分配共享图，用概念闭包、concept→block映射与图内owner/属性posting保留完整DAG资格。

本次给出了可计算合并准则、查询流程、精确局部搜索下的top-k合并证明及动态维护边界。合并准则明确权衡少启动图与新增scope外对象；不把它当成已验证时延模型。owner变化可以只改组内元数据，但布局可能漂移；DAG闭包与图重建成本没有省略。

此次追加未运行实验、未修改生产源码，没有新速度或召回数字。目标改为直接实施理论方案并看查询效率，不再先做信息oracle诊断；组合机制可作为贡献候选，新颖性仍未充分排查。

## 8. 用户约束更新：转向DAG逻辑索引

用户明确：不以物理组织找主创新，紧扣DAG属性特征，允许组合数据结构与索引，但代码实现不构成创新。故暂停上一节CoPack主线；这是定位调整，不是实现或实验失败。

新增完整理论方案[DAG-JBF：联合谓词双边界索引](dag-jbf-design.md)：不改物理图，用DAG偏序的极小/极大反链摘要属性联合子集，给出整个子集满足/不满足recursive scope的判据；与真实属性元组结构组合，避免AND条件的不同对象见证问题，输出互斥资格描述与精确计数。已写明非单调scope回退、边界最坏膨胀、删除包络与拓扑变化失效处理。

本次仅理论设计，无新增实验或性能结论；候选组合的新颖性尚未充分排查，不以代码或新命名代替论文贡献。

## 9. 多图合并瓶颈后的完整搜索候选

用户明确已有实验证明多图独立搜索合并比整图慢。本轮接受该观测，不重跑确认。DAG-JBF保留为过滤组件，不再将其单独称为完整新向量搜索方案。

新增[单队列编码搜索设计](dag-qualified-code-search.md)：将精确DAG/多属性资格用于全局量化编码Trie的前缀存活判断，所有owner共享一个best-first队列、一个候选预算和统一原向量重排。它没有多局部图独立top-k。已区分量化距离下界与原向量距离，未宣称原空间安全停止或已解决WIKI低维信息损失。

有限前序检索读到VA-Trie原始摘要，明确撤销“量化+Trie本身是新点”的可能主张。候选组合的新颖性未证实，暂无新增实验或源码。下一步的研究对象从单纯资格解析，转为资格如何改变几何候选展开；不以新缩写认证创新。
