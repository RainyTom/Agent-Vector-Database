# 从 ANN 库到数据库：生命周期与系统语义

学习日期：2026-10-30。本文是官方资料阅读与项目设计推导，**没有运行数据库、故障注入或并发实验**；不提供产品排行或性能结论。关联主线为 B 类“可迁移借鉴”：一致可见性和维护成本可用于目录约束检索及未来 vector join，但 ANN top-k 本身不是 join。

## 1. 先把“写成功”拆开

ANN 库解决候选搜索；数据库还要回答：谁能读、读到哪个版本、失败后留下什么、怎样恢复。不能从 `addPoint`、`markDelete` 或保存索引文件的接口推出事务、WAL 或复制保证。

一条写入至少有五个不同里程碑：请求接收、日志持久化、数据状态应用、查询可见、优化索引完成。它们不必重合。未进入 HNSW 不必意味着不可检索：查询可能扫描未索引部分；反过来，“已经落盘”也不必意味着当前查询策略会包含它。应分别测量 `ack_latency`、`visibility_lag`、`index_lag`，不能拿提交吞吐代替索引追赶能力。

**实现差异。** pgvector v0.8.1 是 PostgreSQL 扩展，README 明示 ACID，FAQ 的“Is replication supported?”说明使用 WAL，因而可复制及时间点恢复；Storing 提供 SQL INSERT/UPDATE/DELETE。[S1] 其事务语义应放在 PostgreSQL 内讨论，不能套用专用库的 segment 模型。Qdrant 活文档的 Storage/Versioning 则描述先写 WAL 并分配顺序号，再应用到 segment；segment 与 point 保存版本，较旧操作不能覆盖较新 point，用于异常停机后的日志恢复。[S2] 这是重放顺序机制，不等于给用户提供历史快照查询或跨对象 MVCC。

## 2. Segment、墓碑与后台债务

Qdrant Storage 描述 collection 由多个 segment 构成，每个有向量/payload 存储、索引和内外 ID 映射；appendable 可追加、删除和查询，non-appendable 可读和删。不同 segment 可以暂时存在同一 point，搜索有去重机制。[S2，正文] 因此“物理记录数”不能直接当活对象数。

Optimizer 描述优化时旧 segment 继续可读，proxy 把变化放入优先级更高的 copy-on-write segment；Vacuum 清理标记删除记录，Merge 合并 segment，Indexing Optimizer 根据阈值启用索引。[S3，同名小节] 关键不是宣称后台工作免费，而是它把前台修改转换成未来的 CPU、读写带宽、临时空间与队列等待。若持续更新速率超过处理能力，前台短时延迟仍可能很好看，后台债务却不断增长。

`markDelete` 这个名字至多提示逻辑标记，**不能作为物理删除证明**。Qdrant 文档明确先标记、后由 Vacuum 回收。[S3，Vacuum Optimizer] 数据不出现在搜索结果、segment 空间释放、日志/快照/备份中的副本过期是三件事；彻底擦除要求覆盖全部保留介质的策略，不能由一次 delete 响应承诺。pgvector README 也单列 HNSW Vacuum 成本，并建议可先 `REINDEX INDEX CONCURRENTLY` 再 VACUUM；这是该版本文档的维护建议，不是每次删除必须立即重建。[S1，Vacuuming]

**新鲜度有配置边界。** Qdrant 活文档指出 `indexed_only=true` 可让尚未完成索引的更新暂时从结果消失；另有标为 v1.17.1 起、实验性的 `prevent_unoptimized`，使超过阈值的未索引 segment 中新点延迟可见。通常 `wait=true` 等待应用到 segment，但在该模式下会等延迟点完成索引，可能引发超时及队头阻塞。[S3，Prevent Reads… / Effect on wait=true] 不把这些新选项移植到未核对版本，也不把两种模式误写成所有版本的默认行为。

## 3. 事务、一致性与副本不是一个旋钮

PostgreSQL 17 文档 §13.2.1：默认 Read Committed 的普通 SELECT 使用语句起点快照，同一事务的两次 SELECT 仍可见不同提交。§13.2.2：Repeatable Read 使用首条非事务控制语句建立的事务快照，仍可能有序列化异常。§13.2.3：Serializable 可能终止事务，应用须重试整个事务。[S5] 所以“在 BEGIN/COMMIT 中分别读取目录和向量”不足以证明两者共享快照；隔离级别、语句组合及并发约束必须写清。快照一致也不消除 ANN 的候选漏检。

Qdrant 的一致性契约分三维：`write_consistency_factor` 指定写确认副本数；读 `consistency` 指定跨副本读取与结果协调；写 `ordering` 控制顺序。[S4，同名小节；活文档] `strong` 经固定 leader 串行化写入，leader 不可用时可能拒写；`medium` 换 leader 时仍可能不一致；默认 `weak` 不添加顺序保证。这足以否定“专用向量库一律只有最终一致”，却**不能据此推出跨 shard、多对象事务可串行化**。增加副本确认也不自动保证索引新鲜度、ANN 完整召回或应用级事务原子性。

副本的收益和代价必须同时计算：网络传输、每副本存储/索引维护、读扇出和结果合并，以及故障后的追赶。Qdrant 文档明确：达不到写确认数时操作可报错且部分应用；失败副本恢复需要同步。[S4，Write Consistency Factor] “报错就全部撤销”在这里不成立。

多租户的下述规则是 **[INFERENCE] 工程设计要求**，不是声称上述产品已配置好：tenant 必须贯穿认证授权、查询过滤、写路由、ID 命名空间及缓存键；分 shard 或 collection 只是放置边界，不自动等于安全边界。共享图/segment 可节省重复结构，却会竞争内存、优化线程和 I/O；按租户隔离则增加小索引、空闲容量及运维成本。至少记录每租户写配额、尾延迟、索引积压与错误租户命中数，最后一项必须为零。

## 4. 完整维护账本

以下为 **[INFERENCE] 测量清单**，各实现未发生的项记“不适用”，不要记成已测零成本。总成本应覆盖前台、后台、恢复及副本，而不是把后台 CPU 与前台墙钟简单相加。

| 操作 | 前台改动 | 延迟/放大成本 | 应记录的证据与边界 |
|---|---|---|---|
| 初始导入/建索引 | 向量、payload、ID 映射、索引构建 | 日志、临时文件、复制与峰值内存 | 构建墙钟/CPU、持久字节、峰值 RSS；含不含训练 |
| 插入/向量替换 | 数据写入、可写索引修改或新版本 | 旧版本/旧图节点、后台索引、WAL 与副本 | ack/可见/索引三个时间、距离计算、写字节 |
| 标量/标签更新 | payload 与相关过滤结构 | posting/bitmap 更新、日志、缓存失效 | 触及条目/字节、后台重建；不改向量≠免费 |
| ITEM MOVE/子树 MOVE | owner 或 parent、scope/祖先集合 | 路径重键、局部图迁移、摘要失效 | 后代数、祖先数、集合操作及 ANN 插入分别计 |
| 逻辑删除 | live/tombstone 与资格检查 | 死节点积累、搜索浪费、延迟回收 | 活/死数、错误返回、删除可见时延 |
| 压实/重建/合并 | 发布新结构并切换读者 | 新旧结构并存、copy-on-write、I/O 竞争 | 分类次数、CPU/墙钟、临时磁盘峰值、积压 |
| 快照/备份/恢复 | 一致恢复点及元数据 | 日志保留、重放、可能重建索引 | 恢复点、可恢复写边界、重放量、恢复耗时 |
| 副本追赶/迁移 | 状态转移与路由切换 | 网络、重复存储、目标重建 | 确认数、滞后、不可用窗口、最终状态核对 |
| 多租户与版本回收 | 权限校验、配额、读者版本登记 | 热租户干扰、旧版本保留 | 分租户资源、最老读者、回收水位 |

## 5. MOVE 的一致读取与失败/撤销

目录查询的逻辑状态可写为 `S_e=(owner_e, tree_e, live_e, vector_e)`；下标是同一提交视图，不要求各对象内部版本号数值相等。**[INFERENCE]** 可接受结果必须同时满足该视图中的存活、归属、祖先关系及向量距离。如果读到新 owner、旧 tree 和旧 live，就可能跨目录泄漏、漏掉已移动对象或返回已删除对象；最终只检查 live 不能修复混合视图。

可选实现是锁住完整读取区间，或构造新状态并原子发布 epoch，或由数据库事务统一管理。ANN 候选和资格校验也应关联这个视图；发布新状态后，旧读者结束前不能回收其依赖结构。此处是设计推导，不是本项目已实现 MVCC。

本项目 [动态三臂 runner README](../../codes/directory-aware-vector-attr-search/README.md#动态三臂-runner2026-10-29) 明示单线程查询看见先前成功更新、B/C 属性移动不改图，且不证明并发吞吐/MVCC；README 的维护语义还指出公共 `Tree::relink` 遍历后代更新 depth。因此不能把保留局部图写成整个 MOVE 为 O(1)，也不能把串行回放通过写成并发事务正确。

故障语义须逐项写进协议：

- WAL 已持久化但回复丢失：客户端状态未知，不能视为未执行；查询确认或以受控操作标识重试，避免覆盖后来更新。[S2 的机制；重试策略为推导]
- Qdrant 副本确认不足：可能部分应用，官方要求重发以达到一致；重发前须处理竞争写，不能凭错误响应执行“反向 MOVE”。[S4]
- PostgreSQL 事务失败：按隔离级别处理整事务重试；应用外部动作不因数据库回滚自动撤销。[S5；外部动作边界为推导]
- 已提交操作的业务撤销是新操作，不是历史消失。MOVE 回原位置前应核对预期版本，DELETE 后恢复还需原向量/属性；compaction 完成后不能依赖墓碑恢复。[INFERENCE]
- 重建中崩溃：必须能选择完整旧/新状态并重放剩余日志；这是待测恢复不变量，本文未核查任何产品具体原子发布代码。[INFERENCE]

## 6. 自测与下一轮可运行任务

自测：① WAL 已写但搜不到，能直接判数据丢失吗？不能，先分辨 applied、可见策略与 ANN 漏检。② `markDelete` 后磁盘不降是否必为 bug？不是，要查回收条件及备份保留。③ strong ordering 是否证明目录跨 shard 原子 MOVE？没有该推论。④ 两次 Read Committed SELECT 是否同快照？不保证。⑤ 元数据更新不改图是否零维护？posting、日志、权限和缓存仍可能变化。⑥ 串行回放能证什么？该顺序下状态与查询正确，不能证并发隔离。

下一轮任务，**尚未执行**：

1. 固定 PostgreSQL 17 小版本及 pgvector v0.8.1，建立 owner/tree/live/vector 小表；用两个会话交错 MOVE 与目录读取，对比 Read Committed 和 Repeatable Read。记录语句时间线、提交/回滚及混合视图；另用精确搜索分离 ANN 漏检。
2. 固定 Qdrant 镜像摘要；先按版本核对 API，再对唯一 ID 执行 insert→payload update→delete，记录确认、检索可见及优化状态。若该版本支持上述实验选项，再分组启用，不能直接照抄活文档到旧版。
3. 在一次性实例中分别于日志确认后、优化期间终止进程并重启，用操作日志核对恢复状态；另设多副本确认不足场景，核对报错后的部分写，禁止在正式实验服务器直接注入。
4. 为本项目下一版并发探针设计查询暂停点：读 owner 后暂停，执行 MOVE/DELETE，再继续读 tree/live/vector；以一致 epoch 精确 oracle 判定，而非以高召回掩盖越权。探针实施前不得宣称已有 MVCC 支持。

## 官方来源与版本状态

以下五份均已实际读取；活文档未冻结，访问日期同本文日期，不把文档最新功能视为本机已安装能力。

- **S1** [pgvector README，v0.8.1 tag](https://github.com/pgvector/pgvector/blob/v0.8.1/README.md)：Storing、Indexing、Vacuuming、FAQ/Is replication supported；按版本标签阅读，未另冻结提交哈希。
- **S2** [Qdrant Storage](https://qdrant.tech/documentation/manage-data/storage/index.md)：segment 正文、Versioning；未冻结的官方活文档。
- **S3** [Qdrant Optimizer](https://qdrant.tech/documentation/ops-optimization/optimizer/index.md)：Vacuum/Merge/Indexing、Prevent Reads from Large Unindexed Segments、Effect on wait=true；未冻结，文内单独标明实验功能版本。
- **S4** [Qdrant Consistency Guarantees](https://qdrant.tech/documentation/scaling/consistency-guarantees/index.md)：Write Consistency Factor、Read Consistency、Write Ordering；未冻结，本文不采用新版本 read affinity 作为既有部署保证。
- **S5** [PostgreSQL 17，§13.2 Transaction Isolation](https://www.postgresql.org/docs/17/transaction-iso.html)：§13.2.1–13.2.3；主版本固定，在线手册仍可修订，读取页显示 17.11。
