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
