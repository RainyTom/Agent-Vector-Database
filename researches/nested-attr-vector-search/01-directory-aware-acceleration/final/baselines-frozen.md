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

## 附：新增候选基线（2026-11-05 之后）

研究问题需要**同一索引同时回答"属性包含"与"属性集相等"**。文献结论：

| 方案 | 支持范围（原文） | 零重建切换 | 开源 |
|---|---|---|---|
| **LSSG**（arXiv:2609.15058，接收声明 SIGMOD'27 未独立确认） | 明确 equality/containment/overlap，同一多层图按层与 scope 选 | 是 | `nju-websoft/LSSG` |
| **UNG**（DOI 10.1145/3698822，已发表） | 四种谓词；但官方 `--scenario equality` 去掉 LNG 与跨组边 | **否**（两次构建） | `YZ-Cai/Unified-Navigating-Graph` |
| **ACORN**（DOI 10.1145/3654923，已发表） | 谓词无关，`search` 接受 `filter_ids_map` 位图 | 是 | `guestrin-lab/ACORN`（FAISS 分支，`faiss/IndexACORN.{h,cpp}`） |
| Filtered-DiskANN / Stitched（DOI 10.1145/3543507.3583552） | 原文限于"单过滤器精确匹配"，AND 与集合相等列为 future work | 否 | `microsoft/DiskANN` |
| SIEVE（DOI 10.14778/3749646.3749725） | 每种 predicate form 一个索引 | 否 | `BillyZhaohengLi/SIEVE-vldb25` |
| TRIEHI（arXiv:2606.16903，预印本） | 递归=子树包含、非递归=精确节点，同一 trie | 是 | 并入 `volcengine/OpenViking` |

**空白**：以上全部在**平坦标签集**上，没有一篇处理 **DAG 可达闭包 + 多父**。

### 已测：ACORN 形态在低选择率下退化（本仓库移植，标量距离 + PQ 码）

168k / 20 查询 / k=50 / ef=50 / M=32：

| 指标 | 数值 |
|---|---:|
| recall@50 | **0.9132**（与两条基线相同） |
| 每查询不同访问节点 | **77,098** |
| 每查询作用域内合格对象 | 1,140（**0.67% 选择率**） |
| 每查询距离调用 | 117,659（合计 2,353 万） |
| 结果数 | 42.2 / 50 |
| QPS | **0.4** |

同规模同口径对照：B-scan 0.9132 @ 10,544 QPS、B-nodeann 0.9132 @ 10,880 QPS。
⇒ **全局图 + 谓词位图在此选择率下慢约 2.5 万倍，且凑不满 k**。与 ACORN 论文自身成本模型（低于 $1/\gamma$ 选择率退回 pre-filter）一致。

> 勘误：早先一版结果 recall≈0.03 / 0.9 QPS 是移植 bug（终止条件过早、距离计算未去重 668×），已修；上表为修正后数据（200 查询，8 分 39 秒）。

### 公平性口径（用户要求，2026-11-05）

**不使用官方 FAISS 版 ACORN**（`guestrin-lab/ACORN`，`faiss/IndexACORN.{h,cpp}`）：它是 float32 + AVX 内核，与 NodeANN 基线的"PQ 码 + 标量距离"不同口径，直接对比不公平。

采用的 ACORN 臂 = **在 hnswlib 上按 ACORN 机制改造**：
- 同一张 hnswlib 图（M=32，efC=200），向量为 **PQ 还原后的 float32**；
- 距离函数与 NodeANN 完全相同的**标量 for 循环**（`-O2 -fno-tree-vectorize`）；
- ef=50、k=50 与基线一致；
- 谓词 = 每查询的许可位图（闭包=包含 / 精确集合=相等），命中过滤的节点不入选、对其邻居做 2 跳扩展（ACORN-1 形态，γ=1）。

结论：**同召回（0.9132）下 ACORN 形态 0.4 QPS，比 NodeANN（10,880 QPS）慢约 2.7 万倍**，原因是它必须访问 77,098 个节点（合格集的 67.6 倍）才能凑齐结果。与 ACORN 论文自身成本模型一致（低于 $1/\gamma$ 选择率退回 pre-filter）。
