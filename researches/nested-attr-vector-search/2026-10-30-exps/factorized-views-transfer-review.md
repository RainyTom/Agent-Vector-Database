## Summary

**建议优先迁移 F-IVM 的“按依赖键共享局部视图＋增量传播＋延迟枚举”，而不是把 filtered top-k 改写成一般 join 后套 WCOJ。** 对本任务，值得研究的共享对象是**目录子树—facet—geometry-cell 的局部资格表示及其更新依赖**，不是查询相关的 top-k 答案。

深读两篇：
1. **F-IVM 期刊扩展版（2024，在线2023）**：因式分解 IVM 代数框架及编译系统，附具体视图索引；源自2018年工作，必须标为既有基础，不能包装成2024年新发明。
2. **DBSP（PVLDB 2023）**：增量化代数/流计算框架及实现，不是专门的搜索索引。它尤其帮助划清“语义上可增量化”与“得到足够快的维护算法”之间的区别。

下面的迁移方案是 **[INFERENCE：我们的设计假设]**，不是上述作者的向量检索方案，也没有证明新颖性或性能优势。全程只读；未运行实验、作者代码或检查。

---

## Files / 原文与证据入口

| 材料 | 已读定位、用途 |
|---|---|
| [F-IVM正式PDF](https://link.springer.com/content/pdf/10.1007/s00778-023-00817-w.pdf) | 分析版本；§4–7、§8.3、§9、§11。PDF页码＝期刊页码−902 |
| [F-IVM正式HTML](https://link.springer.com/article/10.1007/s00778-023-00817-w) | 完整正文提取为 **`artifact://123`**，可供父任务归档 |
| `artifact://123:341-425` | variable order、view tree、delta规则 |
| `artifact://123:459-503,580-665` | 低秩更新、特殊查询类及定理条件 |
| `artifact://123:787-821,883-940,1011-1019,1051-1059` | 因式分解结果、实验协议、eager/lazy实验、局限与future work |
| [DBSP正式PDF](https://www.vldb.org/pvldb/vol16/p1601-budiu.pdf) | 分析版本；已通过PDF工具取得原文pp.1601、1604–1608、1610–1612；工具未返回独立artifact编号 |
| `researches/nested-attr-vector-search/2026-10-30-exps/index-mechanism-portfolio.md:25-38,65-79` | 已有Curator/ELI/SIEVE机制边界；此处作为本地既有审读，不冒称本轮另深读三篇 |
| `researches/nested-attr-vector-search/2026-10-30-exps/evidence/sieve-component-reuse-20261030.md:74-119` | SIEVE作者对集合选择、成本模型和serving贡献的原文节选 |
| `researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/pareto-index-mechanisms.md:169-178` | 动态wavelet/split-concat的已有条件与维护成本警告 |

### 完整元信息与版本警告

**F-IVM: analytics over relational databases under updates**  
作者顺序：**Ahmet Kara → Milos Nikolic → Dan Olteanu → Haozhe Zhang**。  
*The VLDB Journal* **33(4):903–929，2024**；在线 **2023-11-14**。DOI：**10.1007/s00778-023-00817-w**。预印本：[arXiv:2303.08583](https://arxiv.org/abs/2303.08583)，v1 2023-03-15、v2 2024-01-29；本次分析正式期刊PDF，未逐字比对v2。作者项目：[fdbresearch/FIVM](https://github.com/fdbresearch/FIVM)，未执行。

其2018前身为 **Milos Nikolic → Dan Olteanu**，*Incremental View Maintenance with Triple Lock Factorization Benefits*，SIGMOD 2018:365–380，DOI **10.1145/3183713.3183758**，arXiv **1703.07484**。这里只核关系与元信息，不算第三篇深读。

**DBSP: Automatic Incremental View Maintenance for Rich Query Languages**  
正式版作者顺序：**Mihai Budiu → Tej Chajed → Frank McSherry → Leonid Ryzhyk → Val Tannen**。  
*PVLDB* **16(7):1601–1614，2023**。DOI：**10.14778/3587136.3587137**。  
预印本：[arXiv:2203.16684](https://arxiv.org/abs/2203.16684)，首发2022-03-30。**重要：本次arXiv API返回四作者，不含Tej Chajed；不得把该元数据替代正式版五作者顺序。** 分析依据正式PDF。作者artifact入口：[vmware/database-stream-processor](https://github.com/vmware/database-stream-processor)，未执行。

---

## Architecture：两篇实际共享什么

### 1. F-IVM：不是“把posting放进Trie”这么简单

**输入/输出。** 输入为有限支持的关系、自然连接与group-by aggregate查询、ring定义及insert/delete流；关系/视图为 `key tuple → ring payload`。输出可为聚合结果，也可为分布在视图中的无损因式分解查询结果及其枚举。实现由variable order构造view tree，编译到DBToaster M3再生成C++（§2–5，pp.906–910）。

| 共享层 | 原文机制 | 对本任务的边界 |
|---|---|---|
| 计算/键空间 | variable order规定消元次序；`dep(X)`只保留该子树真正依赖的祖先变量。view tree按依赖键缓存局部计算 | 不是按任意相同前缀自动获得压缩；依赖键很大时共享很少 |
| 聚合payload | 如协方差 `(count, sums, products)` 用复合ring同时维护，复用中间计数和线性项 | 这是聚合代数的共享，不意味着每个query vector的nearest-neighbor排序可共享 |
| 结果表示 | §8.3，pp.918–920：结果分布在多个视图payload中，依键链接；避免显式展开独立分支的笛卡尔积 | 可保留局部搜索视图而不展开全结果；仍须说明如何访问所需分支 |
| 更新 | §6，p.911：可分解delta不展开Cartesian product，先消元再组合 | 任意更新并不天然低秩；不能免费获得分解 |

**具体共享例子。** §8.3 Ex.21：给定A，B与C条件独立，避免列出全部B×C；给定C，D不依赖A，因此即使同一C出现在多个A下面，其D列表也只存一次。**这比“共享Trie前缀”更关键：共享的是相同依赖键下的后续关系，而不是必须共享完整祖先上下文。** 论文不是通用reduced decision diagram的规范化算法；跨分支hash-consing需要另行设计。

**查询/更新/空间条件。**

- §7 Theorem 1，**p.912/PDF p.10**：固定查询的数据复杂度；**ring操作假定O(1)，否则乘其真实代价**。
  - free-connex acyclic：预处理O(N)，枚举delay O(1)，单tuple更新O(N)。
  - q-hierarchical：预处理O(N)，枚举delay O(1)，单tuple更新O(1)。
- q-hierarchical要求变量所出现的关系集合两两嵌套或不交，且满足free变量条件（§7.2）；**与外部目录树的“层级”不是同一定义**。
- Theorem 2把条件推广至满足FD的数据库的Σ-reduct；不能只看到`ID→属性`就宣布所有动态scope/facet查询均符合条件。
- §7.1通过每个view schema被单个输入关系覆盖，使固定查询下视图条目维持线性规模；**大bitmap/关系payload、附加索引的字节成本仍须计入**。一般view tree尤其循环查询不承诺相同空间界。
- 枚举delay不是有序top-k delay；完整枚举M项仍至少Ω(M)。
- §6 Ex.10：可分解 `δS(A,C,E)=δSA×δSC×δSE` 时，delta工作可与各因子尺寸之和相关，而不与完整乘积相关。反过来，随机稀疏/高秩delta可使收益消失。
- §5明确多关系批更新按关系依次传播；同一关系在query中多次出现会触发多条传播路径。事务可见性仍是部署层须明确的语义。

**不能套到任意top-k。** 截断top-k通常没有删除所需的加法逆：只保存前k项后删除一项，无法恢复被丢弃的第k+1项。即使构造“保留全部带权对象”的ring，payload也不再O(1)。任意q改变全部距离排序，SUM/协方差维护定理不提供ANN召回或精确top-k保证。

### 2. DBSP：解决可组合的delta语义，不解决共享索引选择

- §3，p.1604：`QΔ = D ◦ Q ◦ I`；I把delta重建为snapshot，D求相邻snapshot差；chain rule使组合查询可按算子增量化。
- §4.1–4.3，pp.1604–1606：Z-set为有限支持的`tuple→整数权重`；正负权重表达插入/删除；`distinct`根据累计权重是否跨过0输出变化。对多重绑定/重复推导尤其有用。
- §4.4，p.1607：线性算子可按delta大小工作、无持久状态；join的通用分析仍可达 **O(|DB|·|ΔDB|)**；join/distinct/I/D需要保存完整相关输入或输出，空间O(|DB|)。这里的DB是**算子局部关系**，并非只算源表一次。
- Algorithm 4.6的“与算子数成比例”是**增量计划构造成本**，不是每次更新的运行成本。
- §7.2，p.1610明确：COUNT/SUM线性；MIN遇删除须跟踪完整集合，朴素实现每次O(|DB|)，可定制算子优化。**不能把支持任意聚合表达误写成任意聚合自动快速维护。**
- §4.4.1指出换计划通常要重建内部状态，可把当前数据库作为一次大delta灌入；不是免费在线重排variable order。
- §8–9，pp.1611–1612：DBSP是eager bottom-up地产生输出变化；作者把与专用结构性能匹敌、树形/有序集合的适当group表示等列为未解决/未来方向。

---

## Report：迁移判断与可执行设计

### Q2：为什么共同ID posting交不能直接领走AGM/WCOJ收益

令每项谓词已经得到一元ID集合 \(P_i(ID)\)：

\[
E=P_{\rm subtree}\cap P_{\rm category}\cap P_{\rm range}
  \cap\bigcap_{t\in T}P_t .
\]

这是同schema的intersection，DBSP Table 1（p.1606）也直接把intersection写作该特殊equi-join。其结果大小不超过最小posting；**一元超图的AGM界退化为最小关系大小，不存在一般循环join的N^{3/2}相对N²式收益空间。** bitmap AND、最短posting驱动探测、排序交已有直接算法。

多值标签若先转成 `(ID,tag1,tag2,...)` 的组合join，可能人为制造同一对象的标签组合爆炸；存在性过滤应使用semijoin/集合语义，不能把避免错误展开当新算法优势。

**可能真有收益的情况：**

1. 很多scope/查询共享同一facet子表达式、cell-local结果或目录重复模板。
2. 目录内部状态能通过少量结构引用组成，而不是每祖先复制对象全集。
3. 几何cell的对象集合与facet局部状态可以跨多个scope复用；依赖键无须包含全部路径。
4. 更新具有明确块结构，例如整目录移动、同一facet赋予一组对象；表示和传播真正保留这种块，而非先逐对象展开。
5. 若数据确实具有独立组合，可避免表示上的乘积；**对象ID下的真实相关性不能用独立边际乘积替代。**

### Q3：一个最小可实现的“共享局部资格视图”设计

**[INFERENCE] 固定geometry-cell分区；原始FP32向量仍按稳定ID只存一份。cell只是组织键，不改变最终squared-L2。**

#### A. 关系与物化状态

```text
Parent(child_node, parent_node)        外部目录拓扑，稳定node ID
Bind(node, object_id)                  直接绑定；内部节点也可直接绑定
Facet(object_id, facet_key, value)     类别/多值标签
Number(object_id, numeric_key, value)  原始数值
Cell(object_id, cell_id)               单cell归属，避免几何复制
Raw(object_id, fp32_vector)

Direct(node, cell) -> ID-set/refcounts
Scope(node, cell)  -> sparse child-view references + nonzero support/count
LocalFacet(cell, predicate-prefix) -> shared exact local bitmap/view
```

- `Scope`按`Direct ∪ child Scope`组合；**不物化所有祖先×所有facet组合**。
- 目录多重绑定时，union需对象级引用计数或等价去重结构；裸计数求和不能作为distinct对象数。
- numeric range由经典有序索引/BSI等返回cell-local选择器，边界仍按原始值精确判定；不以粗桶替代硬谓词。
- 有选择地共享多谓词prefix，键包含cell、谓词规范形式和依赖版本。**这是需研究的视图选择/依赖组织，不是预建完整facet笛卡尔积。**

#### B. 不先枚举全scope的候选消费

向执行器提供懒局部接口：

```text
cells(scope)                         枚举该scope有对象的cell键
open(scope, cell, predicate)          构造组合视图句柄，不生成全scope ID表
next_word()/next_ids(limit)           只计算将被消费的局部资格片段
contains(id)                         候选点按共同ID核完整资格
```

`open`将目录视图与共享facet局部视图按ID交。按bitmap容器/ID块求值，只展开已访问块；空块跳过。**没有保证查空总是便宜**：可能读完所有相关bitmap字才能证明为空，必须计入实验。

有两条诚实执行路径：

- **精确路径**：流式扫描这些局部视图中的全部合格ID，raw FP32 squared-L2合并top-k；不先生成全scope列表，但距离数仍可为全部合格对象数。
- **局部搜索路径**：cell-local ANN或候选流调用`contains`，必要时消费局部view。资格可以精确，候选覆盖仍近似；不能继承F-IVM精确枚举为ANN保证。若要求严格精确终止，须遍历完合法候选或使用另行证明、包含数值误差处理的安全下界。

**设计的目标是节省重复资格准备、局部视图构造和维护，不以“必有新几何证书”为前提。**

#### C. 更新传播与成本口径

| 更新 | 传播及不能隐藏的成本 |
|---|---|
| 单facet变更 | 修改该对象cell的base posting，再更新/失效依赖它的共享局部views；成本随实际受影响views/容器数增长 |
| bind增删 | 修改`Direct(node,cell)`，沿祖先传播support/引用变化；不会免费O(1)，通常至少依赖目录高度与去重状态 |
| 向量变化 | 写raw；跨cell则旧cell删除＋新cell插入，更新有关scope-cell与facet-cell状态；局部ANN维护另计 |
| 目录MOVE | 稳定ID避免改写路径字符串；子树视图可重挂，但旧/新祖先support仍须更新。按子树非空cell摘要传播，成本至少涉及受影响祖先及这些cell，若还物化完整ID并集则可能与子树对象量相关 |
| MERGE | 目录冲突递归调和、重复绑定计数合并、旧view引用迁移与回收均须处理；不等价于一次重挂 |
| 批更新 | 同一事务聚合正负delta、按依赖传播，提交后查询看到一致版本；不能让旧目录view与新facet状态混用 |

**增量化退化反例：**

- 每个对象独有facet组合、随机数值、随机目录/cell关联：共享后缀几乎没有，结构退化为带指针开销的倒排索引。
- 所有目录scope都跨全部cell：scope-cell摘要趋近`节点数×cell数`。
- U个常用view都依赖一个高频tag：改一次tag可能触发Θ(U)更新/失效；lazy只把成本转移到查询。
- 一个对象有t个标签，物化所有标签子集组合可能达2^t；必须以预算限制物化，而不是称factorization会自动消除。
- 删除最后一个支撑对象时，大量非空状态需要消失；只保持“曾经非空”会错误路由或积累无界空壳。
- 目录MOVE涉及大量cell且多重绑定分布不同：不能从块移动直接推导低秩对象级union delta。

### Q4：研究增量应落在哪里

| 最近机制/经典组件 | 已覆盖部分 | 本设计必须额外证明的差异 |
|---|---|---|
| Curator | 共享几何树、标签信息、qualified ID驱动的虚拟谓词视图 | 跨外部目录与多facet的依赖键共享、避免重复view构造、MOVE及属性更新的传播性质；只称“虚拟局部视图”不够 |
| ELI | 标签包含关系、预算下选择子索引 | 不只是再选几个子索引；要有跨scope/facet的真实状态共享或新的候选消费接口及成本证据 |
| SIEVE | workload驱动index collection，速度/召回/空间模型和serving | “用收益/成本选view”本身不新；差异应是共享view的表示、构造/失效算法及动态代价 |
| BSI，经典 | 数值谓词的bit-sliced求值、bitmap组合 | 是range selector，不自动消除目录×facet×cell重复准备，也不自动减少L2计算 |
| wavelet/range结构，经典 | 序列范围到符号/cell的rank、reporting及压缩 | 本方案若只是subtree→interval→wavelet→cell，仍属经典归约；动态MOVE成本取决于真实split/concat等操作 |
| Trie/decision diagram，经典 | 前缀、后缀/同构子图共享 | 必须定义等价状态、变量次序、局部遍历与更新维护，不能只画DAG |

上述Curator/ELI/SIEVE比较依据本地既有审读；本轮未核它们完整新版实验和动态接口，**不作“均无更新”或“已完全覆盖本方案”的全称判断**。

---

## 最小共享收益实验与失败门槛

**实验尚未执行。** 固定raw向量、同一cell划分、同一查询及更新流；只改变资格表示：

- **B0**：共同ID的bitmap/最短posting驱动交，按cell懒消费；不能故意给基线预枚举全scope。
- **B1**：相同partial views但每scope独立保存，无跨scope共享。
- **F**：上述依赖键共享局部view。
- 相同精确距离内核；另加ANN时三者用同一候选器/预算，避免把搜索差异归因于factorization。

构造两组，保持posting边际大小、合法对象数和查询selectivity尽量一致：

1. **可共享组**：多个scope复用facet子表达式/局部cell状态，查询有重复prefix。
2. **破坏共享组**：随机置换facet及scope-cell关联、提高组合唯一性；再加入高更新率和大MOVE。

记录：
- e2e查询p50/p95、资格准备时间、view命中/实际复用次数；
- bitmap字节读取、局部view节点/容器访问、候选数、raw L2次数；
- 所有元数据/索引常驻空间，不能只报共享payload；
- 更新p95、写入字节、传播view数、失效后查询重建成本、MOVE/MERGE成本；
- 硬资格非法/重复/状态错误必须为0；精确模式与穷尽GT一致。

**通过条件：** F相对B0、B1在同内存/质量下确有端到端收益，且能归因于减少重复view构造、访问或更新；不能只证明压缩率提高。  
**失败条件：** 共享比例很高但e2e收益被L2主导吞没；共享元数据超过节省；更新与失效重建抵消查询收益；破坏共享组中出现不可控膨胀。失败仅否决该组织/工作负载组合，不否决整个跨域方向。

---

## 作者实验、future work与未核边界

**F-IVM，§9，pp.921–926：**

- 基线：first-order IVM、DBToaster higher-order IVM、Apache Flink；前三者同DBToaster v2.3后端，单线程C++；Flink v1.17.1使用全核。
- 数据：Housing合成星型、Retailer真实雪花型、Favorita真实销售数据；Twitter/TikTok图用于最长20 joins路径查询。
- 任务：协方差/线性回归、互信息/Chow-Liu、q-hierarchical结果维护、路径聚合；**不是向量过滤top-k**。
- 主要指标：吞吐、墙钟、内存、物化view数；1000 tuple批次，插入流round-robin且按variable order有序。删除语义由ring支持，但不能把插入实验当频繁随机删除/MOVE实测。
- 环境：Xeon Silver 4214 2.20GHz、188GB RAM；四次中最好三次平均；内存不含输入stream。
- §9.4：枚举很少时lazy占优；频繁枚举时eager-factorized占优。**物化不是无条件赢家。**
- q-hierarchical实验需Favorita Stores静态、Retailer Item静态及指定FD。
- Figure 17/18图像内完整tuple数、join大小和逐项吞吐表未成功提取，本报告不杜撰。
- 作者future work：概率图模型、更复杂ML、并行/分布式、theta joins/nested subqueries/NULLs（§11）；**向量局部资格view是我们的迁移，不是作者future work。**

**DBSP，§8–9，pp.1611–1612：**

- 本文以理论为主，**完整系统性能评价明确留待后文**，没有可据以转述的正式数据集/基线吞吐实验表。
- 作者报告SQL compiler通过700万SQL Logic Tests、理论已Lean形式化；**不是本次重跑结果，且作者明确不是实现本身已形式化验证。**
- 作者future work包含专用算子性能、nested inner relations的增量语义等；有序/树形结构的适当group嵌入并非现成结论。
- 因而DBSP适合为“更新正确性与组合规则”提供依据，**不能作为动态层级filtered ANN性能保证的引文**。