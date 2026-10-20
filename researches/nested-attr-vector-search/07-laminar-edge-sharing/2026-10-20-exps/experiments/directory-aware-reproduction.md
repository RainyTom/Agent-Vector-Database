# 复现：Directory-Aware Query and Maintenance in Vector Databases

日期：2026-10-20。论文：arXiv:2606.16903v1（`papers/prefix-vector-search/2606.16903-directory-aware/`）。用户提供的 PDF 在 `.tmp/`。

## 1. 复现范围

完整复现论文的三层对照，**用我们自己的 ANN 与层级数据**，而非论文的 WIKI-Dir/ARXIV-Dir（2.76M × 1024D ≈ 11 GB，Google Drive 无 API key 无法在本环境拉取）。因此这是**机制与相对趋势的复现**，不是论文数值的逐位对齐。

| 论文 | 我们的复现 |
|---|---|
| PE-Online：直接目录 posting，查询时展开后代并集合 | `dir_representations.cpp` 递归 DSQ：对每个查询节点 DFS 后代、OR 直接 posting |
| PE-Offline：祖先 posting 物化，查时直接取聚合 | 预计算 inclusive posting，查询时 OR |
| TrieHI：稳定 segment Trie + 逐节点 inclusive bitmap + 结构重连 | 与 PE-Offline 共享 inclusive posting；MOVE/MERGE 用重连 + 祖先聚合更新 |
| Roaring bitmaps | 简化 64-bit bitmap（语义等价） |
| ANN 执行器 PG/IVF | 不在本次目录层复现（论文也强调 scope 层独立） |

## 2. 查询（DSQ）结果

数据集：我们的 85 节点主层级与 1365 节点 CCS 形状层级。

**85 节点（mean µs, directory-only）**

| Method | recursive | non-recursive |
|---|---:|---:|
| PE-Online | 3.92 | 0.45 |
| PE-Offline | 0.47 | 2.41 |
| TrieHI | 0.39 | 2.38 |

**1365 节点 CCS（mean µs, directory-only）**

| Method | recursive | non-recursive |
|---|---:|---:|
| PE-Online | 97.20 | 0.80 |
| PE-Offline | 1.23 | 5.27 |
| TrieHI | 0.76 | 4.43 |

趋势与论文 Table IV 一致：PE-Online 的递归查询随规模/深度急剧变慢（1365 节点 97 µs vs 0.8–1.2 µs），而 PE-Offline/TrieHI 把展开成本移到构建期；非递归查找则 PE-Online 最快，因为它不需要物化。正确性已验证：递归候选集合与 oracle 逐位一致（query 0）。

## 3. 维护（DSM）结果

在 1365 节点层级上做 200 次 MOVE 与 200 次 MERGE：

| 操作 | PE 展开重写 | TrieHI 重连 + 祖先聚合更新 |
|---|---:|---:|
| MOVE | 7.35 ms | 10.79 µs |
| MERGE | 5.88 ms | 9.24 µs |

**约 600–700× 差距**，复现论文的 DSM 优势来源：TrieHI 把目录表示为可重连的 trie 节点，结构变更只触及移动集合和受影响祖先的聚合，而不是重写受影响后代的路径键。PE 重写的成本随移动子树内的对象数线性增长，而 TrieHI 重连是 O(t)。

## 4. 与 LES 的关系

- PE-Online = 我们的 B0 Subtree（祖先展开 + 扫描）。
- PE-Offline = 我们的 B4（逐作用域物化）。
- TrieHI 的“稳定节点身份 + inclusive 集合”与 LES 的“前序区间切片”是**集合级共享**的同族近似；LES 进一步在**边级**共享邻接而不是集合，二者可比较但不是同一机制。
- 论文的 OpenViking 集成（dir_index）是目录路由 + bitmap 集合合并，接近 PE-Online，**不是**完整的 TrieHI；不能把 OpenViking 的 dir_index 当作 TrieHI 基线。

## 5. 局限与未复现

- 未复现论文的 WIKI-Dir/ARXIV-Dir 真实数据（下载受限）；也未复现 Viking PG/IVF 的 ANN 行为与端到端 QA。
- bitmap 是 64-bit 简化版，不是 Roaring 压缩 bitmap；Union/Intersect 语义一致但压缩率不同。
- MOVE/MERGE 是随机内部节点，不是论文的结构化/冲突场景；MERGE 的冲突规模最坏情形未专门构造。
- 目录层延迟与论文的端到端/目录层分解一致，但绝对数值不同（我们的树更小、向量维度更低）。

## 6. 复现命令

```bash
# 目录表示（DSQ 目录层延迟）
production/nested-attr-vector-search/work/bin/dir_reps \
  --fixtures .../fixtures_ccs --queries .../workload_sub.txt --out /tmp/da.csv

# 维护（MOVE/MERGE）
production/nested-attr-vector-search/work/bin/dir_dsm \
  --fixtures .../fixtures_ccs --moves 200 --out /tmp/da_dsm.csv
```

代码：`production/nested-attr-vector-search/src/dir_representations.cpp`、`dir_dsm.cpp`。

## 7. 历史数据的适用条件与方法名更正（追加）

以上 §2 的 non-recursive 时间是**实现路径计时**，不是对多标签 Exact 正确性的证明。`dir_representations.cpp` 只检查了首条查询的 recursive 候选集；PE-OFFLINE / TrieHI 的 `Inc(u) & ~OR(Inc(children))` 对 $L_i=\{u,v\}$、$v$ 为 $u$ 子节点会漏掉本应属于 Exact$(u)$ 的 $i$。递归结果仍适用于多标签 OR 作用域。多标签 Exact 应取 $\mathrm{Local}(u)=\{i:u\in L_i\}$，并对所有查询与直接标签 oracle 比对后重新测时；历史 non-recursive 数字不作为有效 Exact 性能。

§3 中 `dir_dsm.cpp` 的 TrieHI 路径用 `inc[v] & ~moved` 去旧祖先集合，未处理某对象同时经未移动分支仍属于同一祖先的情形；PE 路径也未对每种 MOVE/MERGE 冲突执行结果级核验。因此 7.35 ms / 10.79 µs 等为演示实现的**路径计时**，不是多标签维护的正确实现性能或原论文正式复现；且构建索引、向量索引与 ANN 查询不在该计时内。

§4 的 `PE-Online = B0`、`PE-Offline = B4` 仅在“先生成候选集”的局部操作上有表面类似性，不能视为基线等价。论文改以 PFS（posting 后扫描）、PSG（逐作用域 HNSW）命名向量执行器；PE-ONLINE/PE-OFFLINE/TRIEHI 是 [Directory-Aware 原论文](https://arxiv.org/pdf/2606.16903v1)的目录元数据索引。用户所需的强基线是 TRIEHI + 同一过滤 ANN 后端的端到端 Recall@k/QPS/索引字节，仍需实测，见 [paper.md §5.5](../../paper.md)。
