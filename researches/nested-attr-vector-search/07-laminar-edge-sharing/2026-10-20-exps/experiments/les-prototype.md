# 实验记录：LES Subtree 原型与同预算对照

日期：2026-10-20。目标：验证 LES 在 Subtree 上的吞吐是否接近 B4，同时兑现其 level-0 边去重的空间节省；Exact 模式单独比较扫描。

## 1. 实现

- `production/nested-attr-vector-search/src/les_build.cpp`：从 B4 的 85/1365 个作用域紧凑图（`c_*.bin`）中收集 level-0 有向边；计算 $A_i\cap A_j$ 与极小激活节点；按对象分桶、按激活先序排序；写共享向量（u8）、桶条目（邻居 4B + 激活 2B）、偏移、每节点入口；并抽取每个作用域的上界层结构（不共享）。
- `production/nested-attr-vector-search/src/les_query.cpp`：每节点先用其作用域上界下降到 level-0 入口，再在共享 level-0 并图上做 beam；邻居通过桶表的二分区间切片获取，热路径无谓词判定。

## 2. 修复的正确性问题

- **先序区间**：最初的 DFS 使用栈把子节点插在末尾，得到的是后序排列，`[tin,tout]` 不是子树的连续区间；改为显式栈模拟递归先序后，切片引理成立。
- **`act` 数组宽度**：构建误写成 u32 导致读出错位；改为 u16。

## 3. 结果（CCS 形状工作负载，1000 条 Subtree 查询，k=10）

原始数据：[`../data/ccs-les.csv`](../data/ccs-les.csv)。

| 方法 | ef | qps | Recall@10 | p95 | 索引 | 构建 | 线程 |
|---|---:|---:|---:|---:|---:|---:|---:|
| B4 逐作用域物化 | 21 | 74,706 | 0.9641 | 28 µs | 194 MB | 57 s | 1 |
| Filtered-HNSW | 384 | 1,282 | 0.9524 | 1.07 ms | 75 MB | 28.3 s | 1（构建改为 OMP_NUM_THREADS=1 对齐） |
| LES 原型 | 32 | 15,694 | 0.9967 | 0.17 ms | 104 MB | 4.5 s | 1 |
| LES 原型 | 48 | 11,908 | 0.9992 | 0.23 ms | 104 MB | 4.5 s | 1 |
| LES 原型 | 64 | 9,940 | 0.9999 | 0.23 ms | 104 MB | 4.5 s | 1 |

## 4. 条件性结论（当前配置）

- **同召回下 LES 原型比 B4 慢约 4.8–7.5×**（14.7k vs 74.7k），但比过滤 HNSW 快约 8–12×，索引是 B4 的 0.54×（104 vs 194 MB）；
- 召回明显高于 B4/Filtered-HNSW（LES 的并图密度更高），本实验用同一 ef 对照；为公平比较，LES 的 ef 需要按其召回曲线单独调；
- 这是**当前配置下的基线结果**，不是算法定论：构建线程已对齐，但 LES 的并图度数更大，实际空间/速度权衡取决于工作负载的粗细粒度比例；
- Exact 模式仍只有精确扫描基线（191.9k qps，1.0）；LES 直属桶的评测未实现。

## 5. 局限

- LES 原型只实现 Subtree；Exact 查询没有 LES 路径（需直属桶与回退）；
- level-0 边来自 B4 的 HNSW 构图，LES 的并图度数由此决定；换一个构图来源会改变该曲线；
- 单线程、静态数据；未覆盖更新/并发/目录调整。

## 6. 复现

```bash
# 构建 LES（从 B4 的紧凑图）
production/nested-attr-vector-search/work/bin/les_build \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --index-dir production/nested-attr-vector-search/work/index_ccs \
  --out production/nested-attr-vector-search/work/les_ccs

# 查询
production/nested-attr-vector-search/work/bin/les_query \
  --out-dir production/nested-attr-vector-search/work/les_ccs \
  --queries production/nested-attr-vector-search/work/pathseer_ccs/query/query.bin \
  --workload production/nested-attr-vector-search/work/fixtures_ccs/workload_sub.txt \
  --out /tmp/ids.bin --timings /tmp/tim.f64 --ef 32 --repeats 3

# 评分
tools/.venv/bin/python production/nested-attr-vector-search/src/eval_ccs.py \
  --fixtures .../fixtures_ccs --workload .../workload_sub.txt \
  --gt .../gt_sub.bin --ids /tmp/ids.bin --timings /tmp/tim.f64 \
  --query-seconds <QUERY_SECONDS> --mode subtree
```
