# 层级多属性向量检索研究仓库

研究主线：在**层级属性 DAG**（多父、OR 闭包）上做多属性过滤的向量近邻检索。我们的方案名为 **NestedAttrVectorRetrieval**：属性内近邻图 + 跨属性网关 + 闭包位图，查询用一条共享双队列（DQS）完成整条查询，而不是每个属性各搜一次再归并。

## 入口

| 用途 | 文件 |
|---|---|
| 问题定义 | [researches/nested-attr-vector-search/problem_def.md](researches/nested-attr-vector-search/problem_def.md) |
| **方案说明与观测面板**（构建、查询、参数、实测点、可调方向） | [researches/nested-attr-vector-search/approaches.md](researches/nested-attr-vector-search/approaches.md) |
| 最新实验记录 | [researches/nested-attr-vector-search/2026-11-09-exps/report.md](researches/nested-attr-vector-search/2026-11-09-exps/report.md) |
| 论文索引 | [papers/papers.jsonl](papers/papers.jsonl)、[papers/relations.jsonl](papers/relations.jsonl) |
| 研究规则 | [AGENTS.md](AGENTS.md) |

## 目录

```text
.
├── AGENTS.md                      # 研究约定
├── codes/                         # 每个方案一个目录，build / search 分离
│   ├── NestedAttrVectorRetrieval/ # 本方案：index 构建、search、脚本
│   ├── NodeANN/                   # 基线：每属性一张 HNSW 再归并
│   ├── ACORN/                     # 基线：单张全局 HNSW + 谓词感知遍历
│   ├── shared/                    # dataset.h/.cpp 统一加载器、raw_l2_space.h
│   └── third_party/hnswlib/       # 固定版本 hnswlib
├── datasets/                      # 数据集，按名称分目录（gitignored）
│   └── sift1m/                    # SIFT1M 原始语料 + 各研究问题派生的数据
│       ├── raw/                   # sift_base/learn/query/groundtruth
│       ├── base/base.bin          # 1M × 128 float32
│       └── ccs-attr/              # 本问题的层级属性数据与查询/真值
├── researches/
│   └── nested-attr-vector-search/ # 每日实验记录与数据
├── papers/                        # 论文库：索引、全文与关系
└── paper-production/              # 论文写作工程
```

## 复现

```bash
./codes/NestedAttrVectorRetrieval/build.sh && ./codes/NodeANN/build.sh && ./codes/acorn-hnswlib/build.sh
D=datasets/sift1m/ccs-attr
./codes/NestedAttrVectorRetrieval/build/asi build  --data $D --index $D/index/asi.bin
./codes/NestedAttrVectorRetrieval/build/asi search --data $D --index $D/index/asi.bin --efs 10,40,160
python3 codes/NestedAttrVectorRetrieval/scripts/run_experiments.py --data $D \
  --out researches/nested-attr-vector-search/2026-11-09-exps --methods asi,nodeann,acorn
python3 codes/NestedAttrVectorRetrieval/scripts/plot_pareto.py \
  --measurements researches/nested-attr-vector-search/2026-11-09-exps/data/measurements.jsonl \
  --out researches/nested-attr-vector-search/2026-11-09-exps/figures/pareto.svg
```

所有方案只认一个 `--data` 目录，格式固定：`base.bin`、`edges.tsv`、`objects.txt`、`queries.tsv`、`queries.bin`、`gt_<k>.txt`、`meta.json`。数据集由 `codes/NestedAttrVectorRetrieval/scripts/build_dataset.py` 生成，参数与统计见 `ccs-attr/meta.json`。

## 约定

- 数据集按名称放 `datasets/<名称>/`，不入库；代码只有本方案，放在 `codes/<方案名>/`。
- 研究记录按方向放 `researches/<方向>/`，只允许 `approaches.md`、`problem_def.md` 与 `<YYYY-MM-DD>-exps/`；方案文档只描述当前实现与实测，不写论证与复查。
- 提交源码、文档与小体积 JSON/CSV；不提交数据集、构建产物与索引。
- 结论必须能追到运行记录；未运行的结论标注为推断。
