# 实验记录：fixtures_ccs → PathSeer 转换与 Filtered-HNSW / ACORN 基线

日期：2026-10-20。目标：在同一批 CCS 形状 Subtree 查询和同一 oracle 下，跑通有开源代码的过滤 ANN 基线，先验证正确性再测吞吐。

## 1. 转换器与正确性检查

`production/nested-attr-vector-search/src/to_pathseer.py` 输出：

- `load/load.bin`：u32 n,d + n*d float32（原始 SIFT，值域 0–255 的整数；不归一化）；
- `query/query.bin`：u32 nq,d + nq*d float32；
- `query/query_sub.bv`、`query/query_exact.bv`：每查询 `ceil(n/8)` 字节位图，bit i 对应对象 i（LSB first，与 `faiss::IDSelectorBitmap` 一致）；
- `golden/golden_{sub,exact}_l2_10.bin`：u32 nq,k + nq*k int32，0-based ID；
- `load/load.meta`：每对象主直接标签（仅供 ACORN 构图实验，单标签编码）；
- `check.json`。

检查结果：

| 检查 | Subtree | Exact |
|---|---|---|
| 位图 popcount == oracle 作用域大小 | 通过 | 通过 |
| golden ID 0-based 且在范围内 | 通过 | 通过 |
| golden 全部可行 | 通过 | 通过 |
| 作用域大小 min/max | 81 / 86,699 | 64 / 6,973 |
| 位图构造（从标签逐查询） | 3.05 s | 0.30 s |
| 位图构造（预计算节点位图 + OR） | 0.007 s（7.0 µs/query） | 0.008 s（8.0 µs/query） |

两种构造方式生成的位图逐字节一致。预计算路径是系统实际会采用的方式；逐查询从标签计算只是转换器的校验路径。

## 2. 位图是否真的约束遍历（小型人工测试）

构造 1 维链 $x=0..399$，仅 $x\ge360$ 可行，查询 $x=0$：

- 带 `IDSelectorBitmap` 的 Filtered-HNSW 返回 360（最近可行点）；
- 无 selector 的全局 top-10 再后过滤：0 个可行。

结论：位图参与图遍历的候选准入，而不是只在返回时过滤。测试同时暴露了两个实现问题：

1. 该 fork 的 `IDSelector::is_member(id, pos)` 默认返回 `false`；只重写单参数 `is_member(id)` 的 selector（包括 PathSeer benchmark 自带的 `IDSelectorBitmapLatency`）在 HNSW 路径上会过滤掉所有候选。驱动中必须同时重写两个签名。
2. 对“可行集远离查询”的情形，`ef` 必须大于不可行前缀的规模；否则 beam 在到达可行区域前就耗尽。这是过滤 HNSW 的固有弱点，也是基线的真实成本。

## 3. Filtered-HNSW 与 ACORN 实测

数据：CCS 形状工作负载（1365 节点、1000 条 Subtree 查询、k=10）。FAISS 构图多线程（10 核），查询为单查询调用（单线程）；B4/B0 为单线程。原始数据：[`../data/ccs-baselines.csv`](../data/ccs-baselines.csv)。

| 方法 | 配置 | ef | qps | Recall@10 | p95 | 索引 | 构建 |
|---|---|---|---:|---:|---:|---:|---:|
| B0 Subtree 精确扫描 | 后代标签并 + 距离 | – | 3,008 | 1.0000 | 1.16 ms | 51 MB | 0 |
| B4 逐作用域物化 | M24/efc200 | 21 | **74,706** | 0.9641 | 28 µs | 194 MB | 57 s |
| Filtered-HNSW | M32/efc200 | 64 | 5,020 | 0.7986 | 0.32 ms | 75 MB | 5.3 s |
| Filtered-HNSW | M32/efc200 | 256 | 1,912 | 0.9281 | 0.75 ms | 75 MB | 4.9 s |
| Filtered-HNSW | M32/efc200 | 384 | **1,282** | **0.9524** | 1.07 ms | 75 MB | 4.8 s |
| Filtered-HNSW | M32/efc200 | 512 | 991 | 0.9642 | 1.53 ms | 75 MB | 5.0 s |
| Filtered-HNSW | M32/efc200 | 1024 | 537 | 0.9844 | 2.63 ms | 75 MB | 4.9 s |
| ACORN（占位 metadata） | M32/γ1/Mβ4 | 128 | 2,021 | 0.8152 | 0.75 ms | 70 MB | 0.9 s |
| ACORN（占位 metadata） | M32/γ1/Mβ4 | 1024 | 394 | 0.8479 | 3.85 ms | 70 MB | 0.8 s |
| ACORN（主标签 metadata） | M32/γ1/Mβ4 | 512 | 620 | 0.8395 | 2.48 ms | 70 MB | 0.9 s |

## 4. 结论

1. **Filtered-HNSW 在本 workload 上远慢于物化作用域图**：达到 0.95 召回的最低 ef 为 384，1,282 qps；同召回下 B4 为 74,706 qps（约 58×），代价是 2.6× 索引字节。精确扫描 3,008 qps，B4 也快 25×。
2. **ACORN 的位图改造版达不到召回门槛**：占位 metadata（不使用层级构图）最高 0.848；把主直接标签作为单标签 metadata 也没有改善（0.8395）。这验证了“过滤感知构图”的必要性；但当前实验不是完整 ACORN 复现，必须标注为 filter-agnostic / 单标签元数据的变体。
3. 位图构造（预计算）约 7–8 µs/query，相对 Filtered-HNSW 的 0.3–2.6 ms 查询可忽略；若逐查询从标签计算则需 0.3–3 ms，不能忽略。
4. 正确性：所有方法 `violations=0`；golden 与 oracle 一致；自测证明位图约束遍历。

## 5. 公平性说明与待办

- B4 为单线程构建；FAISS 为多线程构建。构建时间不可直接比较，需补单线程对照或注明。
- 索引字节不同（194 vs 75 MB）；需补同内存预算下的 B4 变体（例如 M=16/更小截断）。
- ACORN 参数（γ、Mβ）未调；完整多标签编码/过滤感知构图需另做。
- 只测了 Subtree；Exact 已由 B0 扫描（191.9k qps，1.0）覆盖，Filtered-HNSW/ACORN 的 Exact 版本可后续补。
- 查询延迟为逐查询调用，未包含位图构造；位图构造单独报告。

## 6. 复现

```bash
# 转换（含检查）
tools/.venv/bin/python production/nested-attr-vector-search/src/to_pathseer.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_ccs \
  --out production/nested-attr-vector-search/work/pathseer_ccs

# 自测
production/nested-attr-vector-search/work/bin/faiss_filtered --self-test

# Filtered-HNSW
production/nested-attr-vector-search/work/bin/faiss_filtered \
  --dataset production/nested-attr-vector-search/work/pathseer_ccs \
  --mode sub --method hnsw --M 32 --efc 200 --ef 384 --k 10 --repeats 2

# ACORN（默认读 load.meta 作为单标签 metadata；删除该文件则退化为占位）
production/nested-attr-vector-search/work/bin/faiss_filtered \
  --dataset production/nested-attr-vector-search/work/pathseer_ccs \
  --mode sub --method acorn --M 32 --gamma 1 --Mbeta 4 --ef 512 --k 10 --repeats 2
```
