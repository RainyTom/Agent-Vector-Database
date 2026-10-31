# ef15/20固定小预算：复现与执行记录

## 版本与接入

- 任务书：[closure-small-ef-server-prompt.md](../closure-small-ef-server-prompt.md)
- 执行源码固定 `7654f1e79bef6ce3b1aaaffe399b6e17609c7da1`；只新增文件的交付提交 `e0cd9f6` 通过 `git cherry-pick` 接入（本地 `79f8425`）。未整分支merge本地知识库或旧研究历史。
- 保留5fc1be3全部结果；`work/closure/static-7654f1e-run1` 未修改，仅被只读分析。

```bash
git fetch origin review/handoff-20261029
git cherry-pick e0cd9f6
```

## 1. 旧CSV停止条件归因（不运行ANN）

```bash
python3 codes/directory-aware-vector-attr-search/scripts/analyze_closure_termination.py \
  --raw work/closure/static-7654f1e-run1 \
  --out work/closure/termination-7654f1e-run1.json
```

实际输出：`{"files": 60, "all_forced_property_checks_pass": true}`。它校验原CSV SHA，并分层统计 `E<ef`、强制全scope、expanded占比与编译占比，没有重建/搜索/授权。归因结果小副本：[termination-attribution-7654f1e.json](../data/closure-small-ef-run1/termination-attribution-7654f1e.json)。

## 2. 固定小预算重跑

```bash
python3 codes/directory-aware-vector-attr-search/scripts/run_closure_small_ef.py \
  --previous-raw work/closure/static-7654f1e-run1 \
  --out work/closure/small-ef-15-20-run1 \
  --cpu 2
```

- 输出目录首次运行前不存在；未传 `--smoke`。
- 驱动从旧manifest取原数据与源trace并逐文件校验SHA；复用原200条字面Q与初始快照，不执行更新。
- 校验11份冻结执行/构建源文件的Git blob等于7654f1e；不一致即停止。
- 重建原两C++程序（构建日志在 `work/closure/small-ef-15-20-run1/build-*.log`），先独立生成精确GT，再跑 A/U/R/H × ef15/20 × 3次串行轮换。
- 实际结果：48次计时、9600条query记录、`selection_performed=false`、`dynamic_admitted=false`、耗时371.10秒。
- 全部48次 `gt_mode=precomputed`；非法/重复/状态错误/计时GT距离合计均为0；候选池指纹在U/R/H间一致。

## 3. 只读二次聚合

```bash
python3 researches/nested-attr-vector-search/2026-10-30-exps/experiments/package_closure_small_ef.py \
  --raw work/closure/small-ef-15-20-run1 \
  --out researches/nested-attr-vector-search/2026-10-30-exps/data/closure-small-ef-run1
```

该脚本先断言 `complete=true`、`smoke_only=false`、`k=10`、`efs=[15,20]`、`arms=[A,U,R,H]`、48次运行、无选参、不授权dynamic，再生成表一/表二聚合。它不执行搜索、不选参、不改阈值。

## 4. 原始数据位置

`work/closure/small-ef-15-20-run1/`（3.3MB）保留每query CSV、`.summary.json`、`.updates.csv`、日志与构建输出；随交付提交的只有manifest/runs/points/comparisons/termination归因与两份聚合。原始向量、索引二进制与work目录不上传。

## 5. 已知限制

- 两数据集各仍只有200条旧query、一个初始快照；SIFT历史确认非空仅16个qidx。
- 三次重复是串行轮换，未做隔离整机噪声，不能宣称统计显著性。
- 编译占比是嵌套计时，不是硬件profile，也不能当作“扣掉编译后的性能”。
- 本轮没有真实更新、维护包络、Dynamic、扩大N或新质量门；这些成本仍记未测，不是0。
