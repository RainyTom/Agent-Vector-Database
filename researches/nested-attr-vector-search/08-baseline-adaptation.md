# 过滤 ANN 论文到层级 Exact/Subtree 场景的基线改造（2026-10-21）

目的：从已收集的过滤 ANN 文献中，挑选有开源代码、能改造为本文 Exact/Subtree 查询的强基线，给出改造方式、可行性与优先级。本文不把“换数据跑一遍”当作复现；每项都注明代码入口与尚未核验的部分。

## 1. 语义映射：两类查询都是一个可行位图

给定直接标签 $L_i$ 与树 $T$，两种模式的作用域分别是

$$
\mathcal B_u=\{i\mid u\in L_i\},\qquad
\mathcal D_u=\{i\mid L_i\cap\operatorname{Desc}^{+}(u)\ne\varnothing\}.
$$

对过滤 ANN 系统而言，最干净的改造接口是**每查询可行位图**：Exact$(u)$ 置位 $\mathcal B_u$，Subtree$(u)$ 置位 $\mathcal D_u$，antichain 取并。这样不需要改它们的过滤谓词语义，只需在加载器中构造位图；代价比较也在同一“已给出位图”的口径下进行（不计位图构造时间，或单独报告）。

若系统只接受标签集合而非位图，则可直接给：

- 直接标签编码：Exact 查询标签集 $\{u\}$，Subtree 查询标签集 $\operatorname{Desc}^{+}(u)$；
- 祖先闭包编码：Subtree 查询标签集 $\{u\}$，Exact 需要“命中 $u$ 再排除后代”，多数标签过滤系统不直接支持差集。

因此**优先选择接受任意谓词/位图的系统**；只支持标签匹配的系统用直接标签编码，并单独报告后代集合展开的成本。

## 2. 候选论文、代码与改造空间

代码状态核验日期 2026-10-21；GitHub 入口来自 `papers/papers.jsonl` 或论文首页，未逐一编译。

| 论文 | 代码 | 改造为 Exact/Subtree | 可行性/优先级 |
|---|---|---|---|
| **ACORN**（SIGMOD 2024） | [stanford-futuredata/ACORN](https://github.com/stanford-futuredata/ACORN)；PathSeer 的 FAISS fork 也含 `IndexACORN` | 谓词无关的过滤图遍历，接受任意 predicate；用位图谓词即可同时服务两种模式 | **高**。ACORN 是 predicate-agnostic 的直接对照，代码可获取；需要构建 FAISS 或其独立实现 |
| **Filtered-DiskANN**（WWW 2023） | [microsoft/DiskANN](https://github.com/microsoft/DiskANN)，本地 `deps/DiskANN`（Rust workspace） | 标签过滤 Vamana（stale/fresh）；接受标签/谓词；两种模式用位图或直接标签 | **高（论文层面）**。本地代码可读，但当前环境无 `cargo`，Rust workspace 构建成本高；API 以 label filter 为主 |
| **Filtered-HNSW**（PathSeer 实现） | [lizhiyuell/PathSeer](https://github.com/lizhiyuell/PathSeer)：`faiss/faiss/IndexFilteredHNSW.*` | HNSW + 过滤器；位图谓词直接可用 | **高**。与 ACORN 共用 FAISS 构建与数据转换，是最容易落地的强基线 |
| **NaviX**（PathSeer 实现） | 同上：`IndexNaviX.*` | 邻居导航 + 过滤 | 中高。可与 ACORN/Filtered-HNSW 同一驱动 |
| **PathSeer**（SIGMOD 2026） | 同上：`IndexPathSeer.*`，含完整 benchmark | 自适应邻居处理；位图谓词 | 中高。作为最新对照，但参数与实现复杂 |
| **iRangeGraph**（SIGMOD 2025） | [YuexuanXu7/iRangeGraph](https://github.com/YuexuanXu7/iRangeGraph) | segment-tree 单元图按区间共享；子树在标签树先序上是区间，可映射为范围查询；Exact 需“直属集合”区间或专用结构 | **中高**。与 LES 最接近的共享机制；映射需要额外工作（标签树先序 vs 数据属性范围） |
| **ELI / LabelANN**（VLDB 2026） | [mingyu-hkustgz/LabelANN](https://github.com/mingyu-hkustgz/LabelANN) | 标签混合检索的索引选择；标签集合过滤 | 中。适合作为“按选择率选索引”的对照 |
| **RWalks**（SIGMOD 2025） | [AnasAito/rwalks-reproduce-v2](https://github.com/AnasAito/rwalks-reproduce-v2) | 属性随机游走扩散；任意属性过滤 | 中。需要适配标签谓词与位图 |
| **Graph Range Filters / DLH**（2026） | [TaoLbr1993/DLH](https://github.com/TaoLbr1993/DLH) | 图上的范围过滤；子树映射同 iRangeGraph | 中低（范围语义不是层状子树） |
| **LSSG**（2026） | 未找到公开代码 | 标签集分层；直接标签编码可用 | 低（无代码）；作为语义对照引用 |
| **Curator**（SIGMOD 2026） | 未找到公开代码 | 低选择率过滤的按需索引 | 低（无代码） |
| **Packing / TFANNS**（ICDE 2025） | 未找到公开代码 | 标签感知构图 | 低 |
| **Tag-Filtered ANN**（ICDE 2025） | 未找到公开代码 | 标签过滤 | 低 |
| **UNG** | 仅作者代码快照/海报（本地证据），无完整仓库 | 标签导航图 | 低（复现风险高） |

## 3. 推荐落地路径

### 3.1 数据转换（所有 FAISS 系基线共用）

PathSeer 的 benchmark 期望：

```
dataset/<name>/load/load.bin     # u32 n, u32 d, then n*d float32
dataset/<name>/query/query.bin   # u32 nq, u32 d, then nq*d float32
dataset/<name>/query/query_<class>.bv  # 每查询位图（nr_query * ceil(n/8) 字节）
dataset/<name>/golden/golden_<class>_<metric>_10.bin
```

我们需要写一个转换器，把 `fixtures_ccs/` 转成该布局：

- `base.f32`、`test.f32` 已满足向量布局；
- 对每个查询生成两种位图：`query_sub.bv`、`query_exact.bv`（按 antichain 并集）；
- golden 使用我们已有的 `gt_sub.bin` / `gt_exact.bin`（注意 PathSeer 的 golden 是 ID 列表，metric 需与其距离定义一致）；
- **关闭默认的向量归一化**：我们的距离是平方 L2，PathSeer 的 `load_ann_data` 默认归一化（cosine）；调用处需传 `normalized=false`，否则距离语义改变。

### 3.2 驱动

PathSeer 的 benchmark 已有 build/search 循环，但数据集与谓词名是硬编码的。最省力的做法是写一个独立的小驱动：

1. 读取 `load.bin` / `query.bin` / 位图；
2. 构建 `IndexACORN`、`IndexFilteredHNSW`、`IndexNaviX`（同一 FAISS flat 基座，参数对齐：M、efConstruction、efSearch、过滤位图）；
3. 逐查询调用 `search`，记录吞吐、召回、索引字节、构建时间；
4. 用同一组位图在 B0/B4/LES 上跑，保证查询集与可行集完全一致。

### 3.3 构建与验证

**FAISS fork 已在本机构建成功（2026-10-21）**：

```bash
cd .tmp/PathSeer/faiss
# CMake 4.x 需要兼容旧依赖的 policy；NaviX.cpp 缺少 <array> include，需在克隆中补一行
cmake -DFAISS_ENABLE_GPU=OFF -DFAISS_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF \
      -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-march=native" -B build
make -C build -j10 faiss          # -> build/faiss/libfaiss.so
```

构建修复只涉及克隆副本，不修改本研究仓库：CMake 兼容参数 + 在 `faiss/impl/NaviX.cpp` 顶部补 `#include <array>`。

**过滤接口已确认**：ACORN 的 `SearchParametersACORN` 与 FAISS HNSW 的 `SearchParameters` 都接受 `IDSelector*`；`IDSelectorBitmap(size_t n, const uint8_t*)` 在 `faiss/impl/IDSelector.h` 中。因此两种模式只需为每个查询提供一个可行位图，**无需改算法或标签编码**。`IndexACORNFlat(d, M, gamma, metadata, M_beta, METRIC_L2)` 的 `metadata` 是构图用的每向量整数标签；若只用位图谓词，可用占位 metadata（构造出的图不做过滤感知剪枝），这会在结果中标注为“filter-agnostic ACORN”而不是完整复现。

- Rust DiskANN 需要先安装 `cargo`；当前环境没有，不优先。
- 每个基线的召回必须用我们的 oracle 复核；PathSeer 的 golden 只作交叉检查。
- 过滤成本口径：位图构造时间单独报告；系统内部的谓词求值计入查询时间。

## 4. 当前已完成与未完成

**已完成（2026-10-21）**

- 生成 CCS 形状的层级工作负载（`prep_ccs.py`）：1365 节点（深度 5、4 叉、1024 叶），每对象 1–5 个直接标签（均值 2.1），25% 的对象主标签落在中间节点，叶类目按 Zipf(1.1) 偏斜并与 512 个向量中心相关；1000 条 Subtree 查询、1000 条 Exact 查询与精确 oracle。
- 统计（`stats_ccs.json`）：直接标注按深度为 0/4941/5628/5529/5737/187613；Subtree 作用域 p50/p95/max = 10334/49305/86699；Exact 作用域 p50/p95/max = 126/396/6973；空 Exact 作用域 1 个（合成数据的局限）。
- 基线测量（同机、单线程、ef=21、u8 精确距离）：
  - B4 逐作用域物化（仅 Subtree）：**74,706 qps，Recall@10 0.9641，p95 28.4 µs，索引 194 MB，构建 57 s**；
  - B0 Exact 精确扫描（直接标签倒排 + 距离）：**191,914 qps，Recall@10 1.0000，p95 11.3 µs**。
- 关键观察：在这个 workload 中 Exact 作用域很小（p50 126），精确扫描远快于任何图索引；**为 Exact 物化图没有收益**。这直接支持“小作用域回退扫描”的设计，也意味着 LES 的两段式直属桶必须先与扫描比较。

**未完成**

- ACORN / Filtered-HNSW / NaviX / PathSeer 的**驱动与测量**（FAISS 已构建；下一步写一个最小驱动，用 `IDSelectorBitmap` 传入我们的可行位图，在 1000 条 Subtree 查询上跑 ACORN 与 FAISS-HNSW）；
- iRangeGraph、ELI、RWalks 的适配；
- 位图构造时间、峰值内存与后代标签展开成本的独立计量；
- 真实 CCS/SciHTC 标签（当前为校准后的合成标签）。

## 5. 建议的下一步顺序

1. 写 `fixtures_ccs → PathSeer dataset` 转换器（位图 + golden），构建 FAISS，跑 Filtered-HNSW（最简单）与 ACORN；
2. 在同一批 1000+1000 查询上比较 B0 / B4 / ACORN / Filtered-HNSW；
3. 再引入 iRangeGraph（共享机制）与 PathSeer（最新方法）；
4. 只有在前述都在同一协议下跑通后，才评估 LES 的增量。

## 6. 复现

```bash
# CCS 形状工作负载
tools/.venv/bin/python production/nested-attr-vector-search/src/prep_ccs.py \
  --fixtures production/nested-attr-vector-search/work/fixtures \
  --out production/nested-attr-vector-search/work/fixtures_ccs

# Subtree（B4）
GRAPH_EF=21 production/nested-attr-vector-search/work/bin/candidate query \
  --mode subtree --base .../fixtures_ccs/base.f32 --tree .../fixtures_ccs/tree.txt \
  --meta .../fixtures_ccs/meta.txt --index-dir .../index_ccs \
  --queries .../fixtures_ccs/test.f32 --workload .../fixtures_ccs/workload_sub.txt \
  --out /tmp/ids.bin --timings /tmp/tim.f64 --repeats 3

# Exact（B0 扫描）
... --mode exact --labels .../fixtures_ccs/labels.bin \
  --workload .../fixtures_ccs/workload_exact.txt ...

# 评分
tools/.venv/bin/python production/nested-attr-vector-search/src/eval_ccs.py \
  --fixtures .../fixtures_ccs --workload .../workload_sub.txt \
  --gt .../gt_sub.bin --ids /tmp/ids.bin --timings /tmp/tim.f64 \
  --query-seconds <from QUERY_SECONDS> --mode subtree
```
