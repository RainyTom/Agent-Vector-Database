# 层级多属性向量检索研究仓库

研究主线：在**层级属性 DAG**（多父、OR 闭包）上做多属性过滤的向量近邻检索。我们的方案名为 **NestedAttrVectorRetrieval**：属性内近邻图 + 跨属性网关 + 闭包位图，查询用一条共享双队列（DQS）完成整条查询，而不是每个属性各搜一次再归并。

## 入口

| 用途 | 文件 |
|---|---|
| 问题定义 | [researches/nested-attr-vector-search/problem_def.md](researches/nested-attr-vector-search/problem_def.md) |
| **方案说明与观测面板**（构建、查询、参数、实测点、可调方向） | [researches/nested-attr-vector-search/approaches.md](researches/nested-attr-vector-search/approaches.md) |
| 最新实验记录（top-10） | [researches/nested-attr-vector-search/2026-11-08-exps/report.md](researches/nested-attr-vector-search/2026-11-08-exps/report.md) |
| 论文索引 | [papers/papers.jsonl](papers/papers.jsonl)、[papers/relations.jsonl](papers/relations.jsonl) |
| 研究规则 | [AGENTS.md](AGENTS.md) |

## 目录

```text
.
├── AGENTS.md                      # 研究约定
├── codes/                         # 每个方案一个目录
│   ├── NestedAttrVectorRetrieval/ # 本方案：build.sh、src/、scripts/
│   ├── NodeANN/                   # 基线：每属性一张 HNSW 再归并，build.sh、src/
│   ├── shared/                    # 共享头文件：raw_l2_space.h、native_graph_audit.h
│   └── third_party/hnswlib/       # 固定版本 hnswlib
├── datasets/                      # 数据集，按名称分目录（gitignored）
│   └── ccs-sift-dag/              # CCS 层级 DAG + SIFT 向量工作负载
├── researches/
│   └── nested-attr-vector-search/ # 每日实验记录与数据
├── papers/                        # 论文库：索引、全文与关系
└── paper-production/              # 论文写作工程
```

## 复现

```bash
./codes/NestedAttrVectorRetrieval/build.sh
./codes/NodeANN/build.sh
./codes/NestedAttrVectorRetrieval/build/attr_skeleton --k 10 --efs 10,200 --repeat 1
./codes/NodeANN/build/nodeann_measured --k 10 --ef 10 --N 10
```

默认读取 `datasets/ccs-sift-dag/`：`objects.txt`（对象→直属属性）、`edges.tsv`（子→父）、`queries.tsv`（首列为属性个数）、`orig.bin`（原始 FP32）、`recon.bin`（PQ 还原向量）、`queries.bin`、`gt_10.txt`。`scripts/prepare_large.py` 与 `scripts/prepare_pq.py` 可从 `datasets/sift-128-euclidean/` 与上述标签文件重建派生数据。

## 约定

- 数据集按名称放 `datasets/<名称>/`，不入库；代码只有本方案，放在 `codes/<方案名>/`。
- 研究记录按方向放 `researches/<方向>/`，只允许 `approaches.md`、`problem_def.md` 与 `<YYYY-MM-DD>-exps/`；方案文档只描述当前实现与实测，不写论证与复查。
- 提交源码、文档与小体积 JSON/CSV；不提交数据集、构建产物与索引。
- 结论必须能追到运行记录；未运行的结论标注为推断。
