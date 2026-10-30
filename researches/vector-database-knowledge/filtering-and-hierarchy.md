# 03｜过滤执行：资格集合不等于搜索空间组织

状态：官方文档＋已有论文笔记的机制学习。未运行Milvus、Weaviate或Postgres；产品性能与数据库版本行为不由本页独立认证。记录日期2026-10-30。

## 1. 不用一个pre-filter标签代替算法

先问五件事：谓词何时求值？在哪个对象集合上求值？不合格点能否导航？过滤结果怎样送给ANN？不足k时怎样继续/停止？

| 执行方式 | 主要工作 | 常见边界 |
|---|---|---|
| 先属性索引，再对eligible精确扫描 | 资格集合＋全部合法向量距离 | 合法集小时很强，不是ANN导航创新 |
| 先构造allow-list，再由ANN消费 | 属性集合生成＋图/倒排搜索 | pre-filter不必意味着扫描；结果过滤不等于禁止不合格点导航 |
| ANN内部逐点谓词/bitmap检查 | 与展开交织的资格求值 | 全量bitmap准备不一定划算；点查也可能重复昂贵 |
| ANN候选后置过滤 | 先取全局候选再丢弃非法项 | 可能不足k或漏掉合法近邻，需要迭代/预算合同 |
| 按标签/区间/目录组织局部或共享索引 | 缩小/改变实际导航空间 | 复制、共享、更新及组合谓词需计成本 |
| 迭代向量候选＋逐项过滤 | 按需继续取得候选 | 不是任意谓词下有界成本或精确top-k保证 |

“属性先行”和“向量先行”是物理计划的选择，不天然对应exact/approximate。小eligible集精确扫描是必要控制；但不能让自己的大scope方案扫描、再声称发明新ANN。

## 2. 两个官方例子说明术语不能混用

### pgvector v0.8.0

固定版本[Filtering及Iterative Index Scans](https://github.com/pgvector/pgvector/blob/v0.8.0/README.md#filtering)：文档说明近似索引扫描后应用过滤；可用属性索引、partial index或partitioning改变计划。v0.8.0新增iterative scan，可继续扫描直至取得足够结果或触达`hnsw.max_scan_tuples` / `ivfflat.max_probes`等限制。

`strict_order`是扫描结果的距离顺序合同，不表示ANN已覆盖真正全局最近邻。`relaxed_order`再按原距离排序只能修正已取得候选的顺序，不会凭空修复候选遗漏。实际执行计划需`EXPLAIN (ANALYZE, BUFFERS)`核对；本轮未运行该SQL。

### Weaviate

官方[Filtering](https://docs.weaviate.io/weaviate/concepts/filtering)把倒排索引生成allow-list、再交HNSW称作pre-filtering，一般说明允许沿图导航、只将合法ID加入结果。该页明确产品ACORN是受论文启发的定制实现，不能继承原论文所有构图与保证。

活文档本轮显示v1.34起默认ACORN、v1.40新增PathSeer；这是**页面声明**，不是本机运行的版本。不同策略是否计算不合格点距离、是否多跳及如何进入过滤区域需分别核对，不能把allow-list一般描述套在所有策略上。flat cutoff示意图不是全版本固定百分比；已有[系统笔记](../containment-filtered-vector-search/systems/weaviate-filtering.md)保留配置与图示差异。

### Milvus

本轮读到[官方v2.5.x文档源](https://github.com/milvus-io/milvus-docs/blob/v2.5.x/site/en/userGuide/search-query-get/filtered-search.md)：standard filtering先做metadata范围限制；iterative filtering按迭代向量结果逐个计算标量条件，减少复杂过滤求值对象数，但顺序处理也可能增加时间。v2.5.x是维护分支，不是不可变commit。本轮官网页面超时后改读官方原始文档，未将示例JSON/示意图当运行结果。

## 3. selectivity要写分母，相关性要写测量对象

定义全库存活数N、scope大小S、完整eligible大小E：

- scope比例：S/N；
- scope内普通属性保留比例：E/S（S=0时不定义）；
- 总保留比例：E/N。

另看query近邻区域中的合格比例：同样E/N，合法对象可能集中在query附近，也可能全部远离query。全库比例无法预测导航困难、post-filter预算或局部候选覆盖。

在独立随机采样的理想模型里，预算b的合格数量期望为`b·s`，`k/s`只是期望量级；不是达到k的保证。固定按向量距离排序、属性与距离相关时，这个模型本身就不成立。合格比例1%的near/far例子见[语义探针](2026-10-30-exps/experiments/semantic_probes.py)。

做shuffle控制时说清保留什么：全局频次、每scope数量、每块数量、联合AND/OR大小、向量与属性关系、层级深度，不能只说“随机了所以公平”。保持每字段边际数量不保持字段交集，更不保持真实top-k几何。

## 4. 位图、倒排、范围编码与Trie分别负责什么

- posting存某条件的对象ID；bitmap用共同对象ID做AND/OR/NOT。若label与slot混用，集合算对也会查错对象。压缩bitmap、稀疏ID列表、bitplane各有密度/数值范围/更新开销。
- 单字段计数不够推导多字段交集。一个块分别含category=A、number>10、tag=t的对象，不证明同一对象同时满足三者。只能安全地用“任何一个必要条件完全不存在”排除AND块，不能将边际存在当联合存在。
- 数值bitplane保序编码需明确有符号、端点、NULL与禁用哨兵；集合NOT通常相对当前Live/Scope全集，不能无限取反产生不存在ID。
- Trie适合segment路径，不是裸字符串前缀：`/a/b`不应命中`/a/bc`。直接owner集合与inclusive集合不同；内部目录也可直接挂对象。
- Roaring、Trie、BSI是可复用组件；它们解决集合取得，不自动生成新的几何剪枝证据。是否构成新机制，取决于少做了什么、代价从哪里转移、与直接前序相比的实质差异。

## 5. 层级为什么可能不只是普通类别

单父树的递归子树构成laminar family：任意两子树不交或包含。对owner(x)、owner(y)，最低公共祖先的子树是同时容纳二者的最小子树。因此同一合法子树里包含x/y，就必包含这个最小公共子树内的全部对象。

这允许研究闭包见证、共享范围图、子树入口与拓扑更新等结构，不只是把树摊成标签。但以下边界不能跳过：

1. direct-owner查询不是recursive子树；其集合不包含目录后代，不能直接套用同一闭包性质。
2. 多父DAG的后代集合可以相交且互不包含；单树Euler区间、唯一LCA和laminar证明不能原封不动迁移。
3. 静态Euler编号能将子树转连续区间；动态R重挂使静态编号失效。选择全量重标、动态序列还是祖先关系查询，都有真实成本。
4. 加上普通属性AND后，范围不再只是子树；几何见证在树内但不满足Extra时不能自动作为合法替代。元数据集合的精确性不等于某种图剪枝正确。
5. 连通骨架只保证可达，有限ef仍可早停，长链也可能很慢。

[Directory-Aware既有笔记](../../papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md)明确TrieHI的scope元数据职责；[ACORN笔记](../../papers/filtered-ann/2403.04871-acorn/paper.report.md)明确通用谓词图和多跳前序。层级结构参与ANN是否有价值，继续用[本项目同池消融任务](../nested-attr-vector-search/2026-10-30-exps/closure-server-plan.md)验证，不在知识页预宣布方案成功。

## 6. 两个停止层次，不能混为一谈

**精确块剪枝**需要对块内所有eligible对象成立的距离下界，且单位/浮点误差正确；下界大于当前真实第k合法距离才可安全剪。局部ANN找到一个近点的距离通常是块最小距离的上界，不能反过来做排除证明。

**近似图导航**的剪枝/候选截断/heap停止通常是启发式搜索规则；返回k个合法结果或维持连通不等于已经排除所有更近对象。应测同质量时间，不包装成确定性几何证书。

## 自测与下一步

- 两个1%过滤集合，为什么全局top-50在一个返回5个、另一个返回0个？
- 同块两字段都出现，为什么AND可能为空？哪类摘要可安全排除、哪类只能估计？
- 把单父树改成多父DAG后，指出哪三个实现假设先失效。
- 下次实际系统实验只选一个后端，固定版本、索引、过滤策略和回退阈值，再用相同GT比较scan/pre/iterative；未执行前不填性能数字。

资料状态：上面三组官方文档本轮已读；两篇论文链接复用已有笔记，不表示本轮重新完整精读。默认/策略与实现行为待固定产品版本源码及EXPLAIN验证。
