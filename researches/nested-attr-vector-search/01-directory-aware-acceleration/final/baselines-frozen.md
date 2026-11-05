# 基线冻结（2026-11-05）

冻结的是**两道基线**的实现与口径。此后不得修改其算法、参数默认值与度量方式；新方案只能在它们之上新增。

## 冻结对象

| 基线 | 定义 | 实现 |
|---|---|---|
| **B-scan：精确扫描** | 对合格集逐个算平方 L2，取 top-k | `codes/dag-ann-search/src/nodeann_bench.cpp --mode exact` |
| **B-nodeann：每概念独立图** | 每个非空概念一张 HNSW（M=16, efC=200）；查询对范围内各概念图各搜 top-N 再归并 | 同文件 `--mode nodeann` |

## 公共口径（两者共用，不得各自调整）

| 项 | 值 |
|---|---|
| 向量 | **PQ(m=16, k=256)** 编码，搜索在还原后的 float32 上；压缩 32×（1M: 16 MB 码 vs 512 MB FP32） |
| GT | 在**原始 FP32** 上算，k=50；每查询取 `min(k,|eligible|)` 为分母，ties-aware |
| 距离内核 | **标量 for 循环**，编译 `-O2 -fno-tree-vectorize -fno-tree-slp-vectorize`（无 SIMD） |
| ef | **固定 50**（与 k 一致；N ≤ 50 时不触发 hnswlib 的 ef 上抬） |
| 计时 | 单线程，整批 200 查询一遍，报 QPS = 200 / 墙钟秒 |
| 图构建 | M=16, efC=200, seed=100+c；可落盘复用（`--index-dir`） |

## 冻结数值

**数据**：CCS DAG（1,929 概念 / 2,040 父边 / 103 多父 / 深度 0–6）；查询 200 条（多概念 OR，按可达闭包）。

| 规模 | 合格集均值 | 基线 | recall@50 | QPS |
|---|---:|---|---:|---:|
| 168,937 | 1,140 | B-scan | 0.9132 | 10,544 |
| 168,937 | 1,140 | B-nodeann (N=50) | 0.9132 | 10,880 |
| 1,000,000 | 6,746 | B-scan | 0.8581 | 605 |
| 1,000,000 | 6,746 | B-nodeann (N=50) | 0.8569 | 4,808 |

**交叉结论（写进论文）**：NodeANN 相对精确扫描的同召回 QPS 比，在合格集均值 1,140 时为 **1.03×**，在 6,746 时为 **7.9×**。即收益由**合格集规模**决定，而非绝对优劣。

## 复现

```bash
# 数据与量化（幂等，已有则跳过）
work/venv-embed/bin/python codes/dag-ann-search/scripts/prepare_large.py          # 1M: objects/orig/queries
work/venv-embed/bin/python codes/dag-ann-search/scripts/prepare_pq.py --data work/dag-ann-1m
# 基线
./work/nodeann_scalar --dir work/dag-ann-1m --work work/dag-ann-1m --mode exact   --vec recon_pq.bin --nosimd --k 50 --queries 200
./work/nodeann_scalar --dir work/dag-ann-1m --work work/dag-ann-1m --mode nodeann --vec recon_pq.bin --nosimd --k 50 --ef 50 --N 50 --queries 200 --index-dir work/dag-ann-1m/idx
```

落盘资产（`work/`，未入 git）：`gt_50.txt`、`codes_pq.bin`、`recon_pq.bin`、`idx/`（768 个每概念图）。

## 冻结边界

- PQ 32× 是**召回上限**来源：1M 上连精确扫描也只有 0.858。任何新方案若在 code 空间搜索，天花板同样受此约束；要突破必须做**原始向量重排（rerank）**，那是新方案的范畴，不是基线的修改。
- 基线不含属性谓词（当前查询只有概念范围）；多属性过滤属于新方案要覆盖的设定，不在基线内。
