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

---

## 附：新增基线 ACORN（官方 γ 实现，2026-11-05）

实现：`guestrin-lab/ACORN` 的 FAISS 分支（`faiss/IndexACORN.{h,cpp}`），以 `-DFAISS_OPT_LEVEL=generic` 构建（无 AVX2 内核）。`IndexACORNFlat(128, M=32, gamma=12, metadata=概念 ID, M_beta=32, L2)`，构建 168,937 条用时 43 s（16 线程）。驱动：`codes/dag-ann-search/src/acorn_official.cpp`。

**方法口径**：ACORN 的选择率回退由调用方实现。驱动按每查询选择率 `s = |allowed|/N` 决定路径：`s < thresh` 走 pre-filter（对合格集精确扫描，与 B-scan 同一实现），否则走 ACORN-γ 图搜索。默认 `thresh = 1/gamma ≈ 8.3%`。

### 低选择率（0.67%，合格集均值 1,140，200 查询）

| thresh | pre-filter 查询 | ACORN 查询 | recall@50 | 总 QPS | pre-filter QPS | ACORN QPS / recall |
|---:|---:|---:|---:|---:|---:|---|
| 0.020 | 187 | 13 | 0.9069 | 11,983 | 24,280 | 1,555 / 0.700 |
| 0.042 | 192 | 8 | 0.9126 | 13,375 | 20,285 | 1,646 / 0.788 |
| **0.083（1/γ）** | **197** | **3** | **0.9132** | **12,380** | 14,237 | 1,692 / 0.833 |
| 0.150 | 198 | 2 | 0.9132 | 11,783 | 13,102 | 1,491 / 0.810 |
| 0.300 | 200 | 0 | 0.9132 | 10,238 | 10,486 | — |
| 1.000（纯 pre-filter） | 200 | 0 | 0.9132 | 9,685 | 9,918 | — |

### 高选择率（1.4%–19%，合格集 2,320–32,051，均值 8,629，20 查询）

| thresh | pre-filter 查询 | ACORN 查询 | recall@50 | 总 QPS | pre-filter QPS | ACORN QPS / recall |
|---:|---:|---:|---:|---:|---:|---|
| 0.020 | 7 | 13 | 0.7720 | 1,727 | 3,860 | 1,349 / 0.758 |
| 0.042 | 12 | 8 | 0.7860 | 2,437 | 3,175 | 1,853 / 0.798 |
| 0.083（1/γ） | 17 | 3 | 0.7870 | 1,949 | 1,983 | 2,051 / 0.833 |
| 0.150 | 18 | 2 | 0.7870 | 1,785 | 1,807 | 1,736 / 0.810 |
| 1.000（纯 pre-filter） | 20 | 0 | 0.7870 | 1,300 | 1,309 | — |

### 读数

- **主工作负载（0.67%）整档低于 1/γ**：按文档口径 197/200 查询回退到 pre-filter，ACORN 图搜索只处理 3 条，方法收益在噪声内（12,380 vs 9,685–10,700 QPS，同召回 0.9132）。
- **ACORN 图搜索的固定代价**：约 600 µs/查询，等价于扫描约 7,300 个 item；pre-filter 的边际成本约 82 ns/item。两者相等处 ≈ **4%–5% 选择率**，与论文的 1/γ = 8.3% 同量级。
- **高选择率段确有收益**：4%–19% 的查询交给 ACORN，总 QPS 1,300 → 2,437（1.9×），召回持平（0.787 vs 0.786）。这是 ACORN 在本文工作负载上唯一起作用的区间。
- **仍未复现论文量级（2–10×）**：谓词是 1,929 个概念上的闭包，标签感知剪边把图切成大量分量，图搜索的收益被抵消；且我们的合格集最大仅 32k，pre-filter 本身不贵。

### 边界

- 20 查询那组样本小，QPS 有 ±10% 波动；两档的"总 QPS"混合了不同查询子集，混合值不能单独与某一臂直接比，须看同召回下的总 QPS。
- 相等谓词的 GT 尚未生成，故相等语义下不报 ACORN 数字。
- FAISS generic 内核与 `raw_l2_space` 标量实现不同源，pre-filter 路径已在驱动内用与 B-scan 相同的代码，保证可比。
