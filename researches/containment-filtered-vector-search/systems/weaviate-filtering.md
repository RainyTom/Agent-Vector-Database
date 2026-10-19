# Weaviate 过滤执行与前缀检索基线

来源：[Filtering 官方文档](https://docs.weaviate.io/weaviate/concepts/filtering)、[Vector index 配置参考](https://docs.weaviate.io/weaviate/config-refs/indexing/vector-index)。核查日期：2026-10-19（Asia/Shanghai）。这是系统文档笔记，不是新论文记录，不计入全文论文数量。

## 关键结论

Weaviate 的 pre-filtering 是先确定合格对象 allow-list，再让向量执行器结合它搜索；不应将其解释为只能先筛选后暴力扫描。对前缀研究而言，“前缀范围先转bitmap，再交给ANN”必须与已有的过滤图导航组合比较，不能只用全局top-k之后丢弃不合格对象的弱基线。

## 官方页面描述的执行层

每个 shard 的属性倒排索引生成合格uint64 ID allow-list，传给HNSW。页面的一般执行说明允许导航沿不合格节点的边继续，而最终结果受allow-list约束。不能把这段通用说明等同于每个策略都对所有不合格对象计算距离，策略级行为须分别记录。

| 策略 | 文档描述 | 证据边界 |
|---|---|---|
| sweeping | 沿图搜索，结合allow-list决定结果资格 | 不等于在合格节点诱导子图上切断所有不合格节点 |
| acorn | 多跳邻域探索，避开不合格对象的距离计算，并增加随机合格入口 | Weaviate明确是受ACORN论文启发的定制实现，不能自动继承论文全部构图/保证 |
| pathseer | 底层图搜索中按节点及结果集填满状态调整邻居处理；部分场景检查邻居的邻居 | 当前页面标注v1.40新增；官方称无需额外索引存储或重建，不是本项目独立实测 |

截至本轮页面，默认filterStrategy为acorn（文档标注自v1.34起），pathseer是可选策略。系统版本需固定，不能认为任何历史版本都有三者。官方称不同过滤相关性下策略性能不同，不记录为任意数据统一最优结论。

文档提到的PathSeer原论文为[PathSeer: Adaptive Neighbor Handling for Efficient Filtered ANN Search](https://doi.org/10.1145/3802098)，标注SIGMOD2026。初次核查时DOI正文403，仅登记候选；随后已取得27页原文并核读，见[原论文报告](../../../papers/filtered-ann/10.1145-3802098-pathseer/paper.report.md)。原论文的双区融合索引和VOB离线profiling，与本页产品同名策略分开追踪。

## 成员表示 扫描回退与保证

`indexFilterable`使用Roaring bitmaps支持match类过滤；`indexRangeFilters`为数值/日期范围提供另一路范围索引。前者的范围集合与ANN图导航是可分离组件，bitmap本身不生成新的几何连接。

匹配很少时，系统可通过flat-search cutoff改走合格子集扫描。当前配置参考列`flatSearchCutoff`为integer、默认40000，并说明设0可强制向量索引搜索；Filtering页面图中约15%的示例不是固定的产品默认比例。具体版本的触发条件、分片统计和索引实现仍需代码核查，不能仅凭示意图外推。

“保持图导航”“经验召回通常良好”不能改写成逐查询Recall@k下界。零结果、不足k、并列、压缩距离以及有限ef的行为需测。文档中的性能结论是厂商结果，未独立复现。

## 对本项目前缀问题的意义

前缀p的精确合法集合仍为 $\mathcal D_p=\{o_i:p\preceq_{\mathrm{pref}}s_i\}$。完整前缀成员展开、TrieHI聚合或静态DFS范围归约，都可以作为产生这个集合的不同物理方案；上述为本项目归约，不表示Weaviate该页面宣称原生Trie/目录维护。

该页未说明native prefix tree、目录MOVE/MERGE或路径segment语义。不能仅从本页缺少说明推出整个产品不支持字符串匹配/前缀相关API。若将完整路径作为文本属性，tokenization和路径边界必须另核；不能把裸字符串匹配自动当作本项目的segment前缀谓词。

因此评测分两层：

1. 固定合法集合、ANN及入口，比较前缀展开/Trie/范围表示的scope成本和字节。
2. 固定同一scope与总内存，比较图导航策略；只有这一步的增量才能支持前缀参与ANN导航的假设。

组件实验继续统一hnswlib；Weaviate应作为另列的系统后端，不能把原生hnswlib和Weaviate的跨系统差异归因于前缀结构。后者至少记录数据库版本、策略、ef、连接数、量化、分片/副本、并发、缓存以及flatSearchCutoff，分别运行保留生产回退与强制图搜索的实验。不能只让本方案在少量合格对象时扫描。

所有方案计scope生成、入口、图导航、距离计算、验证/排序、总字节和最终Recall@k；无ANN性能实验前不声称某策略优于TrieHI/Curator或本项目原型。PathSeer原文已补读，其双区索引、混合邻居处理与成本调度会约束F3/F8/F9；产品对应实现仍待逐项核查。

## 证据定位

Filtering页面章节：Post-Filtering vs Pre-Filtering、Efficient Pre-Filtered Searches、Filter strategy（ACORN/Sweeping/PathSeer）、indexFilterable、Flat-Search Cutoff。Vector index参考：HNSW的filterStrategy/flatSearchCutoff配置。日期化摘要与待读候选见[核查记录](../evidence/weaviate-filtering-2026-10-19.json)。未安装/运行Weaviate；后续已读PathSeer原论文及部分作者代码，不将其视为Weaviate实现等价证据。
