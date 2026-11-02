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

## 执行

```bash
bash autoresearch.sh
```

脚本分别编译四个 `main.cpp`，生成独立进程。编译用 `-MMD` 记录真实依赖，验证没有跨目录引用，也没有回落到旧共享 `deps/hnswlib`。每个源 header 的 SHA256 单独存档。

输出目录默认为 `work/dag-composition-validation-independent/`；可用 `DAG_VALIDATION_OUT` 改工作目录、`DAG_VALIDATION_CPU` 指定绑核。24 查询完整变体、120 查询主臂验证均分别测 `recursive` 与 `direct`。原始方案结果保存在各 run 的方法子目录，只在其上聚合 CSV，不合并任何图对象。

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
