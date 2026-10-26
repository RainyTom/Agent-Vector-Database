# 数据集下载清单（迁移用）

更新日期：2026-10-26。原始数据均被 `.gitignore` 排除，不随仓库迁移；
本清单给出每个文件的官方下载地址、本地大小与格式坑。配套脚本：
`bash datasets/download_all.sh`（幂等，已存在的文件跳过，`curl -C -` 断点续传）。

## A. ann-benchmarks HDF5（11 个，URL 已验证 200，大小与本地一致）

统一地址模式：`http://ann-benchmarks.com/<文件名>`。HDF5 结构为
`train`/`test`/`neighbors`（整数标签数据集为 `distances`）。

| 本地文件 | 大小 | 格式坑（实验代码已处理，新代码勿再踩） |
|---|---:|---|
| `sift-128-euclidean.hdf5` | 501M | u8，原生平方 L2 |
| `mnist-784-euclidean.hdf5` | 217M | u8，原生平方 L2 |
| `fashion-mnist-784-euclidean.hdf5` | 217M | u8，原生平方 L2 |
| `glove-25-angular.hdf5` | 122M | **angular：用前先 L2 归一化** |
| `glove-50-angular.hdf5` | 236M | 同上 |
| `glove-100-angular.hdf5` | 463M | 同上 |
| `glove-200-angular.hdf5` | 919M | 同上 |
| `nytimes-256-angular.hdf5` | 301M | angular，同上 |
| `lastfm-64-dot.hdf5` | 136M | **实际是 65 维**（文件名 64 是错的），dot 度量 |
| `kosarak-jaccard.hdf5` | 33M | **扁平 int64 + size 数组的稀疏 jaccard**，非稠密向量 |
| `movielens10m-jaccard.hdf5` | 64M | 同 kosarak |

## B. coco-i2i / coco-t2i（出处未核实）

| 本地文件 | 大小 | 状态 |
|---|---:|---|
| `coco-i2i-512-angular.hdf5` | 136M | `ann-benchmarks.com` 404；出处**未核实** |
| `coco-t2i-512-angular.hdf5` | 136M | 同上 |

线索：命名（i2i=image-to-image，t2i=text-to-image，512 维）指向
MS COCO 的 CLIP ViT-B/32 embedding；cuVS/RAFT benchmark 套件有同名数据集。
两个恢复路径：(1) 在 cuVS benchmark 数据上游继续追；(2) 用 MS COCO +
CLIP 重新生成。LES/PSG 五语料实验用过它们（见
`researches/nested-attr-vector-search/07-laminar-edge-sharing/`），
但当前主线（Directory-Aware / 选择率研究）不依赖，可后置。

## C. Massive-STEPS 城市签到 CSV（STHS 项目）

论文：arXiv:2505.11239，HF collection：
`https://huggingface.co/collections/CRUISEResearchGroup/massive-steps-point-of-interest-check-in-dataset-682716f625d74c2569bc7a73`（Apache-2.0，HF 文件列表已验证）。

地址模式：`https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-<City>/resolve/main/<file>.csv`

| 本地文件 | 来源 |
|---|---|
| `ny_checkins.csv` | `Massive-STEPS-New-York/new_york_checkins.csv` |
| `melbourne_checkins.csv` | `Massive-STEPS-Melbourne/melbourne_checkins.csv` |
| `istanbul_checkins.csv` | `Massive-STEPS-Istanbul/istanbul_checkins.csv` |
| `jakarta_checkins.csv` | `Massive-STEPS-Jakarta/jakarta_checkins.csv` |
| `petaling_jaya_checkins.csv` | `Massive-STEPS-Petaling-Jaya/petaling_jaya_checkins.csv` |
| `sao_paulo_checkins.csv` | `Massive-STEPS-Sao-Paulo/sao_paulo_checkins.csv` |
| `moscow_checkins.csv` | `Massive-STEPS-Moscow/moscow_checkins.csv` |

每个 repo 另有 `_train/_validation/_test` 切分 CSV 与 parquet，本项目只用全量 CSV。
harness 用 6 城（不含 melbourne）。本地合计 383MB。

## D. WIKI-Dir / ARXIV-Dir（Directory-Aware 论文真实数据集，Google Drive）

发布方：`dir-vector-dataset`（TrieHI 论文 arXiv:2606.16903 作者）。
入口（embeddedfolderview）：
`https://drive.google.com/drive/folders/1T3z9IDUbIbq0Sm3A-uVGzUBp1Q7RCJgM`

用 `gdown --folder` 下载；无法列目录时用单文件 ID：

| 文件 | 大小 | Drive 文件 ID | 备注 |
|---|---:|---|---|
| wiki/query.jsonl | ~2.0GB | `1Yg-rr9xoiJimn_8Uu0Ca2-YcM-ZIptrb` | **发布方截断在 2^31 字节**，尾部不完整 |
| wiki/corpus.jsonl | ~1.9GB | `1VPVFTMHCKm0eNUn5aQZ2HgCqHc6NUnX_` | 同上截断 |
| wiki/groundtruth.txt | 28KB | `1D3ubDyoz9JOMPi6HO2ks32DcvhRP_M22` | |
| wiki/corpus_paths.jsonl | 22MB | `1YvHcDYiTFJBj9ZU0Mf5PMEZV65GHSqgG` | |
| wiki/query_vectors.fvecs | **1.87MB** | `14zi_Rf7Z5yMAczLXyLgNhyAm6PeRhjIl` | ⚠ **名字在撒谎：这是 456 条查询向量** |
| wiki/corpus_vectors.fvecs | **7.96GB** | `1PBmCKaUmEYc1BGU_TBcICRaIqbc3xRuQ` | ⚠ **名字在撒谎：这是 1.94M 条语料向量** |
| arxiv/arxiv_query_constraint.json | 4.1MB | `1uU1LQkE_8aO1dKCyiSQyIi3ZGF7MQUIr` | ⚠ **这是 1000 条查询向量（fvecs 内容）** |
| arxiv/arxiv_query_vectors.fvecs | 2.0MB | `1V08kABnk_Lyj-6qDj4a4XYlfgf9r8GWd` | 真实 category 查询（本项目未下载，可选） |
| arxiv/groundtruth.txt | 122KB | `1StfVT_F0vv6__ZS4bUaC8DYkIli2ZP7f` | |
| arxiv/groundtruth_category.txt | 57KB | `1KP6Nk3_3aWWLpvRAV5-vEZ_94g_pqyje` | category 查询的 GT |
| arxiv/arxiv_category_constraint.json | 29MB | `1VM07pjh7S-0D1splg4INcEHG9OLQOvT_` | 每文档 category 列表 |
| arxiv/corpus_metadata.jsonl | 221MB | `1fo-kMgI_bOcE2rB44mk9KrBDTDQFPc07` | |

子目录 folder ID：wiki `19mUedPFWJsfGj3Wnzeg5O9RNq8u_MX1K`，
arxiv `1ZiChH3FO4eYw0Iiq3I-xv3X0tS8bWImM`。

下载后跑 `codes/directory-aware-vector-attr-search/scripts/build_real_fixtures.py`
重建 `data/real_fixtures/{wiki,arxiv}400000`（见该仓库 `experiments/protocol.md` 阶段 2）。

## E. PathSeer filtered-ANN 数据集（HF）

`https://huggingface.co/datasets/lizhiyuell/filtered_ANNS_dataset` —
PathSeer 论文的 SIFT + 合成标签基准。本项目的选择率基线
`work/bin/faiss_filtered` 用自建 SIFT100k+CCS 标签，不依赖它；
如要与 PathSeer 论文数字对齐才需要下载。

## F. 论文引用、本项目未下载（将来可能需要）

| 数据集 | 地址 | 说明 |
|---|---|---|
| SIFT1M (texmex) | `ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz` | 注意：FTP 源 curl 易报 error 18，**重跑即续传** |
| GIST1M (texmex) | `ftp://ftp.irisa.fr/local/texmex/corpus/gist.tar.gz` | 同上 |
| Deep1B | `https://research.yandex.com/blog/benchmarks-for-billion-scale-similarity-search` | 入口页，非直链 |
| Massive-STEPS 其余 8 城 | 同 C 节 collection 链接 | Bandung/Beijing/Kuwait-City/Palembang/Shanghai/Sydney/Tangerang/Tokyo |

## G. 非数据但需重建的产物

| 产物 | 重建方式 |
|---|---|
| `production/semantic-mobility-search/work/`（~5GB） | `prep_potion.py` 重建（约 46s，峰值 4.7GB RAM） |
| `production/semantic-mobility-search/models/potion-base-8M`（31MB） | HF `minishlab/potion-base-8M` 的 model.safetensors + tokenizer.json |
| `deps/hnswlib` 等 | `deps/manifest.tsv` 记录的仓库/版本 |
| `production/nested-attr-vector-search/work/fixtures_ccs` | 由 A 节 `sift-128-euclidean.hdf5` 生成（见 `src/prep_ccs.py`） |

总下载量：A+B+C 约 3.8GB（不含 D 的 ~12GB Drive 文件）。
