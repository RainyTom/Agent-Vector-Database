# 实验记录：Exact 模式的结构性缺口与两项创新验证

日期：2026-10-20。目标：验证两个候选创新——(a) 距离序邻接 + 三角不等式剪枝，(b) 在共享桶上用 direct-direct 边直接服务 Exact。结果是两个有证据的否定与一个结构性发现。

## 1. 三角不等式剪枝（精确，但无效）

实现：构建期给每条 level-0 边存 `dist16 = floor(sqrt(L2²(source, neighbor)))`；查询时按精确界 `dist16 - d(q,c) > tau` 跳过候选。

- 精确性：Recall@10 与未剪枝一致（ef=32 时 0.9967，0 违规）；
- 效果：`PRUNED 0 / SCANNED 5,745,081 = 0.00%`（ef=32）。

原因：HNSW 图的邻居都离 source 很近，而查询到 source 的距离 $d(q,c)$ 通常不小于边长，界 `dist16 - d(q,c) > tau` 几乎不成立。该剪枝在度量空间聚类场景中有效，但在 kNN 图的“邻居贴近 source”结构下不提供跳过收益。**精确但无收益的负结果。**

## 2. Exact 用 direct-direct 边：结构性缺口

`--mode exact_edges`：只对 direct-direct 标记的边做 beam。结果：

- 0.922 召回、739 违规（违规来自未过滤的 seed；`exact_strict` 过滤后 0 违规、0.9269 召回、59 空）；
- 按节点统计（见下表），中高层节点（深度 1–2）的 direct 对象只有 **73–90%** 能被 direct-direct 边连到；叶子接近 100%。

为什么：LES 的边来自逐作用域 HNSW 图。中高层节点的 direct 对象在该作用域图里往往只连到后代（不是 direct 对象），于是它们之间没有 direct-direct 边；BEAM 只能到达有 direct-direct 边的子集，漏掉“孤立”的 direct 对象。**Exact 是节点集合问题，不是边问题；共享边不能替代 direct 标签倒排。**

## 3. 结论

| 创新 | 结论 | 证据 |
|---|---|---|
| 三角不等式剪枝 | 精确但 ~0% 跳过 | PRUNED 0/5.7M，召回不变 |
| Exact 用共享 direct-direct 边 | 不完整：中高层 direct 对象 73–90% 可达，漏孤立对象 | 0.922–0.927 召回，0 违规后仍漏 |

这改变了 Exact 的设计：Exact 必须用 direct 标签倒排 + 精确扫描（本 workload 中 191.9k qps，召回 1.0）。LES 的直属桶不能作为完整 Exact 路径；它至多可作为 large-scope Exact 的补充加速（且需证明覆盖）。

## 4. 复现

```bash
# 三角剪枝（已集成到 les_query，含 PRUNED 统计）
production/nested-attr-vector-search/work/bin/les_query \
  --out-dir .../les_ccs --queries .../query.bin \
  --workload .../workload_sub.txt --ef 32 --repeats 3

# Exact 缺口
production/nested-attr-vector-search/work/bin/les_query --mode exact_strict \
  --out-dir .../les_ccs --queries .../query.bin \
  --workload .../workload_exact.txt --ef 32 --repeats 3
```
