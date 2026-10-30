**针对加强后的 zone-supported portals：已读三个来源中，未见该完整“资格→代表支持域精确投影→代表图结果准入”接口；不能据此认定领域首次。** 与旧版整块非空激活相比，这一变化确实增加了机制差异，而非仅更换术语。

| 已读来源 | 已有的最近操作 | 与新接口的具体边界 |
|---|---|---|
| **多代表路由，正式 PVLDB 2025，§3–3.2**，[原文](https://www.vldb.org/pvldb/vol18/p1649-gottesbueren.pdf)，`artifact://147:108-114` | 每 shard 多代表；在所有代表上建 HNSW；代表检索映射到 shard；以距离或频率排序 | **已读章节未见属性资格参与代表准入。** 代表及其子簇支持关系是几何路由结构，没有 `∃合法对象属于该代表zone` 的查询时精确检查。它仍是几何骨架的强直接前序，但没有覆盖新增资格接口。 |
| **Curator v3，§3、§4.2**，[原文](https://arxiv.org/html/2601.01291v3#S4.SS2)，`artifact://142`；本地 `papers/filtered-ann/2601.01291-curator/paper.md:674-699` | 把完整资格集合按固定几何子树范围分配到临时树；明确提出仅在预计搜索的子树中求谓词、建临时索引 | **“资格投影到几何支持域”这一抽象已有最近对应。** 但其消费端是临时层级视图及 buffer 搜索，不是同块多个固定 proxy 的 zone-specific result-set filtering，也没有在一张多代表 HNSW 中保留不准入 proxy 供导航的该组合。不能把“支持域精确资格投影”单独说成新发现。 |
| **SIEVE v2，§2.2、§3、§6**，[原文](https://arxiv.org/html/2507.11907v2)，`artifact://143:203-217,486-511` | 完整对象 bitmap；hnswlib result-set filtering；predicate 子索引集合；先计算 bitmap 再选择 serving strategy | 过滤的是**被索引数据对象本身**的资格。已读机制未见把另一组对象的资格，通过固定 zone 存在性映射为 routing proxy 资格，也未见这种 predicate-dependent representative activation。 |

### 建议主方案保留的准确表述

> 在已知多代表分区 HNSW 路由骨架上，将完整对象资格按固定、唯一的几何 zone 支持域进行存在性投影，作为 routing proxy 的结果准入谓词；同一块可以因查询谓词不同而激活不同代表，不准入的 proxy 仍可导航。

形式上，新接口为：

\[
\mathrm{Allowed}(p,q)
\iff
\exists x\in Z_p:\mathrm{Eligible}(x,q),
\]

而不是普通对象过滤的 `Eligible(p,q)`，也不是旧方案的 `∃x∈block(p):Eligible(x,q)`。

**这支持“相对已读三篇存在具体组合差异”的判断，不支持“这些操作全部首次”。** Curator 已有完整资格对几何子域的投影和 lazy/local 思想；多代表路由已有 proxy-like 几何骨架；SIEVE/hnswlib 已有准入与导航分离。新增辨识点在于三者的具体连接：**对象资格经 zone 支持域投影后，直接改变多代表图的可返回代表集合。**

此外，固定 routing-vector 副本不作为 data object、删除源对象后保留 proxy 的完整生命周期规则，**在本轮已读三个来源的相关章节中亦未见对应说明**。这只能作为当前设计的明确接口差异，不应额外包装成已证明的新维护原理。