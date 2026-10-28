# 全量 WIKI 两类基线复现协议

## 输入与命令

仓库根目录运行，依赖沿用 `codes/directory-aware-vector-attr-search/build.sh`（PathSeer FAISS fork、CRoaring）。大文件与生成 fixture 不入库。

- 全量语料：`datasets/dbpedia_dir_2m_corpus_vectors.fvecs`，7,960,883,900 bytes，1,941,679×1024。
- 查询原文件：`data/real_fixtures/wiki/query_vectors.fvecs`，1,869,600 bytes，456×1024。
- 真实树/绑定：`codes/directory-aware-vector-attr-search/data/real/{wiki_tree.txt,wiki_bindings.txt}`。
- 固定合成查询约束：[`../data/wiki_full_workload.txt`](../data/wiki_full_workload.txt)。只使用发布的 456 条查询向量，不把合成锚点说成原始查询约束。
- 原始路径元数据为 `data/real_fixtures/wiki/corpus_paths.jsonl`（JSON array，期望名 `dbpedia_dir_2m_corpus_paths.json`）。树/绑定缺失时，可用 `scripts/build_real_fixtures.py --src data/real_fixtures --out codes/directory-aware-vector-attr-search/data/real --wiki-only --seed 20261026` 重建；查询约束应使用本轮已保存文件，而非新 RNG 序列。

```bash
bash codes/directory-aware-vector-attr-search/build.sh
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-27-exps/experiments/run_wiki_full.py
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-27-exps/experiments/summarize_wiki_full.py
```

`run_wiki_full.py` 始终重做 fvecs 转换，并逐 float 位模式核对全部输出、验证有限值、保存 SHA-256 与范数范围。距离为发布浮点上的 squared L2，不再额外归一化。运行环境固定 OMP/OpenBLAS/MKL/BLIS=1，实验配置串行；每个配置只执行一遍 workload，不把单次运行当成多次重复统计。

脚本包含的三个配置（是否实际成功以各 `.command.json` 的 `exit_code` 为准）：

| 配置 | 执行器 | 参数 |
|---|---|---|
| `wiki_full_all3` | 全局 HNSW/IVF × PE-Online/PE-Offline/TrieHI；节点局部 HNSW | 全局 M16, efConstruction=200, efSearch=2048；IVF nlist=1024, nprobe=256；局部 threshold=0, M16, efConstruction=200, efSearch=64，完整候选节点，每节点 top-10，合并 top-10 |
| `wiki_full_node_flat` | 局部 Flat 精确对照 | threshold=2147483647，完整候选节点，每节点 top-10 |
| `wiki_full_ivf_exact` | 全局 IVF 全倒排列表对照 × 三种目录表示 | nlist=nprobe=1024 |

`/usr/bin/time -v` 记录整进程峰值 RSS 与耗时，**不是单个索引内存**。尤其 all3 同时持有多个索引。`_size.csv` 的 NODE 大小仅为向量 payload 下界，不含图、质心、ID 映射及索引对象；不得与全局序列化索引大小等同。目录模块大小也是当前实现的估算，不是进程 RSS。

## 指标

- 固定 GT：每查询 scope 内暴力 squared-L2 top-10，独立于目录表示和执行器，计算不计入查询延迟。
- `recall10` 原 CSV 列保留历史含义 `hit/10`；scope 少于 10 时即使结果完整也小于 1，**不能直接称为标准 recall**。
- 新增逐查询 `gt_count,hit_count,valid_return_count,query_index,search_us`。`query_index` 为 workload 行序号；具体发布 query id 见固定 workload。
- query-average recall：仅非空 scope 上平均 `hit/min(10,scope_card)`；empty scopes 从分母剔除并独立计数。
- global pair recall：`sum(hit)/sum(min(10,scope_card))`；空 scope 对分子、分母均贡献 0。
- QPS：串行 `1000/mean_e2e_ms`；延迟为 scope/候选节点解析 + ANN 搜索/合并，不含 GT、质量审计、CSV 输出和输入加载。
- NODE 不再调用全局 scope 位图解析；`bitmap_us=0`。搜索计时在独立 `search_us`，不是位图成本。
- 返回 ID 的去重与 scope 有效性审计在计时外，异常打印 `RESULT_SCOPE_WARNING`。WIKI 是单绑定；多绑定输入的 NODE 去重/非递归语义仍有限制，不外推本轮结果。

## 已观察的修复烟测

1. 旧转换脚本把原始 float 的 int32 位模式数值转换为 float。输入 `(1.0, -2.5, 0.125)` 实际输出 `(1065353216.0, -1071644672.0, 1040187392.0)`。修复后两条记录（含负值和 signed zero）字节完全保真。
2. 旧 real workload 有 457 行，query id=456 越过原始查询向量范围；本轮去掉该合成行，保留 0..455 全部发布查询。生成脚本改累计舍入，实际小型 fixture 生成 smoke 输出恰好 456 行；运行器遇越界 qidx 抛错，未取模回绕。
3. 12×4d、7 节点、4 查询的真实 CLI smoke 分别覆盖 root scope>k、local scope<k、递归子树、空节点。threshold=0 的 HNSW 与 threshold=2147483647 的 Flat 各返回 `(hit,GT)=(10,10),(2,2),(7,7),(0,0)`，NODE 的 `bitmap_us` 均为 0。越界 qidx=4（test.n=4）被拒绝。
4. 构建实际成功：`bash codes/directory-aware-vector-attr-search/build.sh`，输出 `built build/repro`。

旧 ANN 质量与延迟不可与本轮直接连线：向量转换、越界查询、NODE 双重 scope 成本同时修正；历史文件未覆盖。目录解析/DSM 本身不依赖向量，因此不因转换错误自动失效。
