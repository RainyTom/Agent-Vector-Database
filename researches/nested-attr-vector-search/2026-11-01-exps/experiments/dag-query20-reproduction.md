# DAG query20 实际复现记录

工作目录为仓库根。使用已准备好的 `work/dag-scope/workload-run1` 与 `work/packed/sift-fvecs-run1`，不重新生成标签或向量。输入 SHA、环境和源文件 SHA 见 [验证 JSON](../data/dag-scope-query20-verification.json)。

## 构建及行为检查：已执行

```bash
bash codes/directory-aware-vector-attr-search/build_dag_scope.sh
python3 -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p 'test_dag_scope*.py' -v
```

构建使用 g++ `-O2 -std=c++17`，ANN 链接 pthread；hnswlib 固定 revision `ca426729609b3221563047ceb269cc1213e0d376`。9 项检查通过。查询距离统一为 `RawL2Space` 调用的原始 FP32 squared-L2。

## 两个驻留查询配置：已串行执行

```bash
codes/directory-aware-vector-attr-search/build/dag_scope_ann \
  --data work/dag-scope/workload-run1 \
  --vectors work/packed/sift-fvecs-run1/sift_base.fvecs \
  --queries work/packed/sift-fvecs-run1/sift_query.fvecs \
  --out researches/nested-attr-vector-search/2026-11-01-exps/data/dag-scope-query20-cut1 \
  --partition top --partition-depth 1 --ef 20 --k 10 --tau 512 \
  --max-queries 1234 --sample-queries 20 --seed 20261101 --repeats 3

codes/directory-aware-vector-attr-search/build/dag_scope_ann \
  --data work/dag-scope/workload-run1 \
  --vectors work/packed/sift-fvecs-run1/sift_base.fvecs \
  --queries work/packed/sift-fvecs-run1/sift_query.fvecs \
  --out researches/nested-attr-vector-search/2026-11-01-exps/data/dag-scope-query20-cut3 \
  --partition top --partition-depth 3 --ef 20 --k 10 --tau 512 \
  --max-queries 1234 --sample-queries 20 --seed 20261101 --repeats 3
```

每个进程构建一张全局图及一组分区图，然后四策略轮换执行。`--repeats 3` 不重新构图、不缓存查询答案。20 条 scope 行从全部 1,234 行抽取；scope 行与向量 query 行相同，缺失对应向量时报错，不进行取模替换。输出 `ann.json` 和 `ann_queries.csv`，同目录中有实际 argv。

同批 scope 行（0-based）：1114,129,854,197,740,54,1189,1210,1196,1077,12,643,759,175,24,257,161,334,542,246。不同查询中 exact15 / graph3 / empty2。两个进程串行 wall time 131.18 s，四个构建阶段合计 130.49 s。查询单线程、未 pin CPU；三次是固定几何图上的驻留重复。

CSV 使用外层 `e2e_us`；不能用 scope/search 两项相加代替。GT 与独立范围/资格核对在计时外；所有空查询计时保留，Recall 分母排除空查询。两种 Recall 口径、返回不足和构建成本单独记录。

## 单标签维护路径 smoke：已执行

```bash
codes/directory-aware-vector-attr-search/build/dag_scope_probe \
  --data work/dag-scope/workload-run1 \
  --out researches/nested-attr-vector-search/2026-11-01-exps/data/dag-scope-maintenance-check32 \
  --relabels 32 --verify-batch 8 --budget-mb 64
```

32 次随机重标注，5 个初始/批末检查点，28,935 次成员集合与同数基数核对，差异 0。`bitmap_counter_update_kernel_us` 明确排除 posting、标签赋值、核验、几何图维护；不是完整更新延迟。该 smoke 不涉及扩大数据、完整维护矩阵或重分区。

## SVG 导出：已执行

```bash
/home/daihua/envs/cpp-tools/bin/python \
  researches/nested-attr-vector-search/2026-11-01-exps/experiments/outline_svg_labels.py \
  --font work/svg-fonts/NotoSansCJKsc-Regular.otf \
  --bold-font work/svg-fonts/NotoSansCJKsc-Bold.otf \
  --out-dir researches/nested-attr-vector-search/2026-11-01-exps/figures \
  --report researches/nested-attr-vector-search/2026-11-01-exps/data/dag-attribute-figure.json \
  researches/nested-attr-vector-search/2026-11-01-exps/figures/dag-attribute-scope.svg
```

以 CairoSVG 白底栅格化目视检查，23 组路径标签完整显示。原 SVG 背景透明；没有原生 Tern 窗口验证。
