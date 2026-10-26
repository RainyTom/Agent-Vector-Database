# 实验协议：Directory-Aware 复现（合成 → 论文真实数据集）

日期：2026-10-26。代码：`codes/directory-aware-vector-attr-search/`。

## 阶段 1：合成数据（shape-constrained, CCS）

生成器 `scripts/gen_workload.py`；SIFT1M 的前 100k 向量 + CCS 形标签。
运行（详见本目录早期记录）：`./build/repro --data data/sift100k --csv experiments/results`。

**结论（重跑 bg_63 与偏差审计）**：`resolve()` 在三套设计间是**同一份代码**，
目录范围解析的理论零成本成立；本复现中 PE-Offline 与 TrieHI 的 DSQ 差异是
亚微秒级抖动（方向可在两次运行间翻转），不构成系统性优劣。论文 Table IV 的
DSQ 差异主要来自合成负载的常数因子与机器差异；真正的设计 trade-off 体现在
DSM（TrieHI 免物化递归更新、扩展型设计须重写子树 path key）。

## 阶段 2：论文发布的真实数据集（WIKI-Dir / ARXIV-Dir）

来源：论文 footnote 数据集仓库 `github.com/KurtPatrickHere/dir-vector-dataset`
（Google Drive 镜像 `1T3z9IDUbIbq0Sm3A-uVGzUBp1Q7RCJgM`）。本机 11GB RAM
装不下 1.94M×1024 fvecs（7.9GB）与 2.76M×1024 fvecs（11.3GB），故只下载
目录结构与约束（约 2GB），**不跑 ANN/召回**，只复现论文 Table IV 的目录解析
延迟（DSQ）与 Fig.9 的维护延迟（DSM）。原始文件在 `data/real/`（gitignore）。

已知数据缺口（发布方原因，已在报告中声明）：
- `arxiv_query_constraint.json` 实为 1000 条 1024 维查询向量（fvecs），
  一般 DSQ 的目录约束未发布 → 按论文协议合成锚点。
- wiki `dbpedia_dir_2m_query.jsonl` 的 metadata 为空 → 约束未发布，
  按论文 Fig.10 深度分布（1/2/4/9, 权重 83/39/72/114）合成锚点。
- 两个数据集的 `*_corpus_vectors.fvecs` 未下载（RAM 限制）。

### 构建 fixture

```bash
V=../../production/nested-attr-vector-search/work/venv/bin/python
$V scripts/build_real_fixtures.py --src data/real --out data/real_fixtures
```

输出：`wiki_tree.txt`(468,031 目录) + `wiki_bindings.txt`(1,941,679 条目,
单绑定)；`subject_tree.txt`(168 目录) + `subject_bindings.pairs`
(2,763,543 条目,多绑定,共 5.9M 对)；`temporal_tree.txt`(432 目录)；
`workload_wiki.txt`(457 查询,深度 1/2/4/9,50% 递归)；
`workload_arxiv.txt`(2000 查询 = 1000 真实 category 约束[递归] + 500 叶锚
非递归 + 250 年锚递归 + 250 月锚非递归)；`dsm_wiki_ops.txt`(2000 ops,
50/50 MOVE/MERGE,条目加权,seed 20261026)。

GT 校验：1000/1000 条真实 category 约束的前缀 scope 完整覆盖发布方
ground truth（独立脚本复算）。

### 运行

```bash
bash build.sh
export LD=../../deps/PathSeer/faiss/build/faiss
./build/repro --mode scope --ds wiki --wl data/real_fixtures/workload_wiki.txt \
  --has-ns 0 --nsspec "wiki:data/real_fixtures/wiki_tree.txt:data/real_fixtures/wiki_bindings.txt" \
  --csv experiments/real_wiki
./build/repro --mode scope --ds arxiv --wl data/real_fixtures/workload_arxiv.txt \
  --has-ns 1 --nsspec "arxiv-subject:data/real_fixtures/subject_tree.txt:data/real_fixtures/subject_bindings.pairs;arxiv-temporal:data/real_fixtures/temporal_tree.txt:data/real_fixtures/temporal_bindings.txt" \
  --csv experiments/real_arxiv
# DSM（wiki）
./build/repro --mode scope ... --dsm-spec \
  "wiki:data/real_fixtures/dsm_wiki_ops.txt:data/real_fixtures/wiki_tree.txt:data/real_fixtures/wiki_bindings.txt" \
  --csv experiments/real_wiki_dsm
```

`--mode scope` 不加载向量、不构建执行器，只测三设计的 resolve() 延迟。

### 结果（2026-10-26）

WIKI（457 查询，混合锚深度）dir_mean_us：

| 设计 | recursive | non-recursive |
|---|---|---|
| PE-Online | 2218.92 | **1.11** |
| PE-Offline | **3.36** | 51.42 |
| TrieHI | **3.80** | 54.78 |

论文 Table IV WIKI-Dir：rec 365000/235.23/273.10，nonrec 29.67/1037.07/945.17。
方向完全一致：非递归 PE-Online 快 46–980×；递归 PE-Online 慢 600×。

ARXIV（2000 查询混合）dir_mean_us：

| 设计 | recursive | non-recursive |
|---|---|---|
| PE-Online | 807.62 | **9.48** |
| PE-Offline | **13.48** | 36.89 |
| TrieHI | **11.79** | 35.35 |

ARXIV 真实 category 约束子集（1000 条，全部递归前缀）：PE-Online 923.9µs /
PE-Offline 13.5µs / TrieHI 11.1µs。→ 论文对 PE-Online 递归 "O(1) 元数据查找"
的表述只在**小 scope** 锚点成立；真实约束大量锚在内部目录，PE-Online 须现场
并集整棵子树，比物化设计慢 70–84×。

DSM（wiki, 2000 ops，条目加权 MOVE/MERGE 各半，三设计 audit=ok）：

| 设计 | MOVE mean_ms | MERGE mean_ms |
|---|---|---|
| PE-Online | 20.14 | **0.051** |
| PE-Offline | 5.74 | 0.167 |
| TrieHI | **2.77** | 0.126 |

机制与论文 §V-C 一致：MOVE 下扩展型设计须重写子树 path key/entry key
（PE-Online 最贵），TrieHI 只沿祖先链修补物化集；MERGE 下 PE-Online 只并
Local 并重键，物化设计要沿祖先链修补，反转。

### 与论文数字不可直接比的部分

1. 目录解析延迟不包含执行器时间与端到端；本机与论文机器不同。
2. 论文 WIKI 树 363k 目录 vs 本构建 468k（我们保留全部实际中间前缀）。
3. 一般查询约束为合成（发布方缺失），category 约束为真实数据。
4. ANN 质量（NDCG/Recall）因无向量未复现，需要 ≥16GB RAM 机器补做。
