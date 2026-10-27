# Fig 7–12 补全与全量复现环境搭建（2026-10-26 晚）

## 目标

按交接清单补全 TrieHI（arXiv:2606.16903）§V 全部实验图：既有 `2026-10-26-triehi-repro-results.md` 只覆盖 Table IV 对应实验 + Fig 9 聚合柱 + Table V；缺 Fig 7 逐点 trade-off、Fig 9 逐 op 序列、Fig 10 结构复杂度、Fig 11 深度敏感性、Fig 12 目录解析延迟分解。Table VI/VII（OpenViking 端到端 QA）依赖外部 LLM judge 与 benchmark 配置，本轮不做，理由见末尾。

## 本机环境（首次具备全量条件）

| 项 | 旧机（交接记录） | 本机 |
|---|---|---|
| RAM | 11 GB | **98 GB**（可用 87 GB） |
| 核 | — | 16 |
| FAISS | PathSeer fork 已构建 | 重新构建（libfaiss.so，223 s，两处已知修复） |
| BLAS | 有 | 缺 → `sudo apt install libopenblas-dev`（dpkg 在 openssh-server 配置脚本报错退出，但 openblas 库与头文件已正确安装） |
| CRoaring | v4.3.1 | clone v4.3.1 构建，`deps/croaring` 符号链接 → `deps/CRoaring` |
| 数据集 | 仅 13 个 ann-benchmarks HDF5 | 同 + WIKI-Dir 下载中（见下） |

`build.sh` 修正：croaring include/lib 路径原为 `deps/croaring`（相对脚本目录），改为 `../../deps/croaring`（仓库根）。

## 代码扩展（codes/directory-aware-vector-attr-search/src/repro.cpp）

1. **Design::resolve 阶段计时**（Fig 12）：新增 `t_enum_us`（子路径/子目录枚举）与 `t_bitmap_us`（posting 读取 + 集合运算）。PE-Online recursive 分"子树枚举 / posting OR"两段；物化设计的 non-recursive 分"直接子枚举 / inc 复制+andnot"。
2. **per-query 输出**（Fig 11/12）：`results_dsq_perquery.csv` = dataset,design,executor,query_type,anchor_depth,scope_card,dir_us,enum_us,bitmap_us,e2e_ms,recall10,ndcg10（每查询一行，100k 共 9000 行）。
3. **dsq_scan 模式**（Fig 7）：`--ef-list`、`--nprobe-list` 参数扫描，PG 用 efSearch 列表、IVF 用 nprobe 列表；GT 每工作负载只算一次。输出 `results_dsq_scan.csv`。已修复：scan 模式不再重复执行 run_group、不再截断其它 CSV（各 CSV 仅在其写入模式下打开）。
4. **DSM 逐 op 输出**（Fig 9）：`results_dsm_perop.csv` = design,op_index,op_kind,latency_ms。

绘图 `scripts/plot_repro.py` 新增 5 个图函数（fig7_tradeoff / fig9_dsm_perop / fig10_structure / fig11_depth / fig12_breakdown），默认路径同步到新目录结构。

## 100k 合成复跑结果（SIFT100k，seed 20261026）

mode all（96–105 s）：DSM audit 全 ok；排序结论与前轮一致（TrieHI < PE-Offline < PE-Online；wiki recursive 的 PE-Online 目录延迟高一个量级以上）。mode dsq_scan（474 s）：ef ∈ {32..2048} 七档、nprobe ∈ {4..1024} 五档，scan CSV 145 行。

产出 9 张图（figures/，png+svg）：

| 图文件 | 对应论文 | 数据源 |
|---|---|---|
| fig_repro_fig7_tradeoff | Fig 7 递归质量-延迟 | results_dsq_scan.csv |
| fig_repro_fig9_dsm_perop | Fig 9 MOVE/MERGE 逐 op | results_dsm_perop.csv |
| fig_repro_fig10_structure | Fig 10 深度结构复杂度 | wiki_tree + workload 现场统计 |
| fig_repro_fig11_depth | Fig 11 深度敏感性（PG/IVF 双子图） | results_dsq_perquery.csv |
| fig_repro_fig12_breakdown | Fig 12 解析延迟分解（递归/非递归） | results_dsq_perquery.csv |
| fig_repro_dir_latency / ndcg_buckets / dsm / dir_size | Table IV / Fig 8 / Fig 9 聚合 / Table V | 既有 CSV |

Fig 7 观察（100k 合成）：wiki-IVF 与 arxiv 两子图中 PE-Offline 与 TrieHI 曲线近重合——与论文"recursive 下两者同一权衡"一致；wiki-PG 中低 ef 区三设计交织，原因是 100k 合成的锚深度浅（≤4）、子树规模小，PE-Online 展开开销未占主导（论文 WIKI-Dir 均深 11.95 才出现 365 ms 级展开）。**Fig 7 的论文形态需真实 WIKI-Dir 才能对齐**，100k 图仅验证机制方向。

## 全量 WIKI-Dir / ARXIV-Dir（进行中）

- gdown 下载（SOURCES.md D 节文件 ID）：wiki corpus_vectors.fvecs 7.96 GB 下载中（2.75/7.96 GB），arxiv 排队。⚠ 发布方文件名两处说谎已按 SOURCES.md 处理（query_vectors/corpus_vectors 名字互换；arxiv_query_constraint.json 是查询向量）。
- 本机 98 GB RAM 足以全量：1.94M×1024 f32 ≈ 7.9 GB + 2.76M×1024 ≈ 11.3 GB，加 HNSW/IVF 索引与目录位图仍宽裕（旧机 11 GB 只能 400k 子采样）。
- 跑法：build_real_fixtures.py 重建索引输入 → repro mode all + dsq_scan（dim 1024）。预计 wiki DSQ+DSM 全量 ~20–40 分钟（GT 暴力 1500 查询 × 1.94M×1024 距离是主要成本，单线程约 0.5–2 ms/距离×... 实测为准；必要时 GT 可换 faiss IndexFlatL2 批量搜索加速）。

## 遗留

1. 真实全量 Fig 7–12 与 Table IV/V：下载完成后跑，数字与图追加到本文档。
2. Table VI/VII（OpenViking QA）：需 LOCOMO/HotpotQA 基准 + doubao-seed LLM-as-judge + OpenViking 完整系统（论文只开源 benchmark 配置在 OpenViking 仓库），非本仓库 FAISS/C++ 基线可复现范围；标注为**环境依赖项，未复现**。
3. ARXIV-Dir 双命名空间（subject+temporal）：build_real_fixtures.py 支持情况待下载后确认。

---

# 全量/真实数据结果（2026-10-27 补完）

## 数据获取与适配

- **wiki corpus_vectors（7.96 GB）**：Google Drive 触发"too many users"24h 配额，四次尝试后稳定在 4.22 GB（1,029,778 条完整 fvecs 记录，尾部 2 条半记录截断）。提取完整记录 → 子采样 1,000,000 条（seed 20261026，重编号）作为 ANN 端到端输入；**全量 1.94M 的目录解析与 DSM 实验不依赖向量，已用全量目录树完成**。全量向量待配额恢复后补（`data/real_fixtures/wiki/corpus_vectors.partial` 保留）。
- **arxiv 语料向量**：发布方未提供下载（SOURCES.md D 节无此文件，F 节亦无）；arxiv 只完成目录解析（scope 模式），ANN 端到端不可跑。
- 发布方文件名与 build 脚本期望名的差异全部用符号链接适配（`corpus_paths.jsonl`→`dbpedia_dir_2m_corpus_paths.json`、`arxiv_category_constraint.json`(116KB，SOURCES.md 标 29MB 有误)→`arxiv_category_query_constraint.json` 等）；`corpus_paths.jsonl`/`corpus_metadata.jsonl` 实为 JSON array，`arxiv_query_constraint.json` 实为查询向量 fvecs（与 SOURCES.md 记录一致）。
- **数据集核对**：arxiv 构建出 2,763,543 条目、subject 168 / temporal 432 目录、max depth 3/2，与论文 §V-A 完全一致；GT 校验 1000/1000 查询全在 scope 内。wiki 构建出 1,941,679 条目、**468,031 目录**（论文 363,467）、max depth 31——目录数多 28.7%，因为 build 脚本对每对象的全部路径建 trie，未复现论文"取一规范路径 + 丢弃 invalid links"的裁剪；目录解析绝对值因此小于论文，排序不受影响。workload 457 查询（生成器 round 累积，论文 456）。

## Table IV 对应：全量目录解析延迟（directory-only，µs mean，单线程）

| 数据集 | 查询类型 | PE-Online | PE-Offline | TrieHI | 论文排序一致 |
|---|---|---|---|---|---|
| wiki 全量 1.94M/468k 目录 | recursive | **1964.93** | 2.57 | 3.22 | ✓（论文 365000 / 235 / 273） |
| wiki 全量 | non-recursive | **0.95** | 44.21 | 50.12 | ✓（论文 29 / 1037 / 945） |
| arxiv 全量 2.76M/168+432 | recursive | **852.20** | 8.93 | 9.66 | ✓（论文 1089 / 0.12 / 0.43） |
| arxiv 全量 | non-recursive | **6.27** | 30.57 | 31.44 | ✓（论文 4.35 / 164 / 112） |

物化设计（Offline/TrieHI）的 recursive 比论文快 1–2 个数量级：论文 WIKI 浅层锚点展开集巨大（Fig.10 depth-1 m=236,903/363k 目录），我们 468k 目录树上同位置 m≈10,800（Fig 10 复现图）且 posting 位图更小；arxiv 浅树（均深 ~2）下单次查找本就微秒级。相对关系（PE-Online 在 recursive 慢 2–3 个数量级、non-recursive 快 1.5–2 个数量级）与论文一致。

## Fig 9 对应：DSM 墙钟（2000 ops，ms mean）

| 运行 | MOVE PE-Online / PE-Offline / TrieHI | MERGE 三设计 |
|---|---|---|
| wiki 全量 1.94M（bg_3） | 38.1 / 14.9 / **2.75** | 0.077 / 0.431 / 0.120 |
| wiki 1M 子采样（bg_9 复跑） | 17.1 / 5.90 / **2.38** | 0.039 / 0.129 / 0.095 |

TrieHI MOVE 最快，排序一致；PE-Online MOVE 运行间方差大（38→17 ms），因其代价 ∝ 被移动子树的 entry 数，op 序列中偶发大子树主导 mean。逐 op 序列见 `real_wiki1m_scope_dsm_perop.csv`（Fig 9 复现图 `fig_repro_fig9_dsm_perop`）。DSM audit 全 ok。

## Table V 对应：索引构建与大小

| 项 | PG-hnsw-M16 | IVF-nlist1024 |
|---|---|---|
| wiki 1M 向量索引构建 | 779 s | 45 s |
| wiki 1M 向量索引大小 | 4240 MB | 4108 MB |
| 目录模块（全量 468k 目录） | PE-Online 44.6 MB / PE-Offline 94.9 / TrieHI 70.0；构建 0.14/0.77/0.58 s | 同左 |
| 目录模块（1M 子采样） | 37.1 / 64.8 / 40.0 MB | 同左 |

目录模块构建增量 ≪1.7%（论文结论）；TrieHI 存储 > PE-Offline > PE-Online（论文结论）。

## Fig 7/8 对应：wiki 1M 端到端（ef2048/np256 固定点 + 参数扫描）

固定点：PG e2e 32.4 ms / recall@10 0.107；IVF 3.09 ms / 0.168。三设计质量逐点相同（scope 语义等价），延迟差异体现在 directory-only（PE-Online recursive 616 µs vs 物化 20–28 µs）。`dsq_scan`（ef 64→4096 七档、nprobe 4→1024 五档，73 行）复现图 `fig_repro_fig7_tradeoff`：**PE-Offline/TrieHI 曲线整体位于 PE-Online 左下方（同质量更低延迟）**，与论文 Fig 7 论断一致；arxiv 两子图标注 N/A（向量不可得）。wiki 低召回是深锚点小 scope 的指标饱和（全扫描 ndcg=1.0 已验证），非执行器缺陷。

## 产物清单（本目录）

- 图：`figures_real_wiki1m/fig_repro_*.{png,svg}` ×9（100k 版图在 `figures/`）
- 数据：`codes/directory-aware-vector-attr-search/experiments/real_{wiki_full,arxiv_full,wiki1m,wiki1m_scope}_*.csv`
- fixture：`codes/.../data/real{,_arxiv}/`（全量树/绑定/workload）、`data/real_fixtures/wiki1000000/`（1M 向量 + 重编号）
- 原始数据（gitignored）：`data/real_fixtures/{wiki,arxiv}/`，wiki 向量保留 `corpus_vectors.partial`（4.22/7.96 GB）待配额恢复续传

## 仍遗留

1. wiki 全量 1.94M 的 ANN 端到端 + Fig 7–12 全量图：等 Drive 配额恢复补下 3.74GB 向量后，`fvecs_to_f32.py` 转 base.f32 + 全量 fixture 直跑（目录 fixture 已就绪）。
2. arxiv 语料向量：发布方未发布，ANN 部分不可复现；目录解析与 DSM 结论已覆盖 Table IV 的 arxiv 列。
3. Fig 12 的真实数据分解图基于 1M perquery（`results_dsq_perquery.csv` 2742 行）；全量目录+全量向量的分解待向量补齐后重跑 mode all 自动产出。
