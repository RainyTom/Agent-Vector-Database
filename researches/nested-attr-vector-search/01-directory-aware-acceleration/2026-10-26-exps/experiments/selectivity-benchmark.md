# 实验协议：选择率对属性过滤索引效率的影响

日期：2026-10-26。代码：`production/nested-attr-vector-search/src/selectivity_bench.cpp`；驱动：`selectivity_harness.py`。

## 1. 问题与假设

定义选择率

$$
s(Q)=\frac{|\{i: i \text{ 满足属性条件}\}|}{n}.
$$

要回答的不是“某个索引在单一选择率下快不快”，而是：

- H1：固定 `efSearch` 的 BF-HNSW（FAISS `IndexHNSWFlat + IDSelectorBitmap`）召回随 `s` 下降，维持 Recall@10 ≥ 0.95 所需的 `ef` 约按 `1/s` 增长。
- H2：精确枚举可行 ID 的扫描成本约按 `s·n·d` 线性增长；在低 `s` 它支配 BF-HNSW，存在可测的交叉选择率 $s^\*$。
- H3：目录解析成本（TrieHI 的 inclusive 位图拷贝 vs PE-Online 的子树 OR）与 `s` 近似无关；当 `s` 很低、端到端查询进入几十微秒量级时，解析成本占比显著上升。
- H4：post-filter（先取全局 top-`ef`，再筛可行）在低 `s` 退化最严重，因为其候选池中可行点比例就是 `s`。

这些是工作假设，不是结论；失败/反向结果照实记录。

## 2. 数据与谓词

- 向量：SIFT100k，`n=100000`，`d=128`，L2；1000 条真实查询向量。
- 层级：`work/fixtures_ccs/` 的 1365 节点 CCS 形状树，100000×5 标签（含 `-1` 填充；30146 个对象无标签）。该层级是合成校准，不是真实 CCS；结论只对“层级形状 + 标签相关性”这一设置负责。
- 三种谓词模式：
  1. `random`：每条查询独立均匀抽 `round(s·n)` 个 ID，精确控制 $s\in\{0.001,0.003,0.01,0.03,0.1,0.3,1.0\}$。这是选择率对照轴，属性与几何独立。
  2. `sub`：递归目录范围 `Inc(v)`（节点自身及后代），按实际 `s` 分桶；单节点谓词。
  3. `exact`：非递归范围 `Local(v)`（直接标注在该节点的对象），按实际 `s` 分桶；单节点谓词。
- 每个桶最多 200 条谓词，前 100 条调参、后 100 条评估；查询向量循环使用 `test.f32`。层级谓词按位图哈希去重，避免同一可行集重复计数。

## 3. 方法

| 方法 | 实现 | 访问过滤的方式 | 计时 |
|---|---|---|---|
| `exact` | 枚举位图中置位 ID，逐点平方 L2，k=10 最大堆 | 只访问可行点 | 枚举 + 距离 + top-k |
| `hnsw` | FAISS `IndexHNSWFlat(M=32, efConstruction=200)` + `BitmapSel` | 遍历不剪枝，仅可行点进结果堆 | selector 构造 + 搜索 |
| `post` | 同一 HNSW，无 selector；取全局 top-`ef`，筛可行；不足 k 时 `ef×4` | 结果后过滤 | 所有重试轮次 |
| `exact` + `triehi` | 先 `memcpy` 节点的 inclusive 位图，再枚举 | 目录层解析 | 解析 + 扫描 |
| `exact` + `peonline` | 查询时 DFS 子树、OR 各节点 Local 位图，再枚举 | 目录层解析 | 解析 + 扫描 |
| `exact` + `local` | `memcpy` 节点的 Local 位图，再枚举 | 目录层解析 | 解析 + 扫描 |

- `hnsw` 的 `ef` 在调参半集上从 `{16,32,64,128,256,512,1024,2048,4096,8192,16384}` 中取满足 Recall@10 ≥ 0.95 的最小值；若都不满足，取最大并标记失败。
- `post` 的初始 `ef = max(16, ceil(2k/s))`，不足时 ×4，上限 `max_ef=16384`。
- `exact` 无参数；它与 oracle 使用同一例程，因此另用 `faiss::IndexFlat + IDSelectorBitmap` 在 s=0.01 与 s=1.0 上做独立逐 ID 校验。
- 单线程查询；`OMP_NUM_THREADS=1`；HNSW 图构建一次并缓存 `work/selectivity/hnsw_M32_efc200_n100000.faiss`。

## 4. 指标

- 每个 (mode,bucket,method,variant) 报告：QPS、Recall@10、p50/p95 µs、平均 resolve µs、ef、nq。
- `qps_at_r95`：仅当评估半集 Recall@10 ≥ 0.95 且无重复/不可行 ID 时计入。
- 主指标 `gmean_qps_r95`：`random` 七个桶的 per-bucket 最优 gated QPS 的几何平均。包含 exact scan，故 frontier 总有定义；它衡量“在该选择率下可达到的最好效率”，不是单一索引的普适性能。
- 次指标 `gmean_policy_qps_r95`：固定策略阈值 `--policy-s` 以下用 exact list、以上用 BF-HNSW 的几何平均。100k 分析历史使用 0.01（报告明确指出该阈值偏低）；1M 用 0.02；代码默认 0.06。阈值不是从评估半集调出来的。
- `crossover_s`：七个桶中 BF-HNSW gated QPS 首次超过 exact 的桶下界；仅作粗定位，不内插。
- 目录层单独报告 `resolve_us`；端到端 QPS 已包含解析成本（对应 variant）。

## 5. 有效性与控制

- Oracle：`exact_topk` 与 FAISS `IndexFlat + sel` 在 s=0.01、s=1.0 上逐 ID 对比；不一致直接失败。
- 每个 `exact` 行的评估召回必须 ≥ 0.999，否则 summary `valid=false`、进程返回 3。
- 所有 selector 方法检查不可行 ID 与重复 ID；非零即该行 `ok=false`。
- 调参/评估分半，避免用评估半集选 ef；方法选择仍用评估半集计算 frontier，这是“可达前沿”而非线上策略，报告中分开说明。
- 索引构建时间（冷启动）单独记录，不计入 QPS；缓存命中时 `build_s=0`。

## 6. 复现

```bash
# 一次性生成 fixtures（若缺失；需要 SIFT100k base/test）
python3 production/nested-attr-vector-search/src/prep_ccs.py \
  --fixtures <dir-with-base.f32-test.f32> \
  --out production/nested-attr-vector-search/work/fixtures_ccs

# 选择率基准
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --nq-per-bucket 200 --max-ef 16384

# 仅重建索引
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --nq-per-bucket 200 --max-ef 16384 --rebuild-index
```

输出：`production/nested-attr-vector-search/work/selectivity/{summary.json,selectivity.jsonl,selectivity.csv}`。

### 1M 规模复测

```bash
# 用 nested-attr work venv（含 h5py）导出 SIFT1M
production/nested-attr-vector-search/work/venv/bin/python \
  production/nested-attr-vector-search/src/export_sift1m.py

# 随机谓词；1M HNSW 冷构建约 500 s，索引 625.9 MB
python3 production/nested-attr-vector-search/src/selectivity_harness.py \
  --fixtures production/nested-attr-vector-search/work/fixtures_1m \
  --out-dir production/nested-attr-vector-search/work/selectivity_1m \
  --modes random --nq-per-bucket 200 --max-ef 8192 --policy-s 0.02 --repeats 3
```

1M 只跑随机谓词；结果见报告的 §5。100k 的主指标与 1M 不可直接比较。

## 7. 已知局限

- 100k 与 1M、单一数据集（SIFT）；10M 未测。100k 的层级/目录模式未在 1M 重跑，1M 只有随机谓词与 M=16 一个 HNSW 构型。交叉点数值不能外推到 10M。
- `random` 谓词与几何独立；真实属性通常与向量分布相关。层级模式有部分相关性，但不是真实 CCS 标注。
- 只测单谓词；多节点 OR、AND/NOT 与更新未覆盖。
- `hnsw` 是 filter-agnostic 的 FAISS selector 路径；ACORN/PathSeer 等属性感知图未进入本轮，不能据此评价它们。
- `peonline` 是简化的逐字节 OR，不是 Roaring；绝对 µs 只代表该实现。
- 调参半集与评估半集来自同一 1000 条查询池，仍可能有轻度分布重叠；未做跨数据集调参。
