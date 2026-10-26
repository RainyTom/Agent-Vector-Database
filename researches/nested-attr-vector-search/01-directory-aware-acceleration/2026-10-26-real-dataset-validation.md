# 真实数据集验证：WIKI-Dir / ARXIV-Dir（2026-10-26）

> 本方向此前因 alphaXiv 查重收束（见 README 与 ../alphaxiv-novelty-audit.md）。
> 本轮不是重提独立立项，而是**验证性实验**：拿到论文发布的数据，在其声明的
> 真实负载上复现 Table IV / Fig.9 的核心结论，并检验"真实约束"下论文机制
> 叙述是否仍然成立。

## 数据来源与缺口

- 仓库：`github.com/KurtPatrickHere/dir-vector-dataset`（论文 footnote 1/5），
  Google Drive 镜像。下载脚本与文件清单见
  `codes/directory-aware-vector-attr-search/data/real/`（gitignored）。
- 已下载：wiki 路径表（1,941,679 条目）、arxiv 元数据（2,763,543 条，
  每条例 `categories[]` + `time`）、1000 条真实 category 约束、
  category ground truth、456/1000 查询文本。
- **未下载**：`dbpedia_dir_2m_corpus_vectors.fvecs`(7.9GB) 与
  `arxiv_corpus_vectors.fvecs`(11.3GB)。本机 11GB RAM 无法承载
  ANN 索引 + 向量（HNSW M16 约需 向量 8–12GB + 图）。→ 本轮只测
  **目录范围解析延迟（DSQ）与目录维护（DSM）**，即论文 Table IV 与 Fig.9；
  ANN 质量（NDCG/Recall）与端到端未复现，留待大内存机器。
- 发布方数据问题：`arxiv_query_constraint.json` 实际内容是 1000×1024
  查询向量（fvecs），一般 DSQ 的目录约束**未发布**；wiki query.jsonl
  metadata 为空。一般查询锚点按论文协议合成（Fig.10 深度分布），
  category 约束用真实数据。

## 构建统计（fixture: codes/.../data/real_fixtures/）

| 数据集 | 目录数 | 条目 | 最大深度 | 说明 |
|---|---|---|---|---|
| WIKI | 468,031 | 1,941,679 | 31 | 论文报 363k；我们保留全部实际中间前缀 |
| ARXIV-subject | 168 | 2,763,543（多绑定 5.9M 对） | 3 | 与论文 168 一致 |
| ARXIV-temporal | 432 | 2,763,543 | 2 | 年/月两级 |

GT 语义校验：1000/1000 条真实 category 约束的递归前缀 scope 完整覆盖
发布方 ground truth —— "约束路径 ⇒ 前缀子树"的递归语义得到发布数据确认。

## DSQ 结果（dir_mean_us，resolve() 单次）

WIKI（457 查询，锚深度 1/2/4/9 按 Fig.10 权重，50% 递归）：

| 设计 | recursive | non-recursive | 论文 Table IV（对照） |
|---|---|---|---|
| PE-Online | 2218.92 | **1.11** | 365000 / 29.67 |
| PE-Offline | **3.36** | 51.42 | 235.23 / 1037.07 |
| TrieHI | **3.80** | 54.78 | 273.10 / 945.17 |

ARXIV（2000 查询混合：1000 真实 category + 500 叶非递归 + 250 年递归 + 250 月非递归）：

| 设计 | recursive | non-recursive | 论文 Table IV（对照） |
|---|---|---|---|
| PE-Online | 807.62 | **9.48** | 0.43 / 4.35 |
| PE-Offline | **13.48** | 36.89 | 0.12 / 163.91 |
| TrieHI | **11.79** | 35.35 | 0.12 / 12.49 |

ARXIV 真实 category 约束子集（1000 条，全递归）：PE-Online **923.9µs** /
PE-Offline 13.5µs / TrieHI 11.1µs。

## 结论

1. **论文核心 trade-off 在真实数据上方向完全一致**：非递归查询
   PE-Online 快 46–980×（直接读 Local 或单并集），递归查询 PE-Online 慢
   一至三个数量级（须现场并集整棵子树）。三设计同 resolve 语义下的
   真实相对关系成立。
2. **论文对 PE-Online 递归 "O(1) 元数据查找" 的叙述在小 scope 外不成立**：
   真实 category 约束 521/1000 锚在内部目录，PE-Online 比物化设计慢
   70–84×。物化（PE-Offline/TrieHI）递归延迟与 scope 基数同阶，稳定。
3. 绝对值与论文差 1–3 个数量级（论文 WIKI 递归 365ms 对应极大的
   scope；我们的锚分布更浅、机器不同）。**只比较相对关系与机制方向**。
4. 合成阶段结论仍有效：DSQ 差异在三设计共享 resolve 语义时主要为
   常数/负载效应；DSM 才是设计分化的主战场（TrieHI 免递归更新物化）。
5. **DSM 在真实 WIKI 树上复现了设计分化**（2000 ops，三设计 audit=ok）：
   MOVE：TrieHI 2.77ms < PE-Offline 5.74ms < PE-Online 20.14ms（扩展型设计
   子树 path-key 重写代价）；MERGE：PE-Online 0.051ms < TrieHI 0.126ms <
   PE-Offline 0.167ms（物化设计须沿祖先链修补）。两种操作的最优设计相反，
   与论文 §V-C 的机制叙述一致。

## 局限

- 无向量 → 无 ANN 质量与端到端数字；需要 ≥24GB RAM 机器补全
  （fvecs 可直接流式读入，IndexIVFPQ 可降内存）。
- 一般查询约束为合成锚点；论文原约束若日后发布应直接替换复测。
- DSM 结果见 `codes/.../experiments/real_wiki_dsm_dsm.csv`（wiki 2000 ops）。
- 本机 WSL2，单线程计时；与论文集群/服务器配置不可比。

## 产物

- 代码：`codes/directory-aware-vector-attr-search/src/repro.cpp`（scope 模式、
  多绑定 .pairs、run_dsm 泛化）、`scripts/build_real_fixtures.py`
- 协议：`codes/directory-aware-vector-attr-search/experiments/protocol.md`
- 数据：原始（gitignored）`codes/.../data/real/`；fixture `data/real_fixtures/`
- CSV：`experiments/real_{wiki,arxiv,arxiv_cat,wiki_dsm}_*.csv`
