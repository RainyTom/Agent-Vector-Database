# 学习路径：从检索合同到可解释的向量数据库研究

参考路径结构：[一周新人](https://wangyong9999.github.io/lakehouse-wiki/learning-paths/week-1-newcomer/)、[一月AI](https://wangyong9999.github.io/lakehouse-wiki/learning-paths/month-1-ai-track/)、[一季度资深](https://wangyong9999.github.io/lakehouse-wiki/learning-paths/quarter-advanced/)。以下按本仓库研究目标重排，不要求先搭完整湖仓/RAG，也不承诺日历时间后自动完成。

状态约定：**已读**仅表示来源所列章节；**已验证**必须有本地命令/输出；**计划**没有结果。首轮五章已写成，真正学习完成与否用自测/解释判断，不能按文件数量打勾。

## 第一阶段：五次学习单元，建立最小系统模型

### 单元1｜先给每个查询写合同

- 读：[查询语义](query-semantics.md) §1–4、Faiss MetricType官方页、pgvector v0.8.0 Querying。
- 做：运行[semantic_probes.py](2026-10-30-exps/experiments/semantic_probes.py)，解释IP/cosine不同排序与投影丢失。
- 能回答：为什么Faiss L2与pgvector `<->`阈值单位不能直接混用？为什么向量集合检索不是join？
- 首轮状态：相关来源已读、数学探针已运行；未部署SQL后端。

### 单元2｜看索引省掉的工作，不背参数表

- 读：[索引机制](index-mechanisms.md) §2–6，固定hnswlib构造/搜索源码、Faiss IVF/PQ及DiskANN分支文档。
- 做：运行[真实HNSW API探针](2026-10-30-exps/experiments/hnsw_lifecycle_probe.cpp)；对每个结构写“候选来源/可能漏哪里/精排能修什么/额外字节”。
- 能回答：ef为什么不是距离次数上限？IVFPQ扫全表为什么仍不一定精确？DiskANN的候选预算与SSD请求并行度为何不同？
- 首轮状态：来源已读、四对象HNSW过滤/墓碑/恢复已运行；IVF/PQ/SSD性能操作未执行。

### 单元3｜把过滤表示、计划和图导航分开

- 读：[过滤与层级](filtering-and-hierarchy.md)、现有ACORN/TrieHI笔记、pgvector Filtering、Weaviate Filtering、Milvus官方v2.5.x过滤文档源。
- 做：解释探针中两个同为1%过滤集合为何top-50后分别有5/0条结果；给出边际非空但AND为空、DAG非laminar的反例。
- 能回答：allow-list精确为何仍可能召回差？层级是否改变邻接/路由，还是只提供资格集合？
- 首轮状态：反例已执行，产品文档行为未做数据库实测。

### 单元4｜给每条更新画出确认与可见边界

- 读：[生命周期](lifecycle-and-systems.md) §1–5、PostgreSQL17隔离级别、Qdrant storage/optimizer/consistency官方文档。
- 做：对INSERT/标签更新/R重挂/DELETE分别写ack、可见、索引完成、回收与恢复；指出单线程runner没有覆盖哪种交错。
- 能回答：为什么逻辑删除不是物理擦除？strong ordering为什么不自动成为跨shard事务？BEGIN为什么不自动保证两条SELECT共用快照？
- 首轮状态：来源已读；只运行hnswlib墓碑行为，不冒充WAL、故障恢复或数据库事务验证。

### 单元5｜复核一份结果，而不是只看胜负

- 读：[评测与成本](evaluation-and-cost.md)、[当前实验合同](../nested-attr-vector-search/2026-10-30-exps/closure-server-plan.md)。
- 做：用已有JSON重算macro/pair/空结果例子；选一份真实报告核对均值/p50、独立qidx、计时边界与压实是否触发。
- 能回答：距离调用下降但时间变慢可能来自哪里？哪些证据支持新机制，哪些只能支持维护组件？
- 首轮状态：数学例子已运行、复用既有项目结果解释；不重新跑服务器正式失败来确认。

## 第二阶段：四个专题，补真实系统行为

下列是后续任务，不把其状态计入本轮已验证。

| 顺序 | 具体问题与最小动作 | 验收产物 |
|---|---|---|
| 1 执行计划 | 在隔离实例固定PostgreSQL/pgvector版本，装同一小表；比较属性索引精确过滤、HNSW有限扫描、iterative scan，使用EXPLAIN ANALYZE/BUFFERS与共同GT | 镜像/版本、DDL、查询计划、实际结果、扫描量；分清排序与候选漏检，无需大数据跑分 |
| 2 编码误差 | 同一IVF分区比较Flat/PQ及原维rerank；两个nprobe、两个候选预算，严格分开训练/校准/确认 | 原始GT、候选覆盖、精排前后recall、访问/字节/训练成本；只在合法数据许可下执行 |
| 3 一致视图 | 两会话交错owner/tree/live读取与MOVE；Read Committed和Repeatable Read各一条可复现时间线 | 当前可见性、混合视图反例/消除证据；不把高recall掩盖非法返回 |
| 4 维护偿还 | 一次性Qdrant或其他单一后端固定镜像，insert→payload update→delete→触发optimizer，记录可见性与后台完成 | ack/visibility/index lag、墓碑/segment状态、资源/回收；故障注入仅在可丢弃实例 |

选择这一顺序：先把产品API/计划与算法概念接上，再研究编码与并发；避免一开始部署多个大系统，只得到不可归因的排行榜。

## 第三阶段：面向研究者的三个专题

### A. 存储层级与分布式

在读清segment/WAL之后再学习SSD图、对象存储、本地缓存、分片/副本和查询扇出。每次只选一个真实瓶颈：冷缓存、fanout或重建恢复；先核版本，再给预算/IO模型。湖表snapshot、catalog与在线ANN索引的一致发布应单独定义，不能默认Iceberg+索引文件就是向量服务。

### B. 与检索邻域相互借鉴

从已有[data-indexing](../../papers/data-indexing/README.md)、[incremental-views](../../papers/incremental-views/README.md)、[hybrid-retrieval](../../papers/hybrid-retrieval/README.md)进入谓词布局、范围过滤、增量视图、BM25/sparse/dense融合。迁移时写原问题/新问题、保留假设、失效假设、少做的访问、完整更新代价和能区分旧方案的反例。

### C. 把知识转成研究假设

产出一张机制证据卡：现有机制→不足→本项目差异→可检验预测→最小消融→反例/停止条件。若仅“别人用图，我们换高级图”，或只有更名而无可区分行为，不进入实现。问题新、索引旧也可能有贡献，但必须证明迁移产生此前没有处理的结构/成本收益，而不是单纯换应用名。

不预设本项目闭包候选成功；服务器结果回来后，按[评测知识](evaluation-and-cost.md)区分缺陷、质量、表示覆盖、总时间及新颖性增量，更新学习记录而不是改写历史。

## 每次学习如何留下可复用成果

1. 查`sources.jsonl`和论文索引；已有工作沿用本地材料。
2. 写一个能回答的问题，最多选择一组直接相关机制。
3. 记录来源URL、版本/章节、已读范围、未知与推导，不填“应该是”。
4. 给一个反例/最小操作；运行才写结果，计划仍留待办。
5. 更新对应知识页和[learning-log](learning-log.md)，在日期目录追加report、data与experiments。
6. 提交小文件；大数据、镜像、索引与第三方源码留ignored目录。

这是维护流程，不是已配置的定时任务。本轮没有自动刷新外部网页、监视服务器实验或无限后台学习。
