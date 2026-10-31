# 基线完成状态、IGP 量化迁移与离线 GPU 加速

日期：2026-10-28。范围：Directory-Aware 单查询、scope 内向量 top-k（DSQ）；不是 vector join。**本轮为现存证据核查与研究方案，不实现新量化后端、不运行建图、编码或查询实验。** 用户执行后续实验，助手提供目的、命令与验收标准。

## 1. 结论

1. 原定的两类完整 WIKI DSQ 基线与精确对照已经跑完；HLG 全量结构/检索、量化基线没有完成。不能把“原始基线已完成”说成“全部研究实验已完成”。
2. IGP 可借鉴的是 **ColBERTv2 的 VQ + 低比特残差 SQ**，不是一种 IGP 原创 PQ。其多向量候选聚合与 MaxSim 评分不能直接替换本项目的 squared-L2 DSQ。
3. 先把全局 ANN 的压缩变体作为强工程基线；NodeANN 的候选研究点放在共享表示、跨节点统一误差/精排预算和目录维护约束，不把“每节点换成量化索引”称为创新。
4. 探索阶段允许 **GPU 离线训练、assignment、编码及受支持的构图**，正式查询计时保持 CPU。已看到 RTX 3090 24 GiB，但当前研究环境尚无相应 GPU Python 工具链；设备可见不等于 GPU 流程已可运行。

## 2. 基线到底完成了什么

下表为原始单次全量 WIKI、227 条 recursive 查询的代表结果；A 类每种执行器均已运行 PE-Online / PE-Offline / TrieHI 三种目录表示，表中数字取 TrieHI 行。标准 macro 为非空查询上的 `hit/min(k,scope_card)`；global 为总 hit / 总 GT pairs，二者不能混用。non-recursive 229 条也已完成，其中空 scope 41 条单列。

| 方案 | 本轮已归档状态 | recursive macro / global recall | mean e2e ms | 角色 |
|---|---|---:|---:|---|
| A：三目录表示 + 全局 HNSW M16 / ef2048 | 原始基线完成 | 0.212384 / 0.308634 | 20.091 | 全局图近似基线 |
| A：三目录表示 + 全局 IVFFlat / np256 | 原始基线完成 | 0.467083 / 0.547247 | 6.770 | 全局倒排近似基线 |
| A：三目录表示 + IVFFlat / np1024 | 精确对照完成 | 1 / 1 | 25.126 | 全列表对照，不是 IVFPQ |
| B：NodeANN，直接绑定节点局部 HNSW / ef64 | 原始基线完成 | 0.999559 / 0.999320 | 6.839 | 局部 ANN 基线 |
| B：每直接绑定节点局部 Flat | 精确对照完成 | 1 / 1 | 4.305 | 不替代局部 ANN 基线 |

证据：[原始标准汇总](../2026-10-27-exps/data/wiki_full_summary.csv)、[原始 all3 退出记录](../2026-10-27-exps/experiments/wiki_full_all3.command.json)、[Flat 退出记录](../2026-10-27-exps/experiments/wiki_full_node_flat.command.json)、[IVF 全列表退出记录](../2026-10-27-exps/experiments/wiki_full_ivf_exact.command.json)；三进程的 command JSON 均为 `exit_code=0`，GNU time 独立记录也为 exit 0。原始日志中的旧 `recall` 是 hit/10，不应用来否定标准汇总的 recall=1。

额外轮次单列：

- NodeANN harness setup 是另外 3 个独立进程，recursive mean 中位数 **6.638942 ms**；正式 run1 又是另外 3 次，中位数 **6.649231 ms**，不与单次 6.839 混为同一个统计量。[setup](data/autoresearch_harness_baseline.json)、[run1](experiments/autoresearch_run_01.txt)。
- scope-run 已完成 12 轮，保留 run11 的 3 次 mean 中位数 **1.127503 ms**，macro/global 均为 1。它是静态 packed FP32 scope 扫描候选，不是原局部 HNSW，也没有压缩检索向量。[迭代记录](data/autoresearch_iterations.csv)、[最佳汇总](data/autoresearch_best_summary.json)。
- HLG 已测连接边构造和 G3 MOVE；全量 G1/G2、修正版检索冒烟尚待用户执行。MERGE 图维护、MOVE 后查询元数据布局未完成，`on-shortcut` 构造未实现。[状态与限制](report.md#12-hlg层级血统连通图单索引--可证连通--有界维护)。
- **没有已运行的 SQ/PQ/2-bit 全局或 NodeANN 压缩基线**；没有量化/GPU 加速倍数。

原全局 HNSW 基线构图完成，并不意味着现在有缓存文件：`repro.cpp:641–647` 序列化仅用于计量大小，随后删除文件。因此 HLG 缺少缓存与“旧基线已完成”并不矛盾。后续任何构图应保存版本化产物与 hash，供多个 codec/ef 实验复用。

现有边界：未完成全局 HNSW 同召回网格和独立重复构图统计、未完成局部 HNSW 净内存分解、未做真实发布方查询约束或跨数据集泛化。当前单独 `--executor node` 且 threshold=0 默认进入 scope-run；重现原局部图应显式指定 `--node-algorithm hnsw`。`all3`、`dsq_scan` 和 threshold>0 的 Flat 路径仍保留原局部索引语义，不能误报 Flat 默认已经漂移。

## 3. IGP 的量化到底是什么

### 3.1 原论文与当前源码版本

- **IGP: Efficient Multi-Vector Retrieval via Proximity Graph Index**，作者顺序 **Zheng Bian、Man Lung Yiu、Bo Tang**；SIGIR ’25，2025，pp.2524–2533，正式出版日期 2025-07-13。[DOI](https://doi.org/10.1145/3726302.3730004)、[Crossref](https://api.crossref.org/works/10.1145/3726302.3730004)、[作者全文](https://www4.comp.polyu.edu.hk/~csmlyiu/conf/SIGIR25_IGP.pdf)。作者稿修订号及 arXiv 版本未核实。
- 本轮源码固定到 [`1899e9316e71b023518f9090fc64fe66dbd0c202`](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/tree/1899e9316e71b023518f9090fc64fe66dbd0c202)。README 说明当前 CPU IGP 入口使用 GIGP+ 的更快单线程实现，作者称结果与原 IGP 相同；不是冻结的 2025 原始代码。
- 相关正文已核：§3.1–3.2（PDF p.3）、§4 开头（p.4）、§4.1 Algorithm 1（p.5）、§4.2（p.6）、§5.1 Table 6（p.8）；完整理论、完整实验及引用闭包尚未精读。详细核实状态见[日期化文献审计](data/igp-literature-audit-2026-10-28.json)，本轮不声称新增完整全篇论文索引。

### 3.2 表示与检索不能混为一件事

对每条向量，先取 coarse centroid `c_z`，保存 `uint32 z`，再把残差 `x-c_z` 的每一维量化为 b bit。重建为 `x_hat = c_z + weight[code]`。

- 残差 SQ 支持 **1/2/4/8 bit 每维**，论文推荐 2 bit；不是“整条向量只有 2 bit”。cutoff 与重建 weight 是样本全部残差分量的共享分位数表，不是各节点分别训练一个 PQ 码本。原论文明确沿用 ColBERTv2 的 VQ/SQ（§3.2、§4）。
- IGP 图建在 **float32 质心** 上，不是所有原始对象上，也不是用低比特码替代本项目 1.94M 点 HNSW 的构图距离。小质心图构建快，不能证明量化会让现有全量 HNSW 构图同样加速。当前代码 `M=32`，论文 Table 6 为 `M=64`，配置也不能混写。
- IGP 原任务是 `sum_q max_v q^T v` 的多向量 MaxSim。其 CPU 检索从压缩码重建候选文档后算内积/MaxSim，**没有回查原始 FP32 做精确精排**。搜索索引可不常驻整份原始向量，但仍有质心、图、IVF、ID、每线程 float 重建区；原始编码输入仍在磁盘。
- 当前解码器的重建后 normalize 已被注释。即使原向量近单位范数，量化重建后也不保证单位范数。迁移必须保留

  `||q-x_hat||² = ||q||² + ||x_hat||² - 2 q·x_hat`，

  不能省略 `||x_hat||²` 而把内积排序冒充本项目原始 squared-L2，也不能擅自重新归一化数据改变任务。

源码定位：[CPU 量化/解码](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/include/alg/refine/ResidualScalarQuantizationIGPPipeline.hpp#L17-L98)、[解码不归一化](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/include/alg/refine/ResidualScalarQuantizationIGPPipeline.hpp#L330-L378)、[质心图](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/include/alg/probe/IGP/IGPProximityGraphProbe.hpp#L48-L68)、[索引内存状态](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/impl/IGP.hpp#L25-L132)、[MaxSim 精排](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/impl/IGP.hpp#L243-L277)。

**复用许可边界**：仓库 metadata 的 license 为 null；固定 tree 未见根目录 LICENSE，发现的第三方 LICENSE 不能自动授权 IGP 自有实现。直接复制、修改、再分发对应源码的许可未核实。可依据论文机制独立实现并引用，不能把“开源地址可访问”当作已经取得复用授权。[仓库 metadata](https://api.github.com/repos/DBGroup-SUSTech/multi-vector-retrieval)。

## 4. 全局 ANN：先做压缩强基线，不改变目录语义

现有 A 类是一个全局 HNSW/IVFFlat，加三种目录表示解析出的相同 scope ID selector。codec 不应变更 scope、输出计数或 GT。

### 4.1 两条接入路线分别命名

1. **已有库压缩对照**：本地 FAISS 已有 `IndexHNSWSQ(QT_8bit/QT_4bit)`、`IndexHNSWPQ` 和 `IndexIVFScalarQuantizer/IndexIVFPQ`。可用作标准 SQ/PQ 强基线，**不把它们标成 IGP 的 2-bit residual SQ**；本地 SQ 枚举没有 2-bit 选项。出处：`deps/PathSeer/faiss/faiss/IndexHNSW.h:149–168`、`impl/ScalarQuantizer.h:27–35`。
2. **IGP-style 残差 SQ 对照**：独立实现共享 coarse centroids、uint32 VQ IDs、2/4-bit 残差编码和 CPU squared-L2 scorer。GPU 做离线准备，CPU 使用冻结码本和码；不移植文档/token 聚合、MaxSim、单位范数补偿或 GIGP GPU search。优先在全局 IVF 路径隔离 codec 效果，再接图存储距离接口；这是一项新的全局压缩变体，不是旧基线的新结果。

[INFERENCE] 全局图适配可从 FAISS `storage_distance_computer` / `IndexFlatCodes` 距离接口切入；索引构建还需要点到点距离，不能只实现 query-to-code。图与 code ID 必须一致。直接换 `IndexHNSWFlat` 为压缩存储会改变构图距离和图实例，不能假定只变内存而召回不变。

### 4.2 实验隔离

- **固定图，只换 codec**：冻结同一图邻接、查询、入口、ef、selector，比较 FP32、SQ8/4、IGP-style residual2/4。用来隔离表示/距离近似误差。现无完整 HNSW 缓存，不声称这一实验已经具备所有产物。
- **压缩距离参与重新构图**：训练/编码/构图/序列化分别计时；另标图实例，重新测 recall，不与固定图行混为同一消融。
- **两种内存合同分开**：code-only 候选与保留 raw FP32 的精排候选分别报告。后者可提升候选排序，但不能恢复未被 ANN 访问的真近邻，也不能按 code-only 比例声称 RSS 压缩。
- 低比特码只是存储；如果每次都完整解码 1024 维再做距离，CPU 算术并未减少，且有解码开销。需要测 packed ADC/查表与 CPU float 重建两种内核，不能先许诺搜索或构图倍数。
- 量化不补目录入口、图连通性或候选覆盖；不能把它当成 HLG G1/G2 的替代证明。

## 5. NodeANN：先说清内存，再提出可证伪的研究假设

### 5.1 现有内存的真实来源

单绑定下，每个对象只进其直接绑定节点的局部索引；**当前已经不是每个祖先复制一份向量**。`repro.cpp:876–887` 构建局部索引并保留全局 ID 映射，`:620` 的原始 base 数组同时仍驻留。不能用消除一项原本不存在的祖先复制来宣称创新。

- corpus 为 1,941,679×1024，非空直接节点 363,467，平均 **5.34 点/非空节点**（算术推导，不是新实验）。大量独立小索引有对象/分配/调用开销，不能假定为每节点训练量化器一定划算。
- 当前每非空 Node 存一个 FP32 1024 维质心，payload 合计 **1,488,760,832 bytes**；默认 `node_limit=0` 不作质心截断时，这个字段不参与那段路由。共享码本/编码池、按需保留摘要、减少重复原始加载属于先应做的工程对照，不单独算创新。
- 原局部索引 `size_MB` 只计向量 payload 下界，不能据此报告完整净索引大小。需要分别测 base、codes、centroids/codebooks、graph、ID maps、目录、对象分配、scratch 和常驻原始精排向量。

### 5.2 内存模型，不是测量结果

以下为 **仅向量码 payload** 的算术，MB=10^6 bytes，不含图、质心、ID、精排原向量及工作区。

| 表示 | 每向量 bytes | WIKI payload MB | 相对 FP32 payload |
|---|---:|---:|---:|
| FP32 | 4096 | 7953.117 | 1× |
| SQ8 | 1024 | 1988.279 | 4× 压缩 |
| SQ4 | 512 | 994.140 | 8× 压缩 |
| 2-bit residual codes | 256 | 497.070 | 16× 压缩 |
| PQ64×8 codes（另一个对照，非 IGP） | 64 | 124.267 | 64× 压缩 |

例如 IGP-style b=2、**假设**1024 个全局质心，另加 uint32 VQ ID、共享重建 weight 后约 **509.031 MB**，还没有图/目录/scratch。K=1024 只是清楚列出的模型选择，不是从 IGP 原论文公式照搬，也不是本地实测最优。若保留 7.953 GB raw FP32 常驻，不能把整个进程称为约0.5 GB。

若给每个非空 Node 独立配一套 8-bit PQ 码本，纯码本就约 **381.1 GB**；这仅说明逐节点独立 PQ 的风险，**不是 IGP 的共享 SQ weight 表需要这么多内存**。完整可复算字段见[内存模型](data/igp_quantization_memory_model.json)。

### 5.3 候选研究假设：scope 内跨 Node 的统一精化预算

**问题**：递归查询需合并许多直接绑定 Node；分别返回每 Node 的 k 个候选再精排，会重复支付局部结果准备和高精度访问。低比特误差又可能在跨 Node 排序时改变谁进入 global top-k。

**假设（研究者综合推导，非 IGP 作者 future work，新颖性尚未充分排查）**：全局共享 codes/码本，保留每个对象一次编码；把各 Node 的候选放进同一个 query-global cutoff/精化队列，只细化仍可能进入 scope 内 global top-k 的候选，可能比“各 Node 各精排 k 个再合并”以更少 raw 读取达到同召回与更低内存。

- 第一层工程对照是共享码本/编码池 + 原局部图/原全候选 Node 集，避免把对象分配/布局改善误算为精化策略收益。各 Node 内均使用同一搜索/精化规则，不按数据集规模或选择率切换索引。
- 不把不同 Node 的有偏近似距离直接当作原始真实距离比较；最终 raw 精排的距离须在同一 squared-L2 标尺。shared codec 也不自动消除量化误差。
- 若要主张不漏剪枝，需要真实误差上界与未访问候选覆盖，而不是只给一个 ANN 近似分数。比如在实数算术下保存 `epsilon_i >= ||x_i-x_hat_i||`，则 `max(0,||q-x_hat_i||-epsilon_i)` 是原 Euclidean 距离下界，可与真实候选的第 k 距离比较；squared-L2 要用一致单位，ties 按全局 ID。IEEE 浮点实现需要另行处理保守舍入。
- 上述三角不等式本身不是新定理；若图搜索没有覆盖所有未排除对象，安全精排队列也不能把 ANN 变成精确 top-k。可先在全 scope 的压缩扫描上检验误差/精化，再与局部 ANN 的候选遗漏分开。

**动态约束**：优先将编码状态绑定于稳定的直接对象/节点身份，而不是查询锚点或每个祖先。对纯 MOVE，原直接绑定集合不变，则全局固定量化器的 codes 不必因目录祖先变化而重编码。这是设计性质，不算新的 O(1) 整体 MOVE 定理；目录元数据仍有原维护成本。MERGE、再训练及依赖祖先中心的 residual codec 须另行分析，后者可能导致大规模重编码。

**风险与反例**：现有 WIKI 节点几何 cohesion 只有随机子集的约0.80–0.90，不能假定目录局部 residual 显著更小；独立每节点中心还会引入约1.49 GB 摘要和大量 query-to-center 计算。低比特码可能降低召回或增加精排次数，抵消带宽收益。

**前序边界**：[IGP/ColBERTv2](https://www4.comp.polyu.edu.hk/~csmlyiu/conf/SIGIR25_IGP.pdf#page=3)已有共享 VQ/SQ；[SymphonyQG](https://arxiv.org/html/2411.12229#S3) 已联合设计量化、图、批量距离和隐式精排（§3.1–3.2），其方法仍保留 raw vectors，不能当成 code-only 内存前序。LOPQ、RaBitQ、VA-file、progressive/refinement kNN 和局部/层级 quantizer 还需补完整引用链。仅改成“共享码本 + NodeANN”或“压缩 + SIMD + 重排”不足确认新颖性。

### 5.4 最小实验矩阵（设计，未运行）

| 编号 | 要隔离的变量 | 必须固定/报告 | 否证条件 |
|---|---|---|---|
| Q0 | GPU 编码是否兼容 CPU 解码 | 冻结 centroid/cutoff/weight；逐字节/逐向量核对，覆盖 cutoff ties 和尾维；raw squared-L2 GT 不变 | 格式/ID/打包不一致先修，不报速度 |
| Q1 | 全局 ANN 的 codec 效应 | 同一图/候选与 selector；FP32、标准SQ、IGP-style residual SQ；报告 code-only 与 raw-rerank 两合同 | 同召回不省内存/不加速，则不是可用强基线 |
| Q2 | NodeANN 共享表示的工程收益 | 不改 Node 候选集、局部搜索预算；逐层内存账和端到端时延 | codes 省下的内存被中心/图/复制/raw 常驻吃回 |
| Q3 | query-global 精化与各 Node 独立精排 | 同一码本、同候选集合；报告 raw reads、精化点数、macro/global recall、p50/p95、峰值RSS | 同召回不比强工程对照更好，停止该机制 |
| Q4 | MOVE 是否导致量化状态重写 | 单独记录目录维护、codes/码本变化和更新后查询质量 | 依赖祖先码本而重写整棵子树，不能宣称有界编码维护 |

质量至少保留冻结 harness 的 recursive macro/global≥0.999、non-recursive recall=1；精确型变体另要求全部 GT 命中。门槛不是压缩方法理论保证。精确 Flat/scope-run 作为强对照保留，不降低它们的优化程度。原 harness 的180s构建门槛属于原候选；探索性 GPU 训练/构图单列成本，不为“通过”而隐藏预处理。

**当前没有可运行的 `--quant` 或 GPU-build 集成 CLI。** 上表是设计，不给虚构命令、不要求现在重跑已完成的长基线。接入实现和环境准备完成后，应交付每一臂的完整命令、输出文件和验收标准，由用户执行。

## 6. GPU 使用约定与当前可用性

### 6.1 已查到的本机与工具状态

- 设备信息查询：`NVIDIA GeForce RTX 3090, 24576 MiB, driver 591.86`；只运行了 `nvidia-smi`，未分配 CUDA 计算任务。
- 当前 PathSeer FAISS CMake cache：`FAISS_ENABLE_GPU=OFF`、`FAISS_ENABLE_PYTHON=OFF`。
- 现有研究 venv 的 executable 名为 Python3.13；被检查的 torch、faiss Python、cuVS/CuPy distributions 均未发现。不能把 IGP README 的 Python3.8 原环境直接当成此 venv 已可用，也不声称其他环境都没有 GPU 能力。
- 建议离线 GPU 工具链独立于现有 CPU 测量环境；产物通过文件/ID/格式合同交换，不直接替换 CPU Faiss fork。详情见[环境记录](data/igp_gpu_environment.json)。

### 6.2 哪些阶段用 GPU

| 阶段 | 决策 | 证据/风险 |
|---|---|---|
| codebook/coarse centroid 训练 | 优先 GPU | IGP `vq_sq_pack.py:76–88` 已使用 `faiss.Kmeans(gpu=True)`；本项目保留原 squared-L2，不照搬归一化质心 |
| 批量最近质心 assignment、残差编码/packing | 优先 GPU、分块输出 CPU codes | IGP 有 CUDA encoder；原 assignment 是内积 argmax，迁移为 squared-L2 时要改正 |
| cutoff/weight 的样本分位数 | 可用 GPU 独立实现或先保留小样本 CPU | 被查 IGP 实现是 CPU `nth_element`，不能称整条链已全 GPU |
| 新数据 GT/输入准备 | 可 GPU 离线加速 | 现有冻结 GT 不重算；新 GPU GT 使用原 FP32 距离语义，禁用会改排序的低精度快捷路径并独立核对 ties |
| HNSW/邻接图构建 | 有 GPU builder 才使用；缓存并记录图来源 | 当前 IGP 的质心 HNSW 仍 CPU；不能凭模块名推断 GPU HNSW |
| 正式 DSQ 搜索、原始精排和 latency/RSS | CPU | 不调用 GIGP GPU search，不将 GPU 预计算的查询专属答案或候选塞进 CPU 计时 |

GPU 组件证据：[训练与 assignment](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/script/evaluation/technique/vq_sq_pack.py#L16-L106)、[CUDA residual encoder](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/include/alg/refine/ResidualScalarQuantizationPackGPU.hpp#L18-L79)、[公开离线编码接口](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/app/build_index/GPGPUBuildIndexPackGPU.cpp#L14-L31)。同参数 CPU/GPU 字节一致性尚未实测；固定 cutoff 时还须对齐 upper_bound 的边界规则。

### 6.3 GPU 构图不是不可用，但不能冒称原图

[Faiss 官方 cuVS 文档](https://github.com/facebookresearch/faiss/wiki/GPU-Faiss-with-cuVS)说明 `GpuIndexCagra` 可 GPU 构图并导出 CPU HNSW。这适合探索，但 CAGRA 是另一种构图算法，不是把现有 CPU `IndexHNSWFlat.add()` 原封不动放 GPU；当前本地 fork 未发现该接口，导出的新版本索引也不能假定与旧库序列化兼容。

若采用该路径：固定一张 GPU-built 图，用同图完成 FP32/量化/HLG 对照；与原 CPU HNSW 基线分表，重新测 scope、连通性和召回。不能强制新图在 ef2048 精确复现归档0.2124，也不能因此跳过同图 off/self 与 off/faiss 的一致性检查。

### 6.4 计时与显存

分别报告 `T_train_gpu`、`T_assign_encode_gpu`、`T_graph_build`、H2D/D2H、序列化、总离线墙钟、CPU 加载/初始化和 CPU query latency。GPU 段计时需同步；真实首次图构建/初始化必须计入离线/启动成本，不因移到 search 计时器外就忽略。CPU 测量仍固定并发/线程，GPU 不在后台抢占准备数据。

24 GiB 不是整个流程一定能一次装下的证明：raw corpus 本身约7.95 GB，训练副本、assignment score 矩阵、图候选与临时区可能更大。必须分块、限制 scratch，并记录 GPU 峰值显存与 CPU 峰值RSS，不把最终0.5 GB codes 当成编码峰值显存。

## 7. 本轮产出与未完成项

- 新增本文、日期化文献审计、解析式内存模型和设备/环境记录；同步入口与检索日志。
- 本轮未新跑性能实验，未声称量化/离线 GPU 后端已接入。原始基线状态不因新方向改变；HLG 仍按原门槛等待用户结果，不提前追加 shortcut。
- 后续优先级：先建立可用的独立 GPU 离线环境和 codec/CPU 评分合同，再做全局压缩对照，最后才检验 NodeANN 的跨 Node 精化假设。任何新颖性主张均待完整前序核对与同预算实测。

## 8. 继续研究：新找到的直接前序与更窄问题

### 8.1 核心直接前序

| 工作与书目边界 | 本轮核对位置 | 对候选创新的约束 |
|---|---|---|
| IGP，Zheng Bian / Man Lung Yiu / Bo Tang，SIGIR 2025 | §3.2、§4、Table 6；见上文固定源码 | 共享 VQ/SQ 与有损候选重建已有；不是新的量化器 |
| [iFVS: Towards Instance-Optimized Filtered Vector Search](https://arxiv.org/html/2607.22922v1)，Abdullah Al-Mamun / Yeasir Rayhan / Walid G. Aref，arXiv v1，2026-07-24；正式 venue 未核实 | §2、§3、§4 | **固定 PQ codes，按 query+filter 生成码本扰动与维度权重**已有直接前序，不能再把“目录条件码本/查询感知量化”作为未经排查的新主张 |

iFVS §2 使用共享 memory bank、learned adjustment directions 和谓词编码权重；训练冻结 base codebook 与 PQ codes。§4 作者明确 future work 包括 unseen queries 泛化与 workload shift 自调整。其正文没有给出本轮可确认的目录 MOVE/MERGE 维护协议；这只是当前读到的差异，不是“无人研究”的证明。论文 HTML 的 VLDB Workshop 模板未填写完整会议名，不据此认定已正式发表。

比较限制：iFVS §3 写明其训练和推理使用 NVIDIA A30，而 FAISS baseline 为单线程；Fig.1/2 的平均 recall 含 train/test，Table 2 才分开。不能把该 QPS/recall 直接与本项目纯 CPU、冻结测试 workload 的结果比较；其候选机制仍是必须考虑的直接前序。

### 8.2 背景机制与风险前序

| 工作 | 已核位置与版本 | 不能冒称的新内容 |
|---|---|---|
| [Locally Optimized Product Quantization for Approximate Nearest Neighbor Search](https://www.cv-foundation.org/openaccess/content_cvpr_2014/papers/Kalantidis_Locally_Optimized_Product_2014_CVPR_paper.pdf)，Yannis Kalantidis / Yannis Avrithis，CVPR 2014，pp.2321–2328 | §4.1、§5.4、§6 | 分区 residual、局部 PQ/旋转、每 cell query LUT 与模型开销已有；“把局部量化器放到各 Node”不是独立创新 |
| [SymphonyQG](https://arxiv.org/html/2411.12229#S3)，Yutong Gou / Jianyang Gao / Yuexuan Xu / Cheng Long，arXiv v1，2024；正式版书目本轮未复核 | §2.2、§3.1–3.2 | 量化图遍历、批量距离、隐式精排及量化辅助构图已有，不能只靠这些组合立项 |
| [When Does Low-Bit Quantization Preserve the Decisions of Vector Search?](https://arxiv.org/html/2609.09854)，Wenxuan Xiao / Xu Cao，arXiv v1，2026-09-09；正式 venue 未核实 | §1、§3.1–3.3、§5、§7 Table 4、§8.2–8.3；本轮已读正文（附录仅抽样） | 比较翻转的边界质量+残差尾分解、差分残差方差 `K_xx+K_yy-2K_xy`、未过滤 ANN 的 held-out 选择性证书都已有；`Table 4` 显示 int4 在最难 20% margin 上仍翻转 10.1% |

### 8.3 已被否证的方向：scope 均值校准

> **状态更新（本轮新增）：** 本节原先的“按 scope 校正均值”假设**已在理论上被否证**，结论见 §10.1：DSQ 的所有比较都在同一 scope 内，公共加性偏差在排序比较中抵消，均值校准改变不了 top-k。以下内容保留作推理留痕与内存账参考，**不是当前候选方案**；当前方向见 §10.2。

原先的待检验假设是：固定一份全局 codes，以可合并的小维度统计表示目录 scope 的量化残差偏差；查询仅生成一个 scope 共享校准，MOVE 只更新根路径摘要、不重写对象 codes。

它必须与 iFVS 的固定 codes + 谓词条件评分、普通条件均值校正、动态聚合摘要及层级量化同时对照。差异若最终只是把关系谓词换成目录 ID，则不足立项。

一个用于否证的具体模型如下，尚未实现或运行：

- 原向量 `x_i`，全局重建 `x_hat_i`，误差 `e_i=x_i-x_hat_i`。固定 corpus-only 正交基 `U∈R^(d×r)`，不使用测试 query/GT 训练。
- 每个直接绑定 Node 保存 `s_local(v)=sum U^T e_i` 和 count；每个目录保存其子树的加性聚合 `s_subtree(v)` 和 count。recursive 取 subtree，non-recursive 取 local，空 scope 返回空。
- scope 的校准为 `delta_u=U*s_u/n_u`。压缩候选分数用 `||q-(x_hat_i+delta_u)||² = ||(q-delta_u)-x_hat_i||²`；一个 query 只生成一份校准和共享查表，不逐 Node 重建 dense query LUT。
- **真实任务仍是原始 q 的 squared-L2**。校准仅是近似打分/候选顺序；若执行 raw 精排仍必须使用原始 q，而不是把校准后的 q 当作新的 GT。
- 稳定 basis/codebook 下，MOVE 不改变 local stats 或 codes，只按旧/新祖先路径维护 subtree sums/counts，编码摘要更新为 `O((h_old+h_new)*r)`；这不消除现有目录结构/depth/bitmap 的维护成本。MERGE 直接绑定聚合需要单独定义，不能自动归入已测 G3。

**可证明但不算新贡献的性质**：在一个固定 scope 中，投影均值校正使经验重建 MSE 减少 `||U^T mean(e)||²`。证明是展开 `mean ||e-UU^T mean(e)||²`，用 `U^T U=I` 消去交叉项。该式只约束重建误差，**不保证 recall 提高**。

**明确反例**：1D scope 原向量为 `0,1,100`，重建为 `0,1,103`，误差均值为 `-1`。校正后重建为 `-1,0,102`，MSE 从3降到2；但 query `q=0.1` 原本在压缩数据中正确命中对象0，校正后最近的是对象1。因此不得凭 MSE 下降宣布可用，必须检查 scope 内 top-k margin/比较翻转与冻结 GT recall。这里是解析反例，不是新跑实验。

摘要内存也须计入：r=8、468,031 个目录，local/subtree 各一份 dense float64 r维 sum 共约 **59.91 MB**；两份 uint64 count 约 **7.49 MB**，另有 basis。该额外约67.4 MB 是算术，不是测量；比为每 Node 存1024维中心更省，但更省的表示本身仍不是新颖性证明。float64 及确定性聚合用于控制反复 MOVE 的数值漂移，实际误差与舍入边界待验证。

最小否证问题：**同一全局 codec 下，目录 scope 的量化偏差是否稳定、是否改善临界 top-k 比较，而不是只有整体 MSE 变化？** 若改不动 margin/recall，停止该校准机制，不转而用更大的监督模型或测试 GT 调参挽救。

## 9. CPU 搜索加速与 GPU→CPU 格式合同的进一步收敛

### 9.1 residual SQ 不必每次完整解码后再算距离

设 `x_hat_i=c_z+w(code_i)`，离线可预存 `a_i=||x_hat_i||²`。则原 reconstructed squared-L2 可写成：

`D_hat(q,i)=||q||² + a_i - 2 q·c_z + sum_j T_q(j,code_ij)`，

其中 `T_q(j,s)=-2*q_j*w_s`。`T_q` 跨所有 Node/centroid 共享，`q·c_z` 按 query 懒计算并缓存。norm cache 增加每对象4 bytes，WIKI 约 **7.77 MB**；GPU 可离线准备 norm，CPU 查询不依赖 GPU。

这是展开平方的已有 ADC 思路，不是新算法结论。2-bit 的共享表仅1024×4 float，即16 KiB（算术），但每对象仍需1024维的 packed lookup/累加；不能因为表小、码短就推断一定比64段 PQ或现有 SIMD扫描快。最终还需把 query LUT/centroid项的准备耗时计入，不能在小 scope 场景隐藏固定开销。浮点 ADC 与显式重建不保证逐 bit 相等，需按原 GT 核对排序。

如启用§8校准，可把 q 换成 `q-delta_u` 来计算**近似**分数。若还用预存 `epsilon_i>=||x_i-x_hat_i||` 作安全下界，不能直接把原 epsilon 套到校正后的重建：应对原重建计算下界，或另用 `epsilon_i+||delta_u||` 的保守界。最终 raw 精排与报告距离仍用原 q。

### 9.2 源码静态核对到的 Q0 合同

[GPU encoder](https://github.com/DBGroup-SUSTech/multi-vector-retrieval/blob/1899e9316e71b023518f9090fc64fe66dbd0c202/src/include/alg/refine/ResidualScalarQuantizationPackGPU.hpp#L18-L79) 的 `compress_gpu` 与 CPU `compress` 均从高位起依次 pack。2-bit 桶序列 `[0,1,2,3]` 对应 `0x1B`，是源码规则的解析例子。GPU 以首个 `cutoff > error` 取桶，与 CPU `upper_bound` 的边界方向一致。

这只确认算法/布局意图，不是 CPU/GPU 字节一致性实测。GPU kernel 在尾维判断前有 `assert(dim < vec_dim)`，因此维度不是 pack宽度倍数时还需修正/验证；当前1024维满足1/2/4/8-bit的整除条件。应先固定 centroid、cutoff、weight、ID顺序，再逐块核对两端编码/解码，不能拿独立 GPU/CPU K-means 生成的不同中心去要求 codes 相同。

Q0 至少覆盖：cutoff 相等、负残差、全零残差、ID重排、非整除尾维和格式版本；这些是未运行的验收要求，不是通过记录。GPU训练、编码或 CAGRA 构图首次运行必须保留独立命令、环境、hash 和真实总离线成本。

## 10. 决策层面的理论修正与修订方向（本轮新增）

### 10.1 命题：单 scope 排序中公共加性偏差必然抵消

DSQ 的 top-k 只比较同一 scope $S$ 内的对象，因此考虑一个“scope 加性偏差”型近似器：

$$
\hat d(q,x)=c\,d(q,x)+b_S+\xi_x,\qquad x\in S,
$$

其中 $c>0$、$b_S$ 可依赖 query 与 scope 但不依赖 $x$，$\xi_x$ 是候选相关残差。对任意 $x,y\in S$：

$$
\hat d(q,y)-\hat d(q,x)=c\bigl(d(q,y)-d(q,x)\bigr)+\xi_y-\xi_x .
$$

$b_S$ 被完全消去，$c>0$ 不改变次序。故：

- **任何按 scope 估计的公共加性偏差，对 scope 内 top-k 成员与次序都没有影响**；它能影响的只有跨 scope 比较（DSQ 不做）与 $\alpha\ne1$ 的剪枝规则。
- 该结论与 [2609.09854](https://arxiv.org/html/2609.09854) §3.1–3.3 一致：其仿射标定 $b+a\,d$ 的截距在排序差分中抵消，只在剪枝量 $Z_\alpha=\xi_{tc}-\alpha\xi_{sc}$ 中以 $(1-\alpha)b$ 存活。这是独立推导与原文的双向印证，不是我们新发现的定理。

**推论（撤回 §8.3）**：按 scope 校正均值、或为每 scope 生成“校准后的 query”，其系统性成分是排序 no-op。§8.3 的 67.4 MB 摘要因此**没有排序动机**；只有候选相关的那部分修正才可能移动 top-k，而那需要逐候选信息，等价于提高码率或重排，已不属于“均值校准”。同时，这也解释了为什么“每 scope 重算 query LUT”不会仅因消除偏差而改善排序。

### 10.2 真正决定翻转率的是差分残差方差

同文 Prop. 6 给出：对固定 $(q,x,y)$，排序翻转概率由

$$
\nu_{\text{rank}}^2=K_{xx}+K_{yy}-2K_{xy}
$$

控制（$K$ 为残差联合次高斯代理矩阵），翻转概率 $\le \exp[-c_Q^2\gamma^2/(2\nu^2)]$，$\gamma$ 为精确 margin。可操作杠杆只有三个：

| 杠杆 | 手段 | 本项目的约束 |
|---|---|---|
| 减小边际残差 $K_{xx}$ | 更多 bit、更好码本、残差编码 | 增加内存/解码成本；§7 Table 4 显示 int4 在最难 20% margin 仍翻转 **10.1%**，加比特不移动边界 |
| **增大共享误差 $K_{xy}$** | 让同 scope 候选共享粗质心/共享旋转，使公共分量抵消 | 可能牺牲 scope 内精度；且 label-aware 分区已有前序（见 §10.4） |
| 增大精确 margin $\gamma$ | 由数据与 query 决定 | 索引无法改变 |

原文 Table 7 的实证值得注意：候选对从 i.i.d. 换到 top-32 候选集，共享 query 相关从 0.39 升到 0.60（Cohere 2-bit）。也就是说**候选集本身就是产生相关性的来源**。目录 scope 是更强的候选集约束，理论上应当进一步抬高 $K_{xy}$ —— 但这是待测假设，不是已知结论。

### 10.3 由此收敛的可测问题（评估协议，不是新机制）

**问题**：在相同 bit 预算与相同候选覆盖下，**scope 对齐的粗量化**（同 Node/同子树的对象倾向共享质心）是否提高 scope 内候选对的残差相关 $\rho$、降低冻结候选集上的翻转率，从而在不加比特的前提下改善 recall？

必须报告的量（缺一不可）：

1. scope 内候选对的经验残差相关 $\rho$ 与“独立性比” $(\mathrm{Var}\,\xi_x+\mathrm{Var}\,\xi_y)/\mathrm{Var}(\xi_y-\xi_x)$；
2. 标准化 margin $M=c_Q\gamma/\nu$ 的分箱翻转率（原文 Table 6 的协议：$M<0.25$ 时约一半翻转，$M>5$ 无翻转）；
3. 端到端 recall / p95 / 峰值 RSS；
4. held-out 块的**选择性失败率**（而非只看平均值）。

**否证条件**：若 scope 对齐在固定 bit 下没有提高 $\rho$ 或没有降低翻转率，则该方向关闭，量化只作为纯内存压缩基线使用。

**诚实边界**：**“按标签/属性分区再量化”不是新机制。** Filtered-ScaNN 的 IVF 分区、[ELI](https://www.vldb.org/pvldb/vol19/p617-yang.pdf) 的按标签子索引、[iRangeGraph](https://arxiv.org/abs/2409.02571) 的分段共享图都已按属性/范围组织索引。本项目可辩护的增量若存在，只在**目录层级的 scope 语义 + 决策级翻转度量 + 维护期失效**这一组合上，且尚未充分排查。

### 10.4 证书角度的前序与剩余缝隙

| 工作 | 已核内容 | 与本项目的关系 |
|---|---|---|
| [VADER](https://arxiv.org/abs/2609.38053)，Chatzakis 等，arXiv v2；页面自述 VLDB 2027，正式卷期/DOI 未核实 | 本地[报告](../../../../papers/filtered-ann/2609.38053-vader/paper.report.md)：filter-aware GBDT recall 预测 + 早停，Filtered-ScaNN 上按 partition 粒度预测；未提供无条件逐查询证书 | “filtered + 声明式召回”已被占据；其逐查询召回仍有 $R_{QUT}=0.16$–0.35 |
| [2609.09854](https://arxiv.org/html/2609.09854) §6 Thm 15 | 未过滤 ANN 的 held-out block 选择性证书，$p_s\le(\hat U_{s,\tau}+\epsilon_n)/\hat C_s$ | 提供可直接搬用的统计工具；其 block 定义是 query，不是目录 scope |
| conANN（PVLDB 2025）、DARTH+（HAL 2026） | **仅见 VADER 参考文献条目，正文未读**；不得据此判断覆盖范围 | 待读；若已覆盖 scope 级证书，则本缝隙关闭 |

可能剩余的缝隙（**未证实**）：把证书的 block 定义在**目录 scope** 上，并处理 MOVE/MERGE 后统计量的**生命周期失效**——即“scope 的失败率证书在该 scope 拓扑改变后何时作废”。这与本项目已测的 HLG 维护问题是同一对象，但现有证书工作都在静态 query 分布上。

### 10.5 对实现计划的具体影响

1. **不实现** scope 均值校准（§8.3 撤回），不为其保留目录级摘要。
2. NodeANN 量化的第一版只做：全局共享码本 + 每对象一次编码 + 展开平方 ADC（§9.1）；先作为内存基线，不宣称加速。
3. 任何量化实验必须同时报告翻转率/margin，不能只报 recall 或压缩率；否则无法区分“码率不够”与“候选覆盖不够”。
4. 若后续做 scope 级证书，才引入目录级统计与 MOVE 失效逻辑；届时必须单独计量维护成本，不能并入已测 G3。
5. 上述全部仍是设计，**本轮未实现、未运行**；量化后端与环境准备完成后，命令由用户执行。

### 10.6 预检脚本（廉价否证，先于任何 codec 实现）

在写量化器之前，先用现有向量与目录树判断“同 scope 候选的量化误差是否更相关”。若答案是否定的，§10.3 的方向应直接关闭。

脚本：[experiments/scope_quantization_probe.py](experiments/scope_quantization_probe.py)。它只用 `base.f32`、`wiki_tree.txt`、`wiki_bindings.txt`、`test.f32`，不读 GT、不建图、不做检索。

方法：抽样向量上训练小 k-means（默认 256 中心、8 次迭代），用**仅质心重建**（0 残差 bit）作为最粗代理量化器；对抽样 scope 取“精确最近成员 vs 其余成员”配对，计算残差 $\xi=\hat d-d$ 的配对相关与独立性比，并与随机跨节点配对对照。

运行时（示例；`R` 为结果目录）：

```bash
cd /home/daihua/projs/2026040404-yiming/Agent-Vector-Database
PY=production/nested-attr-vector-search/work/venv/bin/python
$PY researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/experiments/scope_quantization_probe.py \
  --data codes/directory-aware-vector-attr-search/data/wiki_full \
  --out researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/data/scope_quant_probe.json
```

判定：若 `within_scope.pearson <= cross_scope.pearson`，或 `same_centroid_fraction.within_scope` 相对 `cross_scope` 无明显抬升，则关闭 scope 对齐量化方向，量化退回纯内存压缩基线。注意“同质心比例高”本身不是低翻转风险的证据，必须同时看相关系数与翻转率。

冒烟状态：脚本已用 `work/hlg/smoke`（12 个 4 维点、4 个质心）跑通全流程，输出结构符合预期；该次只有 2 对/组，相关系数为 `NaN`，**不构成任何研究结论**。全量预检尚未运行，由用户执行。
