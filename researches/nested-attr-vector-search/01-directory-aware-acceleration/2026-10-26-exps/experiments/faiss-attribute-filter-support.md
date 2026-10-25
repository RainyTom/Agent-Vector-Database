# FAISS 是否支持属性过滤：接口、语义与证据

日期：2026-10-26。问题：FAISS 的向量索引是否支持属性过滤（例如 `category = 'x'` 或目录范围谓词）？本笔记只回答机制与接口，不把“ID 过滤”混同于“属性索引”。

## 1. 结论

**核心 FAISS 不支持属性过滤。** 它没有属性列存储、谓词解析、属性统计或 predicate pushdown；不能把一个属性条件直接交给索引执行。

**FAISS 支持 ID 级过滤钩子。** `SearchParameters::sel` 接受一个 `IDSelector`；调用方必须先把属性条件求值成可行 ID 集合（`IDSelectorBitmap`、`IDSelectorBatch`、`IDSelectorRange` 等），再随查询传入。对 HNSW / IVF / Flat 都是这一套接口。

因此准确的表述是：

> FAISS 提供 filtered search 的执行钩子（ID selector），不提供属性过滤索引。属性目录/bitmap/posting 的构建、选择率估计与“低选择率时换策略”都要在 FAISS 之外完成。

本仓库使用的 PathSeer FAISS fork（`deps/PathSeer`，commit `6e0cd1d`，remote `https://github.com/lizhiyuell/PathSeer`）额外提供 ACORN、PathSeer、FilteredHNSW、NaviX 等索引类型，但它们仍消费同一个 `IDSelector*`，不是属性谓词引擎。

## 2. 接口证据

| 组件 | 位置 | 事实 |
|---|---|---|
| 通用参数 | `deps/faiss/faiss/Index.h:90`（core）；`deps/PathSeer/faiss/faiss/Index.h:74` | `SearchParameters.sel` 是 `IDSelector*`，注释写明 “only these IDs will be considered during search” |
| Selector 类型 | `deps/PathSeer/faiss/faiss/impl/IDSelector.h:26-142` | `IDSelectorRange`、`IDSelectorArray`、`IDSelectorBatch`、`IDSelectorBitmap`、`IDSelectorNot`、`IDSelectorAll` |
| Flat | `deps/PathSeer/faiss/faiss/IndexFlat.cpp:33-44` | 读取 `params->sel`，传给 `knn_L2sqr` / `knn_inner_product`；仍扫描全部 `ntotal` |
| IVF | `deps/PathSeer/faiss/faiss/IndexIVF.h:371-376`；`IndexIVF.cpp:415-450` | `SearchParametersIVF` 继承 `sel`；扫描 posting 时按 selector 跳过 |
| HNSW | `deps/faiss/faiss/impl/HNSW.h:55-60`、`impl/HNSW.cpp:1132-1191`；fork 同位置 `HNSW.cpp:565,577,634` | `SearchParametersHNSW` 继承 `sel`；selector 决定哪些节点进入结果堆 |
| ACORN / PathSeer | `deps/PathSeer/faiss/faiss/impl/ACORN.h`、`impl/PathSeer.h` | 各自的 `SearchParameters` 继承 `sel`，过滤方式仍由 selector 提供 |

## 3. 关键语义：过滤的是结果堆，不是遍历

HNSW 的 filtered search 不是 post-filter：不可行节点仍会被访问、计算距离、继续扩展邻居；只是不进入结果堆。停止条件仍按“已处理且距离小于当前最小候选”的**全部**候选数计算（`efSearch`），不是按可行候选数计算。

```text
// deps/PathSeer/faiss/faiss/impl/HNSW.cpp:565-640（简化）
sel = params ? params->sel : nullptr;
...
float d = qdis(v1);
if (nres < k || d < D[0]) {
    if (!sel || sel->is_member(v1, pos)) {
        // 只有可行节点进入 top-k 结果堆
    }
}
candidates.push(v1, d);          // 不可行节点仍进入候选队列
...
if (candidates.count_below(d0) >= efSearch) break;  // 计数包含不可行节点
```

直接后果：固定 `efSearch` 时，HNSW 的有效候选数约为 `efSearch * s`；要在选择率 `s` 下仍找到 k 个可行近邻，`efSearch` 大致需要按 `1/s` 放大。这就是本研究的核心曲线来源。`IndexFlat` 则是全量扫描 + selector 判定，成本与 `s` 基本无关（只省距离保留，不省扫描）。

## 4. 本仓库的实测核验

`production/nested-attr-vector-search/src/faiss_filtered.cpp --self-test` 构造一条 1 维链，可行集只在 `x>=360`。查询位于 `x=0`：

```text
self-test: filtered_top1=360 feasible=1 | post_filter(candidates=10) feasible=0
SELFTEST PASS
```

若 selector 只做结果后过滤，filtered top-1 不会越过不可行链找到 360；该测试同时证明遍历未被剪枝、结果被过滤。

`selectivity_bench.cpp` 的 `self_check` 进一步在 SIFT100k 上把 `exact_topk` 的 top-10 与 `faiss::IndexFlat + IDSelectorBitmap` 在 s=0.01 和 s=1.0 上逐 ID 对比，作为 oracle 的独立校验。

## 5. 对属性过滤索引研究的含义

1. **属性索引必须自建。** 目录 trie、inclusive/Local bitmap、posting list、选择率直方图都不在 FAISS 内。FAISS 只负责“给定可行位图后如何搜”。
2. **过滤成本分两段。** `resolve`（属性条件 → 位图）与 `search`（位图 → top-k）。前者对 TrieHI 是 O(n/8) 的位图拷贝，对 PE-Online 是 O(子树节点数 × n/8) 的 OR；后者随 `s` 变化。两者必须分开计时，否则会把目录层成本误记成 ANN 成本。
3. **低选择率没有免费的午餐。** BF-HNSW 固定 ef 会随 `s` 下降丢召回；把 ef 放大到 `~k/s` 又会把延迟推向全图扫描。可行的系统策略是：低 `s` 直接枚举可行 ID 做精确扫描；高 `s` 用 BF-HNSW；中间按代价模型切换。这个交叉点正是选择率实验要测的量。
4. **不要把 fork 的“属性感知图”当成核心 FAISS。** ACORN/PathSeer 需要构建期 metadata 或专用图；它们不是 `IndexHNSWFlat + sel` 的等价物，结果要分别标注。

## 6. 复现命令

```bash
# 接口自检（PathSeer fork）
LD_LIBRARY_PATH=deps/PathSeer/faiss/build/faiss \
  production/nested-attr-vector-search/work/bin/faiss_filtered --self-test

# 选择率基准（见同目录 selectivity-benchmark.md）
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --nq-per-bucket 200 --max-ef 16384
```

## 7. 局限

- 证据来自本机两个 FAISS 版本：core `deps/faiss`（manifest `5b4aa46`）与 PathSeer fork（`6e0cd1d`）。其他版本/分支的 selector 语义应以对应源码为准。
- `IDSelectorBitmap` 是稠密位图；Roaring/压缩位图不是 FAISS 自带能力。属性索引的压缩与更新成本不在本笔记结论内。
- FAISS 的 `sel` 不改变图结构；ACORN/PathSeer 的构建期属性感知是另一类方法，不能由本接口推出。
