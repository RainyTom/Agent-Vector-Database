# 本地分块索引复现记录

工作目录：仓库 `codes/directory-aware-vector-attr-search`。源码提交cdc28f3；hnswlib ca426729609b3221563047ceb269cc1213e0d376。原始失败候选不重跑，本次测试的是新P臂。

## 最终实际运行

```bash
bash build_dynamic_triad.sh
bash build_closure.sh
python -m unittest discover -s tests -p 'test_packed.py' -v
python -m unittest discover -s tests -p 'test_eligibility.py' -v
python -m unittest discover -s tests -p 'test_dynamic_triad.py' -v
python scripts/run_packed.py \
  --previous-raw ../../work/eligibility-execution-local/original-static \
  --out ../../work/packed-local/final-diagnostic-static --suite diagnostic --stage static --smoke
python scripts/run_packed.py \
  --previous-raw ../../work/eligibility-execution-local/original-static \
  --out ../../work/packed-local/final-diagnostic-maintenance --suite diagnostic --stage maintenance --smoke
python scripts/prepare_packed_sift.py \
  --base-fvecs ../../work/packed-local/base.fvecs \
  --query-fvecs ../../work/packed-local/query.fvecs \
  --n 4096 --leaves 64 --queries 256 --query-offset 32 --ownership random \
  --out ../../work/packed-local/final-data
python scripts/run_packed.py \
  --dataset toy=../../work/packed-local/final-data \
  --trace toy=../../work/packed-local/final-data/mixed.trace \
  --out ../../work/packed-local/final-scale-static --suite scale --stage static --smoke
python scripts/run_packed.py \
  --dataset toy=../../work/packed-local/final-data \
  --trace toy=../../work/packed-local/final-data/mixed.trace \
  --out ../../work/packed-local/final-scale-maintenance --suite scale --stage maintenance --smoke
```

生成器命令在最终四矩阵之前执行。上述命令按依赖关系列出，不表示当时全部在同一个shell串行输入。最终C++重建后四矩阵全部重跑。`--smoke`跳过自动构建并允许不足正式查询数，**绝不用于服务器真实实验**。

## 输入可重建说明

80对象fixture由 `tests/test_closure.py::fixture` 创建；original-static是此前同fixture的完整静态manifest，非真实旧数据。

128D输入为numpy `default_rng(20261031)` 正态FP32，依次生成4096条base和288条query，每行写little-endian int32维度128，再写128个little-endian FP32。它是输入解析/回放烟测，不能改称SIFT。对同文件运行coordinate控制，逐目录对象数与random完全一致，向量bit不变。生成器最终版另在 `final-data` 运行；源SHA已写入preparation。

若换机器没有历史toy manifest，可以用 `--dataset toy=DIR --trace toy=TRACE --smoke` 测相同路径；报告查询分组规则变更，不声称与本机同一个manifest。正式服务器输入和命令只以[服务器任务](../packed-server-prompt.md)为准。

## 纯A行为不变性

实际运行 `dynamic_triad --data ../../work/eligibility-execution-local/data --trace ../../work/eligibility-execution-local/mixed.trace --arm A --ef 15/20 --scan-threshold 0 --eligibility-policy off`，输出 `final-baseline-ef15/20`。与旧 `off-current-A-0-15/20` query CSV按行比较result_ids/gt_ids/距离/graph_calls/fanout/扫描组/扫描距离。360条零差异。GT为inline，故这两次只查行为，不算正式计时性能或150次预计算GT矩阵。

完整命令、输入/二进制/source SHA、独立GT及log见四个final manifest；raw在work不提交。小证据[data/packed-local-check.json](../data/packed-local-check.json)记录其SHA和计数。失败的只读字段KeyError已在本地报告披露；无ANN失败结果被删改。
