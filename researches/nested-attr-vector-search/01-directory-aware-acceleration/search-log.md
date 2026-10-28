# Directory-Aware 文献检索与新颖性记录

以下条目迁自 vector-join 总检索日志，保留原日期、证据和未核实边界；本次归档不代表新增检索或实验。后续本方向的详细检索记录统一追加于此，跨主题索引仅保留链接。

## 2026-10-25 Directory-Aware 新问题背景与定义初筛

先查 `papers/papers.jsonl`、`relations.jsonl`、本地 `2606.16903v1` 原文/报告及 nested-attr 的 D0–D5 审计、LES 记录；再查 vector similarity join、vector join、similarity self-join、high-dimensional similarity join、inner-product similarity join 加 hierarchy/prefix/metadata 变体，随后查 filtered ANN 与目录相关组合。核对 [Directory-Aware v1 §II–V](https://arxiv.org/html/2606.16903v1)、[Fast AVJ v2 摘要](https://arxiv.org/abs/2603.16360)及 [VBASE 正式论文 §2,4](https://www.usenix.org/system/files/osdi23-zhang-qianxi_1.pdf)。网页结果中有字符串/集合/semantic join 同名项，未当几何向量 join 证据；检索未形成引用闭包，不能以“无直接命中”证明新颖。

产出[文件目录约束下跨集合阈值向量连接的 Problem Formulation](problem-formulation.md)：cross、L2、threshold、exact pairs，目录祖先可比过滤。Directory-Aware 的 DSQ 是单查询 top-k；现有 Fast AVJ/VBASE 是强前序。待查 filtered similarity join、hierarchical/dual-tree join、predicate-aware batch vector join 的直接正文与引用链，以及真实双库目录路径能否支持场景。没有新增已精读论文、关系或性能实验。

同日修订：将背景从单父文件目录树改为可通过共享入口访问同一逻辑目录的**静态虚拟目录 DAG**。参考 [Google Drive 文件夹快捷方式官方说明](https://support.google.com/drive/answer/9700156?hl=en) 的“引用而非复制”现象；DAG 是本项目的抽象限制，未把该产品的实际目录系统或权限实现等同于模型。更新 [Problem Formulation](problem-formulation.md)，增加 B0 全量成对验证、B1 在线目录范围、B2 物化目录范围、B3 精确向量优先四种基线及 DAG 多路径去重公式。D1 已有支持计数/增量维护前序，本问题暂不研究维护；没有新论文记录或实验。

同日再次修订：Google Drive 快捷方式仍属于文件目录背景，改为[多维商品目录 Problem Formulation](problem-formulation.md)。[OpenSearch 官方 faceted-search 教程](https://docs.opensearch.org/latest/tutorials/faceted-search/)以商品目录说明品类、品牌、颜色、尺码等多维筛选；将筛选状态建模为部分属性函数与按包含关系组织的目录偏序是本项目的形式化选择，不宣称 OpenSearch 实现了本文连接。新问题为跨平台、部分属性兼容、L2 阈值、精确 pairs；基线 B0–B4。商品匹配、缺失属性下记录连接及 filtered vector join 的最近工作仍需补查，不能以背景不同证明算法新颖。没有新增论文记录或实验。

补充候选：产品实体匹配已有[莱比锡大学研究页](https://old.dbs.uni-leipzig.de/en/publication/title/tailoring_entity_resolution_for_matching_product_offers)讨论缺失/错误属性；[EM-Join 原始论文](https://www.scitepress.org/Papers/2025/134837/134837.pdf)涉及 embedding-based entity matching join。当前只核题名/摘要或入口，未核其完整定义、阈值语义和实验；列为后续精读候选，不把商品匹配场景本身宣称为新增研究问题。

同日层级语义修正：核 [OpenSearch 官方 faceted-search 教程](https://docs.opensearch.org/latest/tutorials/faceted-search/) §Hierarchical faceting 与普通 facet 过滤，前者明确 Category→Subcategory→Product type，后者为品牌/颜色/尺码等独立过滤字段。当前 [Problem Formulation](problem-formulation.md) 明确定义品类树 × 属性部分函数；整体目录状态并非单父树。前版只有 facet 约束偏序，不能称为明确的品类层级。五种基线已同步新的品类可比条件；未运行实验。

## 2026-10-28 层级DSQ候选机制与前序核查

本轮是单查询、scope内top-k的B类可迁移检索实验，不是几何vector join。先查论文总索引、关系索引、TrieHI全文笔记及既有新颖性审计，再搜`vector similarity join`、`high-dimensional similarity join`、`inner-product similarity join`的hierarchical/prefix/partition变体；随后查filtered ANN hierarchical partition/packing、DFS范围组织、partial-distance与局部索引合并。广义Curator检索混入NVIDIA NeMo同名结果，未当作向量索引证据。

本地原文证据：[iRangeGraph](https://arxiv.org/abs/2409.02571) `paper.md:407-520` 的范围/segment-tree共享图；[Curator](https://arxiv.org/abs/2601.01291) `paper.md:450-500,650-715` 的子树连续ID与按需谓词索引；Packing相关块组织沿用既有全文记录。VA-file原始PDF本轮未成功读取，不能把snippet当精读。没有新增未经完整书目核实的论文条目，既有D0–D5停止/暂停决定不变。

实际完成12轮冻结WIKI实验，见[报告§9](2026-10-28-exps/report.md#9-autoresearch-静态dsq迭代)。scope-run/tile执行候选有效，但DFS区间、bulk scan、SIMD、partial-distance及局部top-k合并都不是单独新贡献；tile cutoff组合的新颖性仍未充分排查。未运行holdout、多数据集或动态维护，不宣称覆盖所有前序。

## 2026-10-28 NodeANN递归路由诊断与APS前序

用户明确SIMD/布局不算创新，本轮将其仅作为工程基线。先查论文/关系索引、TrieHI/Curator笔记及D3新颖性审计，再搜vector similarity join/high-dimensional similarity join/inner-product similarity join + partition/distance bounds，随后查adaptive partition/node search、recall budget与Quake。范围为单查询filtered top-k，不冒称join。

新候选：[Quake: Adaptive Indexing for Vector Search，OSDI2025正式论文](https://www.usenix.org/system/files/osdi25-mohoney.pdf)，另有[arXiv:2506.03437](https://arxiv.org/abs/2506.03437)。本轮原文核到§2.3和§5 Eq.7–9/Alg1（PDF含封面页8–9）：uniform-density与Voronoi半空间近似下预测分区含近邻概率，利用当前第k距离更新概率并按估计recall停止。尚未完整精读理论、实验或引用闭包，列入待精读，不填造完整论文索引或精确recall保证。SPANN/LAET/Auncel为Quake正文引用的继续追查线索，未按二手描述认定完整等价。

实际节点诊断见[报告§10](2026-10-28-exps/report.md#10-用户新问题递归查询能否只搜索有希望的node)：质心截断有较大漏召回；单球best-first保住GT但向量剪枝少。目录语义Node非Voronoi几何分区是待检验差异，不足认证新颖；多球/学习router/federated shard selection仍需补查。没有新算法SOTA或新颖性声明。

## 2026-10-28 树-图协同、双空间剪枝与图嫁接的前序核对

用户提出以TrieHI"只做元数据层"为缺口的三个方向：目录层级与图对齐、双空间包络剪枝、MOVE图嫁接。先查本地论文索引/笔记与既有D2/D3决定，再核公开原文相关章节。

已读本地原文/报告：Grant（2610.11563，属性平衡树+叶子图+概率节点验证；条件性次线性界）`paper.report.md`；Graph Range Filters（2607.00727，过滤图上distance-aware labeling）`paper.report.md:1-28`；ACORN（2403.04871，谓词子图遍历+邻域扩展，概率性可导航分析）`paper.report.md`；Filtered-DiskANN（WWW2023，StitchedVamana/FilteredRobustPrune，单标签为主，AND为future work）`paper.report.md`；ELI（PVLDB 19(4)，按选择比弹性选子索引）`paper.report.md`。在线实读Quake OSDI2025 §2.3、§5 Eq.7–9/Algorithm 1（APS：Voronoi半空间+uniform-density概率，在线更新第k距离并停止）。

判断：树+图联合组织、过滤图连通性修复、按选择比选索引/分区、球界剪枝均有直接前序；"无条件的任意子树有界连通度"不成立，需条件化或按子树加边（后者已发表）。真正空白在**目录MOVE/MERGE与向量图的一致性维护**，但须先证明不修图会造成质量损失。

本轮实测见[报告§11](2026-10-28-exps/report.md#11-审稿人视角树-图协同双空间剪枝与图嫁接的可检验性)：过滤图在高选择率下召回塌陷（PG 0.032–0.040）而非延迟上升；精确scope扫描在大scope尾部占99.6%时间（7.06ms@16.8k）；语义Node几何cohesion仅0.80–0.90且多球仅收缩1–4%，球界只裁9.45%向量。未实现新图索引，无SOTA或新颖性声明。

## 2026-10-28 IGP 量化、NodeANN 与离线 GPU

先查 `papers/papers.jsonl`、`relations.jsonl` 和本方向报告/全文笔记，无IGP、iFVS、LOPQ、SymphonyQG现有完整索引条目。先检索 `vector similarity join`、`high-dimensional similarity join`、`inner-product similarity join` + quantization/compressed/residual，并补 `vector join`、`similarity self-join` 的SQ/PQ/残差变体；随后查 filtered ANN、hierarchical/local/partitioned quantization、quantized graph/refinement。没有把多向量MaxSim或普通ANN称为几何join，也不以无命中证明新颖性。

核心核查：[IGP 作者全文](https://www4.comp.polyu.edu.hk/~csmlyiu/conf/SIGIR25_IGP.pdf) §3.1–3.2、§4–4.2、Table6，Crossref DOI `10.1145/3726302.3730004`，及用户指定仓库固定commit `1899e9316e71b023518f9090fc64fe66dbd0c202` 的CPU入口、共享VQ/SQ训练/打包/解码、质心图、GPU encoder。量化沿用ColBERTv2；当前CPU源码是GIGP+的较快单线程版本，不能与原2025配置混写。GPU训练/assignment/编码存在，非GPU HNSW构图证据；重建不normalize、精排不取raw FP32，迁移须保留原squared-L2。[完整调研与源码定位](2026-10-28-exps/igp-quantization-assessment.md)。

继续核到直接前序：[iFVS arXiv:2607.22922v1](https://arxiv.org/html/2607.22922v1) §2–4，已做固定PQ codes、query+predicate条件码本及维度权重；正式venue未核实，HTML workshop模板不完整，不当作正式PVLDB论文。其GPU推理与本项目CPU测量不能直接比较。作者future work为unseen-query泛化及workload-shift自调整；目录MOVE下可合并低秩误差摘要是本项目综合推导的待验假设，不写成作者future work或已证空白。

背景核查：[LOPQ CVPR2014原文](https://www.cv-foundation.org/openaccess/content_cvpr_2014/papers/Kalantidis_Locally_Optimized_Product_2014_CVPR_paper.pdf) §4.1/§5.4/§6 的局部residual量化及模型/LUT开销；[SymphonyQG arXiv v1](https://arxiv.org/html/2411.12229) §2.2/§3.1–3.2 的量化图与隐式精排；[low-bit decision study 2609.09854](https://arxiv.org/abs/2609.09854)仅核摘要，完整证明仍待读。多尺度量化/VA-file/RaBitQ及动态谓词编码的引用链未闭合，尚未充分查新。

本轮没有运行训练/建图/编码/检索实验；仅审计既有CSV/退出记录、查询GPU设备/包metadata并做解析内存计算。新增日期化[IGP书目/方法审计](2026-10-28-exps/data/igp-literature-audit-2026-10-28.json)、[直接前序审计](2026-10-28-exps/data/quantization-prior-art-audit-2026-10-28.json)，不声称新增完整全篇论文索引。后续详细研究仍只存本方向目录，实验由用户按命令执行。

