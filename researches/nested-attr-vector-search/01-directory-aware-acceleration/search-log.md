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

### 2026-10-28 续：决策级翻转理论使一条设计被否证

补读 [2609.09854](https://arxiv.org/html/2609.09854) 正文 §1、§3.1–3.3、§5、§7 Table 4、§8.2–8.3（附录抽样）。该文：翻转概率 ≤ 边界质量 + 残差尾；排序差分残差尺度 $\nu^2=K_{xx}+K_{yy}-2K_{xy}$；仿射标定截距在排序差分中抵消、仅在 $\alpha\ne1$ 剪枝存活；held-out block 选择性证书（Thm 15）；Table 4 显示 int4 在最难20% margin 仍翻 10.1%。其范围是未过滤固定候选集，不涉及目录 scope 或过滤谓词。

由此**撤回**先前“按 scope 做均值/低秩均值校准”的假设：DSQ 比较全在同一 scope 内，公共加性偏差必然抵消，均值校准是排序 no-op。推论：原计划的目录级摘要（约 67.4 MB 算术值）没有排序动机；该内存账仅留作记录并已标记 refuted。当前方向改为：在固定 bit 下测 scope 对齐粗量化能否提高 scope 内候选残差相关 $\rho$、降低翻转率；这是评估协议问题，且 label-aware 分区已有前序（Filtered-ScaNN/ELI/iRangeGraph），不称新机制。

证书角度另核本地 [VADER 报告](../../../papers/filtered-ann/2609.38053-vader/paper.report.md)（arXiv 2609.38053v2，页面自述 VLDB 2027，正式卷期/DOI 未核实）：filter-aware GBDT 召回预测 + 早停、按 partition 粒度预测、无无条件逐查询证书、$R_{QUT}$ 0.16–0.35。conANN（PVLDB 2025）与 DARTH+（HAL 2026）本轮只见 VADER 参考文献条目，正文未读，不判断覆盖。可能残留缝隙是“以目录 scope 为 block 的证书 + MOVE/MERGE 后失效”，**未证实**。

为使上述判断可执行，新增 [scope_quantization_probe.py](2026-10-28-exps/experiments/scope_quantization_probe.py)：质心-only 重建当最粗代理量化器，比较同 scope 与跨节点配对对的残差相关/独立性比/翻转率。脚本已在 `work/hlg/smoke`（12 点、4 维、4 中心）冒烟跑通，非研究结论；全量预检待用户执行。

## 2026-10-28 属性频繁更新下的层级过滤 ANN 初筛

先用 `update`、`incremental`、`mutable`、`dynamic`、`MOVE/MERGE` 查本地 `papers/papers.jsonl` 与关系索引；已收录 Filtered-DiskANN（静态构图）、iRangeGraph（tmp index）、TrieHI（MOVE/MERGE 元数据维护）。随后按用户给的两条链接核全文。

**链接纠正**：`arxiv.org/abs/2604.22171` 不是 “Filter Index Updates”，而是 **MCI: A Maximal Clique Index for Efficient Arbitrary-Filtered ANNS**（Xiaowei Ye, Rong-Hua Li, Guoren Wang, Kaiwen Xue, Daiyin Wang, Xubin Li；arXiv v1 2026-04-24；正式 venue 未核实）。其 Table 1 的 “Upd.” 列（表注：构建后能否更新特征值而不重建）把 Filtered-DiskANN、NHQ、UNG 标为 ✗；附录 A 又自述当前实现为静态构建、动态策略留作 future work。**结论：MCI 不能作为“动态已解决”证据，但确实支持“Filtered-DiskANN 不支持属性更新”的表述。**

**冲突记录**：[EMA](https://arxiv.org/html/2606.00734v1) Table 1 却把 Filtered DiskANN 标为 Dynamic ✓。两篇 2026 论文对同一事实判断相反；Filtered-DiskANN 原文该点本轮**未核**，故不采信任一方结论。

[EMA](https://arxiv.org/abs/2606.00734v1)（Mocheng Li, Baotong Lu, James Cheng, Chenhao Ma；v1 2026-05-30；注释为提交 PVLDB Research Track，未核实接收）已核 §3.1–3.5、§4、§5.4–5.5：边级 Marker 汇总属性分桶；属性变更只对一跳反向边做 OR 合并；删除 lazy + 20% 触发 patch、每增 10% 再 patch、50% 全量重建；实测删除 1.3 s/1M、属性-only 2.5 s/1M、插入 90.8 s/1M、patch 45.2 s/次 ≈ 全量重建 371.1 s 的 12%。OR 单调、无法回收被删除属性的置位，这是其阈值重建的结构性原因（本文推断，非作者原话）。

**动态 range 已非空白**：MCI §2.2 称 DSG、DIGRA、RangePQ+ 支持动态特征更新；书目另见 DIGRA（PACMMOD 3(3) 148, doi:10.1145/3725399）、Efficient Dynamic Indexing（PACMMOD 3(3) 152, doi:10.1145/3725401）、Dynamic range-filtering ANN（PVLDB 18(10) 3256–3268）。正文均**未读**，仅记录存在。

**判定**：数值 range + 动态、flat label + 动态（EMA）都已有前序；“解耦 ANN 与属性索引”只能当基线。可辩护窄缝收敛为**祖先闭包（层级）谓词 + 高频 MOVE/重标注 + 查询快照一致性 + 可撤销聚合**，其结构优势是更新只沿一条祖先路径传播（$O(h)$），而边级摘要必须动 $O(M)$ 条边且无法撤销。方案、U1–U5 否证条件与指标见[专项记录](2026-10-28-exps/dynamic-attribute-updates.md)。本轮未实现、未运行；不声称该子问题无人研究，检索链未闭合。

