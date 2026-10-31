# 资格感知执行与NodeANN审计：构建、执行与复现

## 版本

- 源码提交：`4fc4b00`。本分支以 `2f8c4e0` 为基点，按顺序 cherry-pick `3d75caa`（实现＋脚本＋回归）、`fcbcd36`（纯NodeANN主基线修订）、`4d6cbed`、`4fc4b00`（研究端记录）。未整分支merge本地知识库或旧实验CSV。
- doc冲突的处理：`index.md` 保留本分支的小ef条目并追加研究端的新条目；`codes/.../README.md` 取新增段落。没有删除任何旧结论。
- hnswlib `ca426729609b3221563047ceb269cc1213e0d376`（构建脚本校验）。

## 1. 构建与语义回归

```bash
bash codes/directory-aware-vector-attr-search/build_closure.sh
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_eligibility.py
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_dynamic_triad.py
```

实际结果：两程序构建成功（仅vendored hnswlib的既有警告）；`test_eligibility` 2项、`test_dynamic_triad` 3项通过。core/NodeANN/资格实现与调度未修改。

## 2. 静态矩阵

```bash
python codes/directory-aware-vector-attr-search/scripts/run_eligibility.py \
  --previous-raw work/closure/static-7654f1e-run1 \
  --out work/eligibility/pure-nodeann-static-run1 --cpu 2
```

- 数据路径与原trace取自 `work/closure/static-7654f1e-run1/manifest.json`，逐文件SHA校验；未重新生成数据。
- CPU2在可用亲和集合内（0–15）；串行、单worker、`OMP/OPENBLAS/MKL/NUMEXPR=1`。
- 每数据集每重复17点：A-graph、U-off/U-cap/U-aware、H-off/H-cap/H-aware（×ef15/20）＋Exact(ef20)＋A-hybrid-reference(ef15/20)；3次旋转 → 51点/数据集，共 **102** 次计时，2次独立GT生成。
- 实际耗时764.35秒，`complete=true`，`measured_runs=102`。

## 3. 有限维护

```bash
python codes/directory-aware-vector-attr-search/scripts/run_eligibility.py \
  --previous-raw work/closure/static-7654f1e-run1 \
  --out work/eligibility/pure-nodeann-maintenance-run1 --stage maintenance --cpu 2
```

A-graph/U-aware/H-aware × ef20 × 3次 × 两条轨迹（原mixed前缀、合成包络）× 2数据集 = **36** 次计时，实际301.17秒。原前缀保留真实V/T/M/B/R/D/I；包络额外触发V/T、R重挂恢复、B批移动＋收费M恢复、D到1/3墓碑压实、I恢复。压实是否真的发生以 `updates.csv` 的 `compaction_count`/`compactions` 为准（包络轨迹实际观测：WIKI A-graph 285次、SIFT A-graph 50次、closure臂各1次；原mixed前缀仅WIKI A-graph 1次、其余0次）。

## 4. 只读打包与复核

```bash
python3 researches/nested-attr-vector-search/2026-10-31-exps/experiments/package_eligibility.py \
  --raw work/eligibility/pure-nodeann-static-run1 \
  --out researches/nested-attr-vector-search/2026-10-31-exps/data/eligibility-static-run1

python3 researches/nested-attr-vector-search/2026-10-31-exps/experiments/package_eligibility_maintenance.py \
  --raw work/eligibility/pure-nodeann-maintenance-run1 \
  --out researches/nested-attr-vector-search/2026-10-31-exps/data/eligibility-maintenance-run1

production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/2026-10-31-exps/experiments/plot_eligibility.py \
  --source researches/nested-attr-vector-search/2026-10-31-exps/data/eligibility-static-run1/static-summary.json \
  --out researches/nested-attr-vector-search/2026-10-31-exps/data/eligibility-static-run1/figures
```

打包器重新读取每query CSV，逐条复核Exact/hybrid精确路径的ID与距离、E=0免搜索、cap不偷用精确扫描、A-graph零扫描与`graph_calls==fanout`、距离分账，并重算artifact哈希。实际输出 `problems: 0`（static 20,400行、maintenance 6,768行）。

## 5. 产物与原始位置

入库：manifest/points/comparisons、static-summary、bands、nodeann-audit、semantic-check、raw-inventory、维护聚合、结果图。
不入库：每query CSV、summary、日志、构建输出与 `runs.json`（3.6MB，含逐run哈希）。它们位于 `work/eligibility/pure-nodeann-static-run1/`、`work/eligibility/pure-nodeann-maintenance-run1/`；SHA见 raw-inventory。

## 6. 限制

- 旧200条Q、一个初始快照；SIFT全部非空只有48个qidx（历史确认组16个），不是新的独立query集。
- 计时串行单核，未做隔离整机噪声或统计显著性；均值为三次重复各自均值之平均。
- 编译/资格时间为嵌套计时，不是硬件profile，也不能当作“扣掉后”的算法预测。
- 维护包络只覆盖已触发事件，不代表稳态、并发或崩溃恢复；未运行Dynamic与大N扩展。
