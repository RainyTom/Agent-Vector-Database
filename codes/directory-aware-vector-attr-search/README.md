# Directory-Aware Vector + Attribute Search — 论文复现

复现对象：**Directory-Aware Query and Maintenance in Vector Databases**
(arXiv:2606.16903v1, Wang et al.)。本地全文与核查记录见
[paper.report.md](../../papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md)。

与论文的差异（复现声明）：

| 项目 | 论文 | 本复现 |
|---|---|---|
| 数据集 | WIKI-Dir 1.94M×1024d / ARXIV-Dir 2.76M×1024d | 已完成完整 WIKI 1,941,679×1024d 两类基线；历史 SIFT/子采样结果保留 |
| 层级属性 | 真实 Wikipedia 类别树 / arXiv subject+temporal | 初始合成模拟（见下）；另有真实目录工作负载 |
| ANN 执行器 | Viking PG + IVF | 全局 FAISS HNSW M16（PG 近似替代，非同一实现）+ IndexIVFFlat nlist=1024；另有每节点局部 ANN 基线 |
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

## 两类基线与维护语义

**A：全局 ANN + scope ID 过滤。** 一个 HNSW 或 IVF 索引覆盖全体向量，三种目录表示（`src/repro.cpp`）分别解析同一 scope 的候选 ID 集，再交给全局 ANN：

- **PE-Online**：目录身份是完整路径字符串，仅存直接父路径的 Local posting；recursive 枚举后代路径键并 union，non-recursive 直接查 Local。MOVE 必须枚举移动子树的键并重映射，不能视作原生树的一次重挂。
- **PE-Offline**：扁平路径键上物化祖先 Inclusive posting；recursive 一次查找，non-recursive = Inc(v) − ∪ Inc(child)。MOVE 同样重映射子树键，并维护新旧祖先的成员集合。
- **TrieHI**：Trie 节点 + Inclusive 集合，查询语义同 PE-Offline；MOVE 重挂子树而无需重写所有后代路径键，但仍维护新旧祖先聚合集。

`IDSelector` / `sel` 限制结果资格；这不等于属性谓词已下推到图遍历，也不能据此断言不合格节点必被跳过、不再扩展。未核对具体版本源码前不作该机制判断。

**B：节点树 + 每节点局部 ANN。** 使用 `--node-algorithm hnsw`：每个非空节点建局部索引，递归查询枚举候选节点，各搜 top-k 后合并；非递归只搜锚节点。该基线的 `--node-hnsw-threshold 0` 仍表示全部非空节点使用HNSW，大阈值/混合Flat仅作对照。`all3`与`dsq_scan`默认保持该基线，不因新增候选而替换旧复现实验。N<k、截断节点和局部HNSW自身均可能造成近似损失。

DSM 语义：MOVE = 子树重挂；MERGE = u 的直接绑定条目迁移到 w、u 的子目录作为节点单元转挂到 w、u 删除（论文 §V-C “transferred as node units”）。PE-* 额外承担子树路径键重映射；PE-Offline 与 TrieHI 还维护新旧祖先聚合集。参照[论文相应说明（第 5 页）](https://www.alphaxiv.org/abs/2606.16903?page=5)。

成本须区分结构与集合维护：$O(m_u)$ 表示移动子树键的重映射次数（不含字符串长度成本）；$O(t)$ 仅表示祖先集合操作次数，不含位图规模及差/并运算成本。当前公共 `Tree::relink` 还遍历后代更新 depth，不能把 TrieHI 或 NodeANN 整个 MOVE 实现称为 $O(1)$。NodeANN 的局部索引随节点保留、无需因重挂重建，不等于整次 MOVE 无后代成本。公共 catalog 的历史 DSM 计时排除项沿用原实验记录，不能等同于实现没有该开销。

完整 WIKI 输入为 `datasets/dbpedia_dir_2m_corpus_vectors.fvecs`（7,960,883,900 bytes，1,941,679×1024）。本轮已修复 fvecs 转换的 int 位模式数值转换错误，并确认旧 `data/real/test.f32` 受影响；未核查的历史实验不推断污染范围。NODE 查询已移除额外全局 scope 位图解析；旧457行约束改为456条有效查询，运行器拒绝越界 qidx。新结果与旧ANN质量/延迟不可直接比较，旧目录/DSM记录不因向量转换自动失效。

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

完整 WIKI 两类基线（**仓库根目录**运行；默认局部全 HNSW，Flat 仅对照）：

```bash
bash codes/directory-aware-vector-attr-search/build.sh
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/2026-10-27-exps/experiments/run_wiki_full.py
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/2026-10-27-exps/experiments/summarize_wiki_full.py
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/tests/test_fvecs_to_f32.py
```

输入准备、固定约束、GT/empty-scope口径和内存限制见[复现协议](../../researches/nested-attr-vector-search/2026-10-27-exps/experiments/reproduction.md)，数字见[全量报告](../../researches/nested-attr-vector-search/2026-10-28-exps/report.md)。原CSV的 `recall10` 仍为历史 `hit/10`；标准macro/global recall由新增计数列汇总。NODE `bitmap_us=0`，局部搜索/合并计入独立 `search_us`。

历史选择率诊断给出过 ef 随选择率缩放的经验拟合，不是普适参数保证。完整 WIKI 本轮使用固定 ef=2048、nprobe=256，并单列 nprobe=1024 精确对照；极小 scope 下全局 ANN 召回仍低，不能称该默认值已覆盖 workload 或达到同召回最优。

## 实验报告(与代码分离)

报告、数据、图统一放在
[researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-triehi-reproduction](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-triehi-reproduction/2026-10-26-triehi-repro-results.md):
`2026-10-26-triehi-repro-results.md`(论文对比与偏离声明)、`data/`(CSV 原始数据)、
`figures/`(PNG+SVG 图)、`2026-10-26-triehi-repro-protocol.md`(复现命令)。

自检：每个 DSM op 后可设 `DSM_AUDIT_EACH=1` 逐 op 重算 Inclusive 集合
audit;结构不变量(parent/children/path-key 一致）默认每 op 校验。

## 新索引研究的固定 autoresearch 基准

仓库根目录运行 `bash autoresearch.sh`：完整WIKI、独立冻结NumPy GT、局部全HNSW起点，串行3次重复；输出主指标 `METRIC recursive_e2e_ms=...` 与召回/构建/RSS等副指标。每次重复均须通过质量及资源门槛，否则非零退出。`result_ids` 为新增per-query列，输出与独立质量审计均在查询计时外。

[固定协议及新颖性边界](../../researches/nested-attr-vector-search/2026-10-28-exps/experiments/autoresearch-harness.md)；[已观测baseline](../../researches/nested-attr-vector-search/2026-10-28-exps/data/autoresearch_harness_baseline.json)。已完成12轮静态DSQ迭代，最佳保留候选及消融见[报告§9](../../researches/nested-attr-vector-search/2026-10-28-exps/report.md#9-autoresearch-静态dsq迭代)，不是已通过新颖性认证的论文方案。

### Scope-run 候选索引

单独`--executor node`的DSQ默认采用新候选；显式配置为`--node-algorithm scope-run --scope-mode 4 --scope-local-order geometry`。静态single-binding，DFS区间及每节点内部符号编组，tile8 AoSoA与global top-k cutoff反馈；每个绑定仅一个向量副本。

- `--scope-mode 0`：row布局穷尽L2扫描；mode1为四路交错对照，mode2为逐点partial-distance对照。
- mode3：转置tile8穷尽扫描；mode4：每64维进行tile级共享截止反馈。mode4的浮点容差不是形式化精确性保证，本轮独立GT召回为1。
- `--scope-local-order original`：关闭节点内部几何编组，保持顺序payload物化，用于关键消融。
- `--node-algorithm hnsw`：保留原局部HNSW/Flat路径；全局HNSW/IVF与三种目录表示未移除。
- 不支持多绑定/DAG及MOVE/MERGE；拒绝重复global ID。物理DFS布局不继承TrieHI的动态重挂优势，不能将静态查询收益写成维护收益。
