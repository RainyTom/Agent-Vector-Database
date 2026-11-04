# 独立 hnswlib 图方案

每个目录是一个独立的 C++ 程序和一份独立源码库，不共用另一方案的 hnswlib 文件，也不共用运行时图对象。

| 目录 | 方案及配置 |
|---|---|
| `dag_merge/` | 属性内 HNSW、DAG 组合及单层过滤搜索；CSR、资格/visited bitmap、prune16/24 为同一方案的消融 |
| `nodeann/` | 本地原生 HNSW `searchKnn` 与 top-k 归并；原生每图同 ef 是主对照，split-ef 为单独控制 |
| `acorn1/` | 原生 HNSW 构图＋ACORN-1 一/二跳搜索策略移植；不是作者完整 ACORN 索引复现 |
| `global_prefilter/` | 原生全局图＋资格前置、拒绝节点不扩展的控制 |

每目录含 `main.cpp`、`hnswlib/LICENSE`、`hnswlib/UPSTREAM.json` 和 `hnswlib/hnswlib/*.h`。七个原生 header 初始字节与 pinned `ca426729609b3221563047ceb269cc1213e0d376` 一致；各目录的 `dag_composition_method.h`、`global_acorn1_hnsw.h`、`global_prefilter_hnsw.h` 是各自持有的搜索/组合扩展。独立库修改应只影响本目录，不做自动同步。

共同 harness 位于 `researches/nested-attr-vector-search/2026-11-02-exps/experiments/`，统一数据、固定参数/插入序列、直接/递归资格、GT、正常计时、计数和结果输出。hnswlib 的建图/搜索算法代码由该程序自己的 include path 提供；不包含旧 `merge_arms.cpp`。

当前四方案统一 `M=64`、`efConstruction=200`，各自库扩展声明自己的构图参数；全局索引也使用对应方案的参数。原生 level-0 容量为 `2M=128`，上层为64。portal=8、反向桥上限16与可选合并后剪枝上限不是HNSW的M；未剪枝`merged`为主臂。ACORN搜索邻域截断读取实际`graph.M_`，本轮也随M变为64。

## 执行

```bash
bash autoresearch.sh
```

脚本分别编译四个 `main.cpp`，生成独立进程。编译用 `-MMD` 记录真实依赖，验证没有跨目录引用，也没有回落到旧共享 `deps/hnswlib`。每个源 header 的 SHA256 单独存档。

当前默认输出`work/dag-two-cases-m64/`，可用`DAG_PARETO_OUT`、`DAG_PARETO_CPU`、`DAG_PARETO_NQ`设置。正式矩阵为120查询×recursive/direct，只含两个固定格：`k=10,efSearch=32`与`k=100,efSearch=128`；NodeANN每图n在格内取1…k，其他方案各一个固定点。原生宽度至少为k，统一按`max(k,efSearch)`执行并同时记录请求/有效值。正常整批预热后测10批次，每批遍历非空查询16遍，计数另跑。程序保存全部点、原始批次和实测值，见[两格比较报告](../../researches/nested-attr-vector-search/2026-11-02-exps/dag-two-cases-report.md)。旧M16与九格探索数据保留，不改写。

历史24/120构图变体驱动仍可显式运行`bash researches/nested-attr-vector-search/2026-11-02-exps/experiments/run_dag_composition_validation.sh`；它不是当前默认Pareto矩阵。新增分层/b/等字节剪枝仅有研究设计（原设计文档已随方案清理删除），尚非已实现臂。

### DAG 执行器更新

`dag_merge` 已合入查询内拒绝缓存、frontier/best/pending 容量复用、结果堆 replace-root 和资格通过后的批量评分/预取。图结构、入口及距离内核不变，不以原生局部图回退代替单图搜索。复用工作区须由单一查询独占；查询期间资格掩码固定。容量按历史高水位保留，结果仍会分配，不称零分配。

仅严格 `b=0` 路径把拒绝状态写入本次 seen；后续查询由 epoch 隔离。未来有预算的桥跳转不能直接复用这个布尔状态。资格检查/拒绝计数和 trace 会减少，duplicate计数会增加，距离/扩展/邻接计数及返回顺序不变。未改造的CSR/dense臂保留旧拒绝检查方式，是独立消融，不能要求它们的资格调用数也相同。

只跑两格的真实before/after对照、完整审计和限制见[性能修复报告](../../researches/nested-attr-vector-search/2026-11-03-exps/report.md)。旧精确原因百分比分解已撤回。

行为回归：

```bash
g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/dag-composition-methods/dag_merge/hnswlib \
  -Icodes/directory-aware-vector-attr-search/src \
  researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_composition_test.cpp \
  -o /tmp/dag-composition-semantic-test
/tmp/dag-composition-semantic-test
```

完整语义与边界：[交付说明](../../researches/nested-attr-vector-search/2026-11-02-exps/dag-composition-delivery.md)。
