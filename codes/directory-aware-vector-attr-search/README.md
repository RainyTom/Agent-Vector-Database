# Directory-Aware Vector + Attribute Search — 论文复现

复现对象：**Directory-Aware Query and Maintenance in Vector Databases**
(arXiv:2606.16903v1, Wang et al.)。本地全文与核查记录见
[paper.report.md](../../papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md)。

与论文的差异（复现声明）：

| 项目 | 论文 | 本复现 |
|---|---|---|
| 数据集 | WIKI-Dir 1.94M×1024d / ARXIV-Dir 2.76M×1024d | SIFT100k×128d(`datasets/sift-128-euclidean.hdf5`) |
| 层级属性 | 真实 Wikipedia 类别树 / arXiv subject+temporal | 合成模拟（见下） |
| ANN 执行器 | Viking PG + IVF | FAISS HNSM M16(≈PG) + IndexIVFFlat nlist=1024(≈IVF) |
| ID 集合 | Roaring bitmaps | CRoaring v4.3.1(与论文一致) |
| 硬件 | 48-core Xeon Silver 4310 | 本机 WSL2(单线程计时) |

合成层级属性(`scripts/gen_workload.py`,seed=20261026):

- **wiki-like**：深随机树，19,000 目录、最大深度 14、均深 13.4（论文 WIKI-Dir
  363k 目录、均深 11.95);90% 对象绑定在深度≥8 的目录，保证浅层锚点挂大子树
  （论文 Fig. 10 的关键结构性质）。
- **arxiv-like**：两个独立命名空间——subject 树 1+14+42+168（均深≈2.19,
  同论文 168 目录）;temporal /yyyy/mm 树 1+20+240（均深 1.70，论文 1.92)。
- **workload**:wiki 500 查询（锚点深度混合按论文 Fig. 10:1/2/4/9 层
  计数比 83/39/72/114),arxiv 1000 查询（subject/temporal 各半），每个查询
  带 recursive / non-recursive 目录约束（各 50%)。GT = 可行集暴力 top-10
  （同论文 ARXIV-Dir 协议）。
- **DSM**:wiki 命名空间上 1000 MOVE + 1000 MERGE（同论文数量）。

三种目录设计(`src/repro.cpp`):

- **PE-Online**：仅存 Local posting;recursive DSQ 查询时 DFS 枚举后代目录
  并 union;non-recursive 直接查 Local。
- **PE-Offline**：摄入时物化祖先归属（每目录 Inclusive 集合）;recursive 一次
  查找；non-recursive = Inc(v) − ∪ Inc(child)。
- **TrieHI**:Trie 节点 + Inclusive 集合（查询语义同 PE-Offline);DSM 用子树
  重链代替路径键重写。

DSM 语义：MOVE = 子树重挂；MERGE = u 的直接绑定条目迁移到 w、u 的子目录
作为节点单元转挂到 w、u 删除（论文 §V-C "transferred as node units")。
扩张式设计（PE-*)额外承担子树范围路径键重写；公共 catalog 不计入
（同论文 §V-A Implementation Details)。

## 运行

```bash
# 1. 生成数据(需要 nested-attr work venv 的 h5py)
production/nested-attr-vector-search/work/venv/bin/python \
  scripts/gen_workload.py --out data/sift100k

# 2. 构建(CRoaring 已预构建在 deps/croaring/build;
#    FAISS 使用仓库 deps/PathSeer/faiss 的构建产物)
./build.sh

# 3. 运行全部实验(HNSW efSearch=2048、IVF nprobe=256,可用 --ef/--nprobe 调)
#    --csv 指向研究报告数据目录(代码与报告分离,见下)
REPRO=../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-triehi-reproduction
LD_LIBRARY_PATH=../../deps/PathSeer/faiss/build/faiss \
  ./build/repro --data data/sift100k --csv $REPRO/data/results --mode all

# 4. 绘图(PNG+SVG 写入 $REPRO/figures)
cd ../..
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/scripts/plot_repro.py
```

DSQ 参数选择说明:HNSW 过滤搜索的 ef 必须随选择率缩放(本仓库
2026-10-26 选择率实验:ef≈23·s^-0.77),默认 ef=2048 覆盖 wiki workload
中位 scope(card≈54, s≈5e-4);IVF nprobe=256 同理。

## 实验报告(与代码分离)

报告、数据、图统一放在
[researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-triehi-reproduction](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-triehi-reproduction/2026-10-26-triehi-repro-results.md):
`2026-10-26-triehi-repro-results.md`(论文对比与偏离声明)、`data/`(CSV 原始数据)、
`figures/`(PNG+SVG 图)、`2026-10-26-triehi-repro-protocol.md`(复现命令)。

自检：每个 DSM op 后可设 `DSM_AUDIT_EACH=1` 逐 op 重算 Inclusive 集合
audit;结构不变量(parent/children/path-key 一致）默认每 op 校验。
