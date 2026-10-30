# 向量索引机制：省掉了什么，又为此支付什么

学习日期：2026-10-30。状态：官方文档与指定源码阅读；**本页没有运行构建、查询或性能实验**。先建立成本模型，再做最小操作与自测。以下讨论单查询 top-k，不把 ANN 检索当作向量 join，也不把某库的接口能力泛化到所有同名算法。

## 1. 先分清误差与距离语义

设真实 top-k 为 G，进入最终精排的候选为 C，输出为 R。若候选去重、使用同一原始距离、同一资格集合与确定性 tie-break，且 |C|≥k，则精确精排后 `|R∩G|=|C∩G|`。这是集合推导，不是产品性能保证。**rerank 能纠正 C 内排序，救不了 G 中根本没进入 C 的对象**。

- **候选遗漏**：IVF 未探测目标倒排表；图搜索未访问目标或未保留它；PQ 初排把目标挤出精排预算。
- **量化排序误差**：访问了对象，但压缩距离改变相对次序。全扫描 PQ 仍可能不精确；“exhaustive”只说扫描覆盖，不说距离无损。
- 图导航若使用压缩距离，量化还会改变访问路径，不能把所有差异都归给最后排序。

Faiss 的 L2 返回平方距离；IP 按内积从大到小，不能直接当 cosine。仅当库向量和查询均归一化且非零，才有 `||x−q||²=2−2<x,q>` 的排序等价。[S2，活文档] 零向量、归一化时机、并列距离和浮点精度应先冻结，否则比较的可能不是同一个任务。

## 2. Flat：全扫描是基线，不是落后方案

`IndexFlatL2/IP` 保存原 FP32 向量、不需聚类或码本训练，逐个比较全部对象。[S1，Summary/Flat indexes] 在假设 N 条 d 维 FP32、忽略 ID/容器/临时缓冲时，载荷为 `4Nd` 字节，单查询距离算术量为 Θ(Nd)，另有 top-k 选择开销。1024 维每条仅原向量就有 4096 字节；这是容量算式，不是实际 RSS。

连续扫描可利用 SIMD、预取与批处理；稀疏过滤后随机 gather 则可能破坏局部性。因此“小合法集合精确扫描”和“全库扫描”必须分开。Flat 不用训练不等于更新免费：复制、ID 映射和删除移动均有代价；Faiss 基础 Flat 删除会缩短数组并改变顺序编号，业务稳定 ID 要显式管理。[S1，Supported operations]

## 3. HNSW：多层导航与有预算的底层探索

高层稀疏图用于寻找接近查询的入口，底层维护待扩展候选与当前最好结果，利用邻边继续搜索。邻居选择不只是取最近 M 个，还要保留导航方向的多样性。这里的收益来自少访问对象，不是每次距离计算变便宜。

指定 hnswlib 提交中，构造函数设置 `maxM_=M`、底层 `maxM0_=2M`，并令 `ef_construction_=max(ef_construction,M)`；内存按 `max_elements` 预留底层载荷。[S4，源码构造函数] 所以 M 不等于每层每点实际度数，当前对象数也不等于已分配容量。

| 参数 | 直接控制 | 必须同时观察的成本 |
|---|---|---|
| M | 邻接容量与连接选择规模 | 边存储、每次展开检查的邻居、建图与修边工作 |
| efConstruction / ef_construction | 插入时的搜索保留规模 | 建图距离、堆操作、锁竞争；增大后质量收益可能饱和 |
| efSearch / ef | 查询时的结果候选宽度 | 访问点、邻接读取、距离、visited 状态与堆操作；不是距离次数上限 |

[S4，ALGO_PARAMS 的建议是经验调参指导，不能移作本项目最优值。] 增大 ef 通常换取更高召回，但不能修复所有图结构缺陷；固定预算下，插入顺序、数据几何和查询分布都重要。随机层级的高度直觉**不能推出一般数据分布上查询必为 O(log N)**；这里没有一般分布、固定召回的对数工作量保证，更不能把“图连通”当作有限 ef 足够的证明。

同提交 README 支持同 ID 更新、标记删除及可选槽位替换，但明确更新流程有成本，`add_items` 与 `knn_query` 不能并发互用；`set_ef` 还需在加载索引后重新设置。[S4，API description] 标记删除不等于物理回收，也不等于图老化没有代价。Faiss 文档则明确其 HNSW 不支持删除。[S1，IndexHNSW Supported operations] 两者不可混写成“HNSW 都支持/不支持删除”。

## 4. IVF：先付路由费，再扫描选中的表

训练 coarse quantizer 将空间划成 `nlist` 个单元，每个对象进入一个倒排表；查询选择 `nprobe` 个表，表内顺序比较。[S1，Cell-probe methods] IVFFlat 保存原向量，因此误差主要来自表未覆盖，不是表内距离量化。

假设采用 Flat 质心路由、L2、无过滤与提前终止，候选数严格为 `C(q)=Σ_{j∈P(q)}|list_j|`；距离算术约为 `Θ(nlist·d+C(q)·d)`。**只有表长近似均衡且所选表有代表性时**，才可粗估 `C≈N·nprobe/nlist`；真实表偏斜和热点会影响均值与尾延迟。增大 nlist 可能减小表却增加路由、训练和质心存储；增大 nprobe 增加扫描，二者不是免费旋钮。

若确实访问全部表、存原向量且没有扫描上限/不一致过滤，IVFFlat 可退回穷举，但额外路由与表管理未消失；IVFPQ 即便扫全表仍有量化误差。添加对象通常使用已有分区，不自动重新学习质心；分布漂移会造成表失衡或路由覆盖下降。重新训练意味着评估重新分配、重编码和索引切换成本，不能仅计一次 `train`。

## 5. PQ 与 OPQ：用码本查表换掉原维计算

PQ 将 d 维分为 m 段，每段学 `2^b` 个中心，用 b 位记录中心编号。假设 d 可被 m 整除、码紧凑打包，每条编码约 `ceil(mb/8)` 字节，FP32 码本约 `4·2^b·d` 字节，尚未计 ID、倒排、原向量副本和查表缓存。[S1，PQ/Vectors encodings；公式按该机制推导] 注意这里 m 是子量化器数，不是 HNSW 的 M。

以 L2 非对称距离计算（ADC）为例，查询仍用原值，先算各段到码字的距离表，再对对象的 m 个码查表求和；简单实现建表算术量约 Θ(2^b d)，扫描 C 条编码约 Θ(Cm)，不含 top-k、解包和内存访问。IVFPQ 常编码相对粗质心的残差，查询所需表与预计算策略也会改变成本，不能直接把上述简式当所有实现的时延模型。[S1，IVFADC]

OPQ 学习旋转，使分段更适合 PQ，而不是一项新的候选搜索结构。[S3，Pre-transforming the data] **同维正交旋转、实数运算假设下**原 L2 不变，变化的是量化误差；降维则不具有这个等距性质。密集 d×d 变换朴素应用为 Θ(d²)，还要计训练和矩阵存储。重建 MSE 下降不保证 top-k 召回提升，因为最近邻间的距离间隔可能很小。

PQ/OPQ 训练集必须覆盖服务数据，而非只方便抽样；评估集不能用于选择码本或调参。更新可沿用旧码本编码，但分布变化可能恶化失真。若要原维精排，应另外保存或读取原向量；Faiss `IndexRefineFlat` 明确需要完整向量，`k_factor` 控制先取多少候选。[S3，IndexRefineFlat] 压缩索引小，不代表整个系统也小。

## 6. DiskANN：把图导航放到存储层级中计算

版本必须分开：读取时 `main` README 已是 DiskANN3 的 `DataProvider` 组合库；旧 C++ 留在 `cpp_main`，README 声明不再积极开发维护。[S5，Legacy C++ Code] 下列参数仅对应 **cpp_main 的 SSD 工作流活文档**，不是 DiskANN3 接口说明。

该工作流将图放在 SSD，以内存 PQ 表示辅助搜索，可缓存常访问节点；磁盘向量默认不压缩，也可用 `PQ_disk_bytes` 压缩。[S5，SSD build/search] 查询用候选列表推进图访问，节点命中缓存免去相应盘读；未命中时付 SSD 读取成本。调参的对象至少包括：

- `R` 与构建 `L`：图度及构建搜索列表，影响图体积、构建时间和可导航性。
- 查询 `L`：搜索候选预算；不能理解成固定 I/O 次数。
- `beamwidth`：每轮最多发出的 I/O 请求数。增加并行度可减少往返轮次，却可能读更多节点；高并发时又会争抢 SSD IOPS。
- 搜索 DRAM 预算与节点缓存：更长 PQ 码、更大缓存均占 RAM；查询热点变化会改变命中率。冷启动与稳态不能混比。

概念上，时延同时依赖 CPU 查表/排序、串行 I/O 轮次和设备排队，而不是“原向量不在 RAM 所以必慢”或“SSD 足够快所以免费”。构建峰值内存、磁盘体积、请求放大和 p99 也需计入。官方 README 的实时更新能力描述不证明任意 provider、持久化方式和更新流都零维护；本页未核实现或运行动态更新。

## 7. 联系项目：表示边界与计数边界

本仓库[联合信息诊断报告 §13](../nested-attr-vector-search/2026-10-30-exps/report.md#13-同日续轮接收0837918的联合信息诊断)记录：在其冻结条件下，WIKI 的既定 64 维完整同对象投影下界没有排除；这是已有服务器证据的本地记录，**不是本页重跑，也不是所有低维 ANN 无效**。1024 维降到低维会丢掉余空间方向；恢复对象绑定不能凭空恢复该信息。安全下界要求绝不误剪，PQ/OPQ/ANN 则允许候选或排序误差，二者不能互相代替成功标准。

“少算距离”也不等于“更快”：1024 维原距离、m 次 PQ 查表、质心距离不是同价操作；随机访问、堆、过滤、投影、去重与精排都收费。更细的是，S4 的 `searchBaseLayerST` 将 `metric_distance_computations` 按邻接长度累加，而真正距离调用在 visited 检查之后；该字段不是逐次函数调用的精确计数。实验报告必须说明统计口径，不能只凭字段名解释收益。

### 自测与下一最小操作

1. IVFPQ 的 nprobe=nlist 是否保证原维精确？答：否，量化与候选截断仍在。
2. 候选只含真实 top-10 的 7 条，原维 rerank 能否超过 0.7 recall？答：在第1节假设下不能。
3. ef=100 是否最多计算100次距离？答：不是；邻居扩展与结果队列容量不同。
4. OPQ 的等距旋转为何还能改善结果？答：改变分段量化误差，而非原空间邻居关系；改善与否需实测。

**下一最小操作（尚未执行）**：固定一小份项目 1024 维向量、查询及合法 ID 集，用原维 Flat 生成带 tie-break 的 GT；只比较同一 IVF 分区的 IVFFlat/IVFPQ，在两个 nprobe 和两个精排预算下记录候选覆盖、精排前后 recall、实际扫描数与端到端时间。原向量读取和训练分别计费。它优先区分路由遗漏与压缩截断，不需要先部署六种索引；另记软件版本、训练样本、线程、冷/热状态，不能提前填入结果。

## 实际读取的来源、版本与未核事项

以下为五组官方来源；除 S4 固定提交外，均为**未冻结活文档**，不等同于已安装版本。本轮只核所列章节，不声称论文全文精读。

- **S1 — Faiss index wiki**：[Faiss indexes](https://github.com/facebookresearch/faiss/wiki/Faiss-indexes)。已读 Summary、Flat、Cell-probe、IndexHNSW、PQ/IVFPQ；网页显示编辑于 2025-07-28。未核当前二进制的各类 PQ 位宽支持，文中不将旧示例的位宽限制作统一结论。
- **S2 — Faiss metric wiki**：[MetricType and distances](https://github.com/facebookresearch/faiss/wiki/MetricType-and-distances)。已读 METRIC_L2、METRIC_INNER_PRODUCT、cosine；网页显示编辑于 2024-12-03。未核零向量处理与具体距离核浮点差异。
- **S3 — Faiss transform/refine wiki**：[Pre and post processing](https://github.com/facebookresearch/faiss/wiki/Pre--and-post-processing)。已读 Pre-transforming、PCA 示例、IndexRefineFlat；网页显示编辑于 2020-02-06。未核本机 API 签名与 OPQ 训练实现。
- **S4 — hnswlib 固定 `ca426729609b3221563047ceb269cc1213e0d376`**：[ALGO_PARAMS](https://raw.githubusercontent.com/nmslib/hnswlib/ca426729609b3221563047ceb269cc1213e0d376/ALGO_PARAMS.md) 的 Search/Construction；[README](https://raw.githubusercontent.com/nmslib/hnswlib/ca426729609b3221563047ceb269cc1213e0d376/README.md) 的 Highlights/API；[hnswalg.h](https://github.com/nmslib/hnswlib/blob/ca426729609b3221563047ceb269cc1213e0d376/hnswlib/hnswalg.h#L90-L145) 构造函数及 [searchBaseLayerST](https://github.com/nmslib/hnswlib/blob/ca426729609b3221563047ceb269cc1213e0d376/hnswlib/hnswalg.h#L324-L435)。未运行并发、更新或召回实验。
- **S5 — DiskANN 官方分支文档**：[main README](https://raw.githubusercontent.com/microsoft/DiskANN/main/README.md)、[cpp_main README](https://raw.githubusercontent.com/microsoft/DiskANN/cpp_main/README.md) 的 Usage，以及 [SSD_index](https://raw.githubusercontent.com/microsoft/DiskANN/cpp_main/workflows/SSD_index.md) 的 build/search 参数。分支未固定提交；引用块里的 0.6.1 不是当前 main 版本。SSD 文档 build/search 的 cosine 列表存在差异，未核源码消解，故不承诺该组合支持范围；未核 DiskANN3 provider 持久化、动态维护或其性能宣称。
