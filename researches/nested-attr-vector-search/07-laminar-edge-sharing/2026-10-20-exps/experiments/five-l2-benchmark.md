# 五个补充负载：固定工作点与同召回对照

日期：2026-10-20。目的：在现有向量数据上补充五个评估点，量化 LES 相对逐作用域图、过滤图、精确扫描和 Directory-Aware 目录元数据的实际位置。全部层级标签、查询作用域和 oracle **均为合成**（`prep_ccs.py`，1365 节点 4 叉深度 5，seed 20261021），沿用 H2 的生成语义；没有任何真实层级标注。

## 1. 五个评估点与来源

| 评估点 | 向量来源 | n | d | 说明 |
|---|---|---:|---:|---|
| sift25k | `datasets/sift-128-euclidean.hdf5` train 前 25,000 | 25,000 | 128 | 与 sift50k/100k 是**同一语料的前缀嵌套子集** |
| sift50k | 同上，前 50,000 | 50,000 | 128 | 含 sift25k 全部向量 |
| sift100k | 同上，前 100,000 | 100,000 | 128 | 含上述两者；与既有 H2 的向量集合相同 |
| mnist25k | `datasets/mnist-784-euclidean.hdf5` train 前 25,000 | 25,000 | 784 | 独立语料 |
| fashion-mnist25k | `datasets/fashion-mnist-784-euclidean.hdf5` train 前 25,000 | 25,000 | 784 | 独立语料 |

三个 SIFT 点是**同一语料的规模切片**，不是三个独立数据集；MNIST 两套是不同语料但同维度、同规模。所有取值都是 `[0,255]` 内的整数，float32、u8 与平方 L2 顺序无损一致（导出脚本逐块校验）。测试向量取完整 10,000 条，`prep_ccs` 固定抽取 1,000 条并把每查询可行集约束在 ≥64。

## 2. 协议

- 每个评估点只生成一次 fixture；查询为 1000 条混合 Subtree（70% 单节点、30% 2–3 节点反链），k=10。
- PSG：逐作用域 HNSW，M=24、efConstruction=200、ef=21。
- LES：以 PSG 图为底图做 level-0 边归属去重，ef=32（另测 8/16/24）。
- BF-HNSW：FAISS HNSW M=32、efC=200、ef=384（另测 512/768），查询位图**预计算**。
- TRIEHI+HNSW：同一份序列化 BF-HNSW 图、同一 ef，查询时用节点 inclusive bitmap 现场解析 $U$ 得到候选位图；候选集在计时前逐查询与 oracle 位图比对，1000/1000 一致。仅静态 Subtree，无路径字符串解析、无 DSM。
- PFS：直接标签 posting + u8 穷举距离，精确（Recall@10=1）。
- PathSeer：`deps/PathSeer` 的 `IndexPathSeerFlat`，M=32、M_exp=64、efC=200、ef=384、M_exp_search=16；未使用其自动虚拟开销边界，**不是完整 PathSeer 配方**。
- 所有进程 `OMP_NUM_THREADS=1`、`OPENBLAS_NUM_THREADS=1`；默认线程数的旧数据不与此表混用。
- 计时为查询循环内最好一次（2 次重复），不包含 fixture 生成、索引加载、FAISS 位图构造、PFS posting 构造；构建时间单独计。
- 空间口径不同：PSG 为 `c_*.bin` 磁盘字节；LES 为整个输出目录（含 u8 向量与逐作用域上层）；FAISS 为序列化 float32 图；TRIEHI 的 `metadata_bytes` 是内存 bitmap，**不与磁盘字节相加**。

复现：

```bash
PY=production/nested-attr-vector-search/work/venv/bin/python
$PY production/nested-attr-vector-search/src/export_five_l2.py
$PY production/nested-attr-vector-search/src/run_five_l2.py --with-pathseer --with-triehi
$PY production/nested-attr-vector-search/src/run_five_l2_sweep.py \
  --run <run_id> --datasets sift25k,sift50k,sift100k \
  --psg-ef 32,48 --les-ef 8,16,24 --faiss-ef 512,768
$PY production/nested-attr-vector-search/src/plot_five_l2.py --csv <run.csv>
```

原始数据：`data/five_l2_20261020T104139Z-68414a0f.csv`（固定工作点）、`data/five_l2_sweep_20261020T104139Z-68414a0f.csv`（同召回扫描）、`data/five_l2_20261020T104139Z-68414a0f.json`（命令、哈希、口径）。原始日志：`experiments/five_l2/20261020T104139Z-68414a0f/`。

失败记录：`20261020T104017Z-43e4c8ff` 在 sift25k 的 LES 查询阶段因把无头 `fixtures/test.f32` 当带 8 字节头的查询文件读入，触发 `std::bad_alloc`；修正为传 `pathseer/<name>/query/query.bin`。该失败 run 的 CSV/JSON/日志均保留，未删除。`20261020T104059Z-2dd71e88` 是修正后的单点 smoke run。

## 3. 固定工作点结果

| 数据点 | 方法 | Recall@10 | QPS | p95 (ms) | 索引 (MB) | 构建 (s) |
|---|---|---:|---:|---:|---:|---:|
| sift25k | PFS | 1.0000 | 16,054 | 0.191 | – | – |
| sift25k | PSG ef21 | 0.9784 | 113,003 | 0.019 | 48.7 | 11.28 |
| sift25k | LES ef32 | 0.9992 | 22,569 | 0.094 | 31.5 | 12.35 |
| sift25k | BF-HNSW ef384 | 0.9730 | 2,355 | 0.662 | 19.6 | 4.33 |
| sift25k | TRIEHI+HNSW | 0.9730 | 2,558 | 0.571 | 19.6 + 4.27 元数据 | 4.33 |
| sift25k | PathSeer | 0.9793 | 2,346 | 0.641 | 26.3 | 7.92 |
| sift50k | PFS | 1.0000 | 7,829 | 0.427 | – | – |
| sift50k | PSG ef21 | 0.9714 | 95,347 | 0.022 | 97.4 | 25.38 |
| sift50k | LES ef32 | 0.9984 | 21,243 | 0.099 | 61.7 | 27.59 |
| sift50k | BF-HNSW ef384 | 0.9654 | 2,191 | 0.633 | 39.2 | 11.10 |
| sift50k | TRIEHI+HNSW | 0.9654 | 2,230 | 0.630 | 39.2 + 8.54 元数据 | 11.10 |
| sift50k | PathSeer | 0.9744 | 2,082 | 0.669 | 52.6 | 20.07 |
| sift100k | PFS | 1.0000 | 2,222 | 1.445 | – | – |
| sift100k | PSG ef21 | 0.9564 | 74,304 | 0.030 | 199.8 | 60.08 |
| sift100k | LES ef32 | 0.9970 | 14,471 | 0.148 | 124.1 | 65.10 |
| sift100k | BF-HNSW ef384 | 0.9534 | 1,654 | 0.861 | 78.4 | 29.55 |
| sift100k | TRIEHI+HNSW | 0.9534 | 1,644 | 0.844 | 78.4 + 17.07 元数据 | 29.56 |
| sift100k | PathSeer | 0.9622 | 1,359 | 1.071 | 105.3 | 54.52 |
| mnist25k | PFS | 1.0000 | 2,343 | 1.380 | – | – |
| mnist25k | PSG ef21 | 0.9882 | 45,944 | 0.054 | 218.5 | 16.21 |
| mnist25k | LES ef32 | 0.9996 | 6,751 | 0.399 | 43.8 | 17.43 |
| mnist25k | BF-HNSW ef384 | 0.9524 | 1,154 | 1.282 | 85.2 | 13.19 |
| mnist25k | TRIEHI+HNSW | 0.9524 | 1,112 | 1.337 | 85.2 + 4.27 元数据 | 13.20 |
| mnist25k | PathSeer | 0.9678 | 1,024 | 1.479 | 91.9 | 27.37 |
| fashion-mnist25k | PFS | 1.0000 | 2,002 | 1.723 | – | – |
| fashion-mnist25k | PSG ef21 | 0.9953 | 48,790 | 0.049 | 218.6 | 12.93 |
| fashion-mnist25k | LES ef32 | 0.9995 | 7,824 | 0.284 | 39.4 | 13.94 |
| fashion-mnist25k | BF-HNSW ef384 | 0.9442 | 1,375 | 1.054 | 85.2 | 10.19 |
| fashion-mnist25k | TRIEHI+HNSW | 0.9442 | 1,456 | 0.958 | 85.2 + 4.27 元数据 | 10.19 |
| fashion-mnist25k | PathSeer | 0.9603 | 1,375 | 1.036 | 91.9 | 23.59 |

全部行的 `violations=0`、`feasibility_violations=0`、无重复 id。BF-HNSW/TRIEHI 在 2–8% 查询上返回不足 10 个结果（`full_k_queries` 92–98），LES、PSG、PFS 全部填满。

## 4. 同召回对照（五个点）

固定工作点的召回不同，因此对全部五个点扫描搜索宽度，取召回最接近的实测点：

| 数据点 | 对照 | Recall@10 | QPS | 相对吞吐 |
|---|---|---|---:|---:|---:|
| sift25k | PSG ef32 / LES ef16 | 0.9920 / 0.9913 | 84,131 / 51,123 | 1.65× |
| sift50k | PSG ef32 / LES ef16 | 0.9878 / 0.9871 | 69,750 / 39,037 | 1.79× |
| sift100k | PSG ef32 / LES ef16 | 0.9801 / 0.9802 | 51,904 / 18,142 | 2.86× |
| sift100k | PSG ef48 / LES ef24 | 0.9912 / 0.9925 | 43,096 / 13,658 | 3.16× |
| mnist25k | PSG ef32 / LES ef16 | 0.9953 / 0.9959 | 39,028 / 10,865 | 3.59× |
| fashion-mnist25k | PSG ef21 / LES ef12 | 0.9953 / 0.9953 | 48,790 / 17,811 | 2.74× |

按固定召回门槛取各方法最快的实测点（不插值）：

| 数据点 | 门槛 | PSG | LES | BF-HNSW | PFS |
|---|---|---:|---:|---:|---:|
| sift25k | r95 | 113,003 (ef21) | 61,767 (ef12) | 2,355 (ef384) | 16,054 |
| sift25k | r98 | 84,131 (ef32) | 61,767 (ef12) | 2,162 (ef512) | 16,054 |
| sift50k | r95 | 95,347 (ef21) | 47,574 (ef12) | 2,191 (ef384) | 7,829 |
| sift50k | r98 | 69,750 (ef32) | 39,037 (ef16) | 968 (ef768) | 7,829 |
| sift100k | r95 | 74,304 (ef21) | 25,179 (ef12) | 1,654 (ef384) | 2,222 |
| sift100k | r98 | 51,904 (ef32) | 18,142 (ef16) | 602 (ef1024) | 2,222 |
| mnist25k | r95 | 45,944 (ef21) | 16,559 (ef12) | 1,154 (ef384) | 2,343 |
| mnist25k | r98 | 45,944 (ef21) | 16,559 (ef12) | 440 (ef1024) | 2,343 |
| fashion-mnist25k | r95 | 48,790 (ef21) | 17,811 (ef12) | 1,206 (ef512) | 2,002 |
| fashion-mnist25k | r98 | 48,790 (ef21) | 17,811 (ef12) | 584 (ef1024) | 2,002 |

汇总文件：`data/five_l2_summary_20261020T104139Z-68414a0f.csv`（由 `src/analyze_five_l2.py` 生成）。r95/r98 取“实测点中满足门槛且 QPS 最大者”，不是在两个点之间插值。

结论：**在当前实现与参数下，LES 用更小的索引换来了更低的吞吐**。sift100k 上 LES 索引 124.1 MB、PSG 199.8 MB（LES 为 0.62×），但同召回时 PSG 快 2.9–3.2×；MNIST 上差距扩大到 3.6×。LES 的低 ef 侧（ef8）召回只有 0.82–0.83，说明其并图在窄 beam 下退化更快。

## 5. 度数裁剪诊断（sift100k）

LES 的共享并图把逐作用域度数从约 19 提到约 37，因此测试两类度数上限是否是吞吐差距的来源（`les_build --max-degree` / `--max-bucket-degree`，仅诊断，默认不启用）：

| 变体 | 保留条目 | 索引 (MB) | ef | Recall@10 | QPS | 填满 k |
|---|---:|---:|---:|---:|---:|---:|
| 无裁剪 | 13,403,746 | 124.1 | 16 | 0.9802 | 18,142 | 1000 |
| 全局 top-24 | 2,393,555 | 44.9 | 32 | 0.4027 | 29,670 | **710** |
| 每桶 top-8 | 6,907,289 | 78.7 | 32 | 0.9775 | 20,436 | 1000 |
| 每桶 top-16 | 10,602,821 | 104.5 | 24 | 0.9884 | 15,955 | 1000 |
| 每桶 top-24 | 12,320,401 | 116.5 | 16 | 0.9787 | 27,392 | 1000 |

两点发现：① **全局按距离裁剪会破坏作用域可达性**（Recall 0.33–0.40，只有 710/1000 查询填满），因为被删边在祖先作用域中同样承担连接作用；LES 的冗余不是纯浪费。② **按 (source, activation) 桶裁剪**可保留召回（0.978–0.997），在 r95 上把吞吐从 25,179 提到 29,973（+19%），索引从 124.1 MB 降到 104.5 MB，但仍只有 PSG 的 0.40×（29,973 vs 74,304）。因此 LES 的差距不只是度数，而是“跨作用域并图”本身的访问局部性与路径冗余。原始数据：`data/five_l2_les_degree_20261020T104139Z-68414a0f.csv`。

## 6. 空间构成：PSG 的体积主要是重复向量

在 sift100k 上把 PSG 的图度数降到 M=6/12/16，其他不变，测索引字节与召回：

| PSG 变体 | 索引 (MB) | 最高 Recall@10 | 对应 QPS |
|---|---:|---:|---:|
| M=24（主表） | 199.8 | 0.9970 (ef32) | 14,471 |
| M=16 | 182.0 | 0.9673 (ef32) | 52,740 |
| M=12 | 173.2 | 0.9488 (ef32) | 42,570 |
| M=6 | 160.5 | 0.8212 (ef32) | 70,355 |

索引字节随 M 近似线性：M=12→16 增加 8.8 MB，斜率约 2.2 MB/M，截距约 146.8 MB。也就是说 **PSG 的 160–200 MB 中约 147 MB 是逐作用域重复存放的 u8 向量**，图只占十几 MB。M=6 即使 ef=32 也只有 0.821 召回，说明“重复向量”版本无法在 160 MB 以下达到 r95。

**共享向量 PSG（已实现并实测）。** 在 `baseline.cpp` 增加 `GRAPH_SHARE_VECTORS=1`：构建时把 u8 向量写一份 `base_u8.bin`，`c_*.bin` 只存链接；查询从共享向量取距离。默认关闭，旧索引不受影响。sift100k 结果：

| 变体 | 索引 (MB) | ef | Recall@10 | QPS |
|---|---:|---:|---:|---:|
| 共享 M=12 | 53.6 | 32 | 0.9487 | 49,094 |
| 共享 M=12 | 53.6 | 48 | 0.9717 | 40,321 |
| 共享 M=16 | 62.3 | 24 | 0.9494 | 53,256 |
| 共享 M=16 | 62.3 | 48 | 0.9840 | 36,180 |
| 共享 M=16 | 62.3 | 64 | 0.9904 | 30,031 |

共享 M=16 与重复向量 M=16 在 ef=24/32 的召回完全相同（0.9494/0.9673），验证了布局改动不改变搜索语义。对照 LES（124.1 MB，r95 25,179，r98 18,142）：共享 M=16 只用 **0.50× 空间**，r95 快 **2.12×**、r98 快 **1.99×**；共享 M=12 用 **0.43× 空间** 也在 r95 上快 **1.60×**。因此 LES 的“更省空间”优势主要来自 PSG 重复向量。**该段曾据此判为“LES 被严格支配”；§9 的严格交错与同空间（约 53.5 MB）对照修正了这一结论：LES 不是被严格支配，而是在中等召回/r95 门槛不落后、r98 落后。**

**同一张图的等价性验证。** 用 `src/shrink_psg_vectors.py` 把重复向量 M=16 索引离线转换（保留 links、去掉块内向量、写一份 `base_u8.bin`）：转换结果 62.34 MB，与重新构建的共享索引一致；ef=24/32 的 1000 条查询输出**逐位相同**（`np.array_equal`），QPS 60,538/48,833。这排除了“布局改动改变搜索语义”的可能，也说明共享构建与转换之间的 $10^{-4}$ 召回差异来自构建期浮点次序，而不是查询逻辑。

原始数据：`data/five_l2_psg_space_20261020T104139Z-68414a0f.csv`、`data/five_l2_psg_shared_20261020T104139Z-68414a0f.csv`；复现命令见 §10。

## 7. LES-v2：删除死权重、对齐边预算

针对共享 PSG 的对照，做了三项改动（`les_build --drop-aux`、底图 M=16、`--max-bucket-degree 16`）：

1. **底图从 M=24 降到 16**，与共享 PSG M=16 的边预算对齐；
2. **删除 `dist16`（u16）与 `dflag`（u8）**：三角不等式剪枝实测 `PRUNED 0/618,585 = 0.00%`，`dflag` 只服务诊断模式，二者合计约占 40 MB；
3. **按 (source, activation) 桶限制到 16 条边**，避免并集把单桶度数抬高。

结果（sift100k）：

| 变体 | 索引 (MB) | r95（实测点） | r98（实测点） |
|---|---:|---:|---:|
| LES 原版（M24，含 aux） | 124.1 | 25,179 (ef12, 0.9642) | 18,142 (ef16, 0.9802) |
| LES-v2 无桶限（M16，drop aux） | 73.6 | **35,084** (ef12, 0.9506) | 22,103 (ef24, 0.9880) |
| LES-v2 桶限 16 | 69.0 | 29,571 (ef16, 0.9691) | **23,504** (ef24, 0.9861) |
| 共享向量 PSG M16 | 62.3 | 53,256 | 36,180 |

相对原版：r95 提升 **+39%**、r98 提升 **+30%**、索引缩小 **41%**。但仍落后共享 PSG：r95 为 $0.66\times$、r98 为 $0.65\times$，空间为 $1.11$–$1.18\times$。差距从 $2.1\times$ 缩到约 $1.5\times$，方向正确但不足以翻盘。

**剩余瓶颈（有依据的部分）**：LES 的邻居 id 是全局 u32，PSG 用逐作用域局部 u16；LES 每次扩展还要在 run 数组上做两次二分。相同边数下，LES 的邻接带宽约为 PSG 的 $2\times$，并多两次分支。要进一步逼近，需要按激活桶保存局部 id 映射（等价于在 LES 里重建 PSG 的局部编号），这会引入每桶映射表并削弱“单份全局邻接”的简洁性。

**回归验证**：旧索引（含 `dist16`/`dflag`）在新查询程序下仍返回 0.9729（与改动前一致）；LES-v2 全部 5,000 个结果无违规、全部填满 $k$。

原始数据：`data/five_l2_les_v2_20261020T104139Z-68414a0f.csv`。

## 8. 全部稠密语料（11 个，n=25k）

`datasets/` 中可作稠密图的语料共 11 个：SIFT/MNIST/Fashion-MNIST（原生平方 L2 且值为 u8）与 glove25/50/100/200、coco-i2i、coco-t2i、nytimes、lastfm（原生 angular/dot，含负值）。后 8 个统一 min–max 量化到 $[0,255]$ 后用平方 L2 评测，**不是原生度量**；kosarak/movielens 是 1-D 稀疏 jaccard，排除。每语料 $n=25$k、1000 条 Subtree、PSG 与 LES 都用 M=16；PSG 与 LES 的 ef 分别扫到 512/192。

| 语料 | PSG r95 (ef) | LES r95 (ef) | LES/PSG | 空间比 |
|---|---:|---:|---:|---:|
| sift25k | 113,283 (24) | 78,404 (12) | 0.69 | 1.16 |
| mnist25k | 40,526 (16) | 16,716 (12) | 0.41 | 1.05 |
| fashion-mnist25k | 62,935 (16) | 22,798 (12) | 0.36 | 0.99 |
| glove25† | 113,131 (24) | 80,480 (12) | 0.71 | 1.16 |
| glove50† | 58,390 (48) | 45,383 (24) | 0.78 | 1.10 |
| glove100† | 25,400 (128) | **32,783 (32)** | **1.29** | **0.94** |
| glove200† | 29,000 (128) | **55,058 (12)** | **1.90** | **0.77** |
| coco-i2i† | 55,777 (16) | 26,148 (12) | 0.47 | 1.12 |
| coco-t2i† | 13,642 (128) | 7,926 (48) | 0.58 | 1.12 |
| nytimes† | 0.891 @ef512 | **10,306 (64)** | 达标 | 1.10 |
| lastfm† | 164,721 (24) | 128,080 (12) | 0.78 | 0.98 |

结论：**不是所有语料都差**。LES-v2 在 glove100/glove200 上同召回更快且索引更小，nytimes 上 PSG 即使 ef=512 也只到 0.891 召回而 LES 达到 r95；lastfm 在 r98 为 $1.17\times$。其余 8 个在 r95 落后（$0.36$–$0.78\times$），**所有原生度量语料全败**。反超只出现在量化后的 angular/dot 语料，更像“高维、量化几何下并图密度有利”的局部现象，需按维数/选择率分层验证；固定 M=16 也未按语料调参。原始数据：`data/all_datasets_20261020.csv`、`data/all_datasets_ext_20261020.csv`、`data/all_datasets_20261020_summary.csv`；图 `figures/fig_all_datasets.png`。

## 9. LES-v3：文献启发的新布局与自适应 beam

**文献线索**（alphaXiv 检索，2026）：JAG 用向量+属性联合度量构造按选择率分层的导航边（arXiv:2602.10258）；Fiber-Navigable Search 把过滤子图视为 fiber，用 anchor atlas 在停滞处重入可行区域（arXiv:2604.00102）；Uemura 等给边打属性标签、区分始终可走的 ⊥ 边与条件边（arXiv:2607.24173）；BOA/PathSeer 按查询自适应 beam（arXiv:2609.16175）。可直接落地的是**边标签压缩**与**自适应 beam**；fiber anchor 与 JAG 联合构图未实现。

**E0：执行优化。** `les_query` 复用候选堆/结果堆（`vector + push_heap/pop_heap`，保留 capacity），level-0 入口复用 upper descent 的距离；比较器与停止条件不变。u32/varint、固定/自适应下输出逐位一致；单次运行 fixed ef12 29.1→23.5 ms（−19.5%），adaptive ef24 36.2→35.6 ms。

**改动一：per-run delta-varint 邻接。** activation run 内目标 id 升序，按 run 做 delta + varint，查询顺序解码。`les_build --varint`：

- 索引 73.6 → **53.3 MB**（共享 PSG 为 62.3 MB）；
- 同一 ef 下输出与 u32 布局**逐位相同**（5,000 个 id 全等）；对全部 1,010,797 个 run 解码并逐条比对，无差异。

**改动二：按作用域基数自适应 beam。** `les_build` 写 `counts.bin`（每节点可行对象数），`les_query --adaptive` 选 $ef_q$：$<500$ → $ef/2$，$<10000$ → $2ef/3$，否则 $ef$。sift100k 固定 ef24：22,646 QPS / 0.9880；自适应 ef24：**27,666 QPS / 0.9807**（+22%，仍满足 r98）。

**严格配对测量。** 两引擎均 `original` 顺序；PSG 的 `scope` 分组在同一图上快约 13%（20.9→18.2 ms），使用 original 对 PSG 保守。7 个交错 block、单线程、每 block 每配置一次，中位数（`data/five_l2_strict_blocks_20261020.csv`）：

| 方法 | 索引 | r95：Recall / QPS | r98：Recall / QPS |
|---|---:|---:|---:|
| LES-v3（E0） | 53.3 MB | 0.9506 / **40,687** | 0.9807 / 25,589 |
| 共享 PSG M12 | 53.6 MB | 0.9717 / 34,506 | 0.9813 / **30,289** |
| 共享 PSG M16 | 62.3 MB | 0.9673 / 48,987 | 0.9840 / 37,021 |
| 共享 PSG M16 + adaptive | 62.3 MB | 0.9559 / 51,957 | 0.9876 / 32,800 |

同空间约 53.5 MB：LES 在 r95 门槛快 $1.18\times$；在约 0.963 召回（LES ef14 0.9625/34,699 vs PSG-M12 ef40 0.9638/32,707）快 $1.06\times$；r98 慢 $1.18\times$。PSG-M16 多 17% 空间，r95/r98 快 $1.20\times$/$1.45\times$。**LES 不是被严格支配，而是更小空间下的中等召回工作点。**

**S1 静态空间分析（未实现查询）。** 对每个 $(u,\text{local source})$ 的有序全局邻接做 exact interning：$N_{\rm ref}=1,034,631$，唯一列表 1,019,476（唯一率 **0.985**），平均长度 12.8。字节：refs 4.14 + offsets 8.16 + targets 52.11 + gid 4.14 + upper 1.61 + base 12.80 = **82.99 MB**，高于共享 PSG 62.34 MB 与 LES-v3 53.27 MB。按门槛**不实现**；逐作用域邻接几乎不可跨 scope 复用，S1 路线结束。

**负结果**：① 邻居向量预取使固定 ef24 从 45.2 ms 退化到 56.3 ms，已移除；② 对 $<1000$ 可行集的查询改走精确扫描无收益（31.0 ms vs 30.4 ms）。原始数据：`data/five_l2_les_v3_20261020T104139Z-68414a0f.csv`、`data/five_l2_strict_summary_20261020.csv`。

## 10. Directory-Aware 目录层的端到端位置
TRIEHI+HNSW 与 BF-HNSW **共享同一份序列化图和同一 ef**，因此 Recall@10 逐位相同（0.9730/0.9654/0.9534/0.9524/0.9442），差异只剩在线候选解析：QPS 变化 −3.7%～+8.6%（每查询时间 −8.0%～+3.8%），落在两次重复的运行噪声内。目录构建为一次性 3.1–13.5 ms，inclusive bitmap 占 4.27–17.07 MB。

这说明：**在这些选择率下，目录候选解析不是瓶颈**；Directory-Aware 的目录层与 LES 的边共享处在不同层，不能互相替代。TRIEHI 的优势在动态 MOVE/MERGE（本表未测 LES 更新），而 LES 的增量必须体现在 ANN 边的存储与访问上，而不是候选位图。注意这只验证了静态 Subtree 单谓词；原论文的 non-recursive 需要改为直接 posting 才能覆盖多标签（见 `directory-aware-reproduction.md` §7），本表未测 Exact。

## 11. 局限

- 五个补充评估点只有三个独立语料；§8 的 11 个稠密语料中 8 个是量化代理（非原生度量），且统一用 M=16、n=25k，不能代表其原生任务。
- **未在 PathSeer 论文的六个真实过滤数据集（SIFT1M/HM/LAION/Paper/Arxiv/Amazon）上运行**，本文数字与文献 SOTA 表不可直接比较；PathSeer 只用了固定 $M_{exp}$，未做虚拟开销边界分析。
- ACORN 未调 $\gamma/M_\beta$ 且只做单标签 metadata；Filtered-DiskANN / iRangeGraph / LSSG 未接入。
- 未做同内存预算、同构建资源或训练集调参；PSG 的 ef=21 与 LES 的 ef=12/16/24 是测试集扫描后选点。
- 只测 Subtree；Exact 仍只有 PFS 精确扫描基线。
- TRIEHI 是静态目录解析器，不含路径字符串、DSM、OpenViking 集成；PathSeer 是固定 M_exp 配置，未做虚拟开销边界分析。
- 查询为单线程、2 次重复取最好；没有置信区间，±9% 的差异不能解释为方法优劣。
- 构建时间不能直接横比：PSG 与 LES 是单线程 hnswlib，FAISS 为 OMP=1 的串行构图但实现不同；LES 的完整构建必须计为 PSG 底图 + 归并（表中 `build_seconds` 已相加）。

## 12. 补充实验复现命令

```bash
PY=production/nested-attr-vector-search/work/venv/bin/python
BIN=production/nested-attr-vector-search/work/bin
RUN=20261020T104139Z-68414a0f
F=production/nested-attr-vector-search/work/five_l2/fixtures/sift100k
P=production/nested-attr-vector-search/work/five_l2/runs/$RUN/sift100k

# 同召回扫描（五个点）
$PY production/nested-attr-vector-search/src/run_five_l2_sweep.py --run $RUN \
  --datasets sift25k,sift50k,sift100k,mnist25k,fashion-mnist25k \
  --psg-ef 32,48,64 --les-ef 8,12,16,24,48 --faiss-ef 512,768,1024
$PY production/nested-attr-vector-search/src/analyze_five_l2.py --run $RUN

# 度数裁剪诊断
$BIN/les_build --fixtures $F --index-dir $P/psg --out /tmp/les_bcap16 --max-bucket-degree 16
$BIN/les_query --out-dir /tmp/les_bcap16 --queries \
  production/nested-attr-vector-search/work/five_l2/pathseer/sift100k/query/query.bin \
  --workload $F/workload_sub.txt --mode subtree --k 10 --ef 16 --repeats 2 \
  --out /tmp/ids.bin --timings /tmp/t.bin

# 共享向量 PSG（与重复向量 M 相同，召回应一致）
GRAPH_SHARE_VECTORS=1 GRAPH_M=16 GRAPH_EFC=200 OMP_NUM_THREADS=1 \
  $BIN/candidate build --base $F/base.f32 --tree $F/tree.txt --labels $F/labels.bin \
  --meta $F/meta.txt --index-dir /tmp/psg_shared16
GRAPH_SHARE_VECTORS=1 GRAPH_EF=48 OMP_NUM_THREADS=1 \
  $BIN/candidate query --base $F/base.f32 --tree $F/tree.txt --meta $F/meta.txt \
  --index-dir /tmp/psg_shared16 --queries $F/test.f32 --workload $F/workload_sub.txt \
  --mode subtree --out /tmp/ids.bin --timings /tmp/t.bin --repeats 2
```

LES-v2（底图 M=16、删除辅助数组、可选桶限）：

```bash
# 底图：PSG M=16（重复向量版本即可，les_build 需要块内向量）
GRAPH_M=16 GRAPH_EFC=200 OMP_NUM_THREADS=1 $BIN/candidate build \
  --base $F/base.f32 --tree $F/tree.txt --labels $F/labels.bin --meta $F/meta.txt \
  --index-dir /tmp/psg_m16
# LES-v2
$BIN/les_build --fixtures $F --index-dir /tmp/psg_m16 --out /tmp/les_v2 --drop-aux
$BIN/les_build --fixtures $F --index-dir /tmp/psg_m16 --out /tmp/les_v2_cap16 \
  --drop-aux --max-bucket-degree 16
$BIN/les_query --out-dir /tmp/les_v2 --queries \
  production/nested-attr-vector-search/work/five_l2/pathseer/sift100k/query/query.bin \
  --workload $F/workload_sub.txt --mode subtree --k 10 --ef 12 --repeats 2 \
  --out /tmp/ids.bin --timings /tmp/t.bin
```

全部稠密语料：

```bash
$PY production/nested-attr-vector-search/src/run_all_datasets.py \
  --out-csv researches/nested-attr-vector-search/07-laminar-edge-sharing/2026-10-20-exps/data/all_datasets_20261020.csv
$PY production/nested-attr-vector-search/src/extend_all_datasets.py
$PY production/nested-attr-vector-search/src/analyze_all_datasets.py \
  --csv .../data/all_datasets_20261020.csv .../data/all_datasets_ext_20261020.csv
$PY production/nested-attr-vector-search/src/plot_all_datasets.py
```

LES-v3（varint + 自适应 beam）：

```bash
$BIN/les_build --fixtures $F --index-dir /tmp/psg_m16 --out /tmp/les_var \
  --drop-aux --varint
$BIN/les_query --out-dir /tmp/les_var --queries \
  production/nested-attr-vector-search/work/five_l2/pathseer/sift100k/query/query.bin \
  --workload $F/workload_sub.txt --mode subtree --k 10 --ef 24 --adaptive --repeats 2 \
  --out /tmp/ids.bin --timings /tmp/t.bin
# 共享 PSG 的自适应 beam
GRAPH_ADAPTIVE=1 GRAPH_SHARE_VECTORS=1 GRAPH_EF=32 $BIN/candidate query ...
```
