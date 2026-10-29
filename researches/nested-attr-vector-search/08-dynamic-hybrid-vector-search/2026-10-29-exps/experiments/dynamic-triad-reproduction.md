# 动态三臂：已执行命令与口径

日期：2026-10-29。仓库根目录执行。源代码：`codes/directory-aware-vector-attr-search/{src/dynamic_triad.cpp,scripts/generate_dynamic_triad.py,scripts/run_dynamic_triad.py}`。规则见 [协议](../dynamic-triad-protocol.md)。

## 环境和构建

- AMD Ryzen 7 5800X，8核/16逻辑CPU，WSL2；g++ Ubuntu15.2.0，`-O3 -march=native -std=c++17 -Wall -Wextra`。
- hnswlib commit `ca426729609b3221563047ceb269cc1213e0d376`；M16/efConstruction200/seed100；efSearch256。
- 每个建图/查询/更新进程均单线程；每个 driver `--jobs 2`，不同 driver 窗口有重叠，最多4个 benchmark进程。**不是隔离串行测量**。3次重复只量化该调度下的波动，不消除共享缓存/内存带宽影响。
- 普通 `python` 环境没有 `h5py`，两次初始生成都以 `ModuleNotFoundError` 退出；改用仓库已有 venv 后生成成功。未重复安装依赖。
- 构建的警告来自 pinned hnswlib 的 unused parameter / unsigned comparison，未修改第三方头或屏蔽警告。

```bash
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_dynamic_triad.py -v
```

## 因子筛查（N=2000，1次重复）

```bash
PY=production/nested-attr-vector-search/work/venv/bin/python
GEN=codes/directory-aware-vector-attr-search/scripts/generate_dynamic_triad.py
RUN=codes/directory-aware-vector-attr-search/scripts/run_dynamic_triad.py
EXP=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps
$PY $GEN --suite screening --smoke --reserve 200 \
  --work-root $EXP/work/dynamic_triad/screening --tracked-root $EXP/data/dynamic_triad/screening
$PY $RUN --manifest $EXP/data/dynamic_triad/screening/manifest-screening-n2000-s20261029.json \
  --repeats 1 --jobs 2 --tracked-root $EXP/data/dynamic_triad/screening \
  --work-root $EXP/work/dynamic_triad/screening/runs
```

42个配置×3臂=126个运行。全部 macro/pair recall=1，invalid/duplicate/state violations=0。该规模大多数 A 节点没有图；不能由这些更新微秒数外推大局部 HNSW。B/R结构操作仅3/5个事件，p95/p99只是小样本描述，不解释成稳健尾延迟。

## 主覆盖率 × Q/U 曲线（N=12000，3次重复）

```bash
$PY $GEN --suite crossover --n-live 12000 --reserve 1000 \
  --work-root $EXP/work/dynamic_triad/crossover --tracked-root $EXP/data/dynamic_triad/crossover
$PY $RUN --manifest $EXP/data/dynamic_triad/crossover/manifest-crossover-n12000-s20261029.json \
  --repeats 3 --jobs 2 --tracked-root $EXP/data/dynamic_triad/crossover \
  --work-root $EXP/work/dynamic_triad/crossover/runs
```

26个配置（零更新Q-only按数据集去重），234个运行已完成且全部过质量门。各非零覆盖率0.1/0.2/0.5/0.8、Q/U0.25/1/4精确达到，无query cap/floor。mixed 的V/T/M/D/I提交数相等；D/I组成一个插删unit，unit比例为标量/标签/移动/插删各25%。所有臂使用相同文件及哈希。

另已运行 `--select '*mixed-r0p2-qu0p25' --repeats 1 --jobs 2`，输出 `data/dynamic_triad/pilot/` 与 `work/dynamic_triad/pilot/runs/`。6个pilot运行均过质量门；不并入3重复主结果。

第一次主driver在命令3600秒上限被中止；`/usr/bin/ps -C dynamic_triad -o pid,ppid,etime,args` 确认无残留执行器后，用上面同一driver命令追加 `--resume`，并取消外层命令deadline。226运行通过原始文件哈希和逐行复核后复用，8个未完成运行重跑，最终234个完成；新进程中每条查询仍重新计算GT。统一binary SHA256：`1b1fa3d9529e3da9771be3809c20dec207fa4f7582906998c07090cecc6a3f73`。

## 单 ITEM 移动的低Q/U补充（N=12000，3次重复）

目的：隔离“B/C属性移动不改全局图”与“A局部图插入/墓碑/阈值维护”，不让插入reserve新向量的全局建图成本掩盖该差别。此补充在读取主曲线完成结果前登记。

```bash
$PY $GEN --suite move-crossover --n-live 12000 --reserve 1000 --min-queries 0 \
  --work-root $EXP/work/dynamic_triad/move-crossover --tracked-root $EXP/data/dynamic_triad/move-crossover
$PY $RUN --manifest $EXP/data/dynamic_triad/move-crossover/manifest-move-crossover-n12000-s20261029.json \
  --repeats 3 --jobs 2 --tracked-root $EXP/data/dynamic_triad/move-crossover \
  --work-root $EXP/work/dynamic_triad/move-crossover/runs
```

6配置×3臂×3重复=54运行，全部过质量门；覆盖率0.5，Q/U0.01/0.1/1，各6000个移动、60/600/6000查询，零floor/cap。Q/U0.1与1的成本排序在每个重复中翻转，见 `crossing-audit.json`。不以线性预测代替真实事件重放。

## 派生分析与图

```bash
python $EXP/scripts/analyze_dynamic_triad.py $EXP/data/dynamic_triad/screening/results.json \
  --out $EXP/data/dynamic_triad/screening/analysis
python $EXP/scripts/plot_dynamic_triad.py $EXP/data/dynamic_triad/screening/results.json \
  --out $EXP/figures/dynamic_triad
# crossover / move-crossover 的 results.json 使用同一分析/绘图入口。
```

`results.json/csv`、config/commands/manifest 保存小体积结果。逐查询/逐更新原始CSV、stdout/stderr、输入向量与trace在忽略的 `work/` 保留，不提交大向量/索引；resume要求binary/config/输入/trace/raw哈希匹配并重新审计原始行。

`compaction_total_us/count` 是 runner 的**同步局部图维护总量**，包含死比触发重建、扫描↔图阈值转换及释放；不能单独宣称全是墓碑压实时间。初始build另列。HNSW `indexFileSize()` 是该格式序列化字节，metadata为显式逻辑估算，RSS包含输入向量/GT等；没有测物理设备I/O或后台compaction占用。
