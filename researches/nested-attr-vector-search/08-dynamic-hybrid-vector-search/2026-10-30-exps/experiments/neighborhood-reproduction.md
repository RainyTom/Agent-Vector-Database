# 谓词可寻址邻域：实际构建与复现

## 版本与输入

研究合同为 [`2456849`](https://github.com/RainyTom/Agent-Vector-Database/blob/2456849/researches/nested-attr-vector-search/2026-10-30-exps/next-neighborhood-plan.md)。本分支以 `0837918` 为实现前的基点，没有自动合并研究分支、WQR分支或论文索引。`manifest.json.source_sha` 是准备实验时的基点，不是声称未提交的新源码已经包含在该提交内；本次交付提交保存实际源码。

- 机器：AMD Ryzen 7 5800X，8核/16逻辑CPU，Microsoft/WSL2虚拟化；没有绑核或频率隔离，不能据三次重复声称统计显著性。
- 编译器：`g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0`。
- 两个在线程序均为C++17、`-O3 -march=native -Wall -Wextra`、同一hnswlib revision `ca426729609b3221563047ceb269cc1213e0d376`、公共 `RawL2Space`。没有新量化、FAISS索引或专用距离实现。
- 图：M=16、efConstruction=200、seed=100；B/C按同一初始活对象ID顺序插入。每个中心的二跳集合去重、排除自身；原始一跳边保留。
- Python只做轨迹选择、进程调度、离线核验和绘图；所有在线搜索、资格求值和精确GT均在C++。
- `OMP_NUM_THREADS/OPENBLAS_NUM_THREADS/MKL_NUM_THREADS/NUMEXPR_NUM_THREADS=1`；测量进程串行轮换，没有将并发结果与串行结果比较。

原始向量位于以前的ignored `work/` 数据目录，未上传。本轮没有重新生成属性或重新绑定层级。全部输入文件SHA、原始路径、元数据和源轨迹SHA保存在 [manifest](../data/neighborhood-run1/manifest.json)，200条选择记录及字面Q分别保存在其 `sift/`、`wiki/` 子目录。拿到相同原始文件后，可按SHA核对；GitHub链接本身不提供这些大向量文件。

## 实际执行顺序

以下命令从仓库根目录执行，输出目录在首次执行前不存在。`prepare`、`analysis`、`audit`、`figures` 均拒绝静默覆盖，重复复现须改用新输出目录；不要覆盖本轮历史结果。

```bash
bash codes/directory-aware-vector-attr-search/build_neighborhood.sh
codes/directory-aware-vector-attr-search/build/neighborhood_research --self-check
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh

P=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps
PY=production/nested-attr-vector-search/work/venv/bin/python
OUT=$P/data/neighborhood-run1

$PY $P/experiments/neighborhood_experiment.py prepare --out "$OUT" \
  --binary codes/directory-aware-vector-attr-search/build/neighborhood_research \
  --baseline codes/directory-aware-vector-attr-search/build/dynamic_triad
$PY $P/experiments/neighborhood_experiment.py run-e1 --out "$OUT"
$PY $P/experiments/neighborhood_experiment.py run --out "$OUT"
$PY $P/experiments/neighborhood_experiment.py analyze --out "$OUT"
$PY $P/experiments/neighborhood_audit.py --out "$OUT"
$PY $P/experiments/neighborhood_figures.py --out "$OUT"
```

构建成功；编译输出含vendored hnswlib的unused-parameter、unsigned comparison和unused-function警告，没有修改依赖或通过关闭检查掩盖它们。实际smoke输出：

```json
{"eligibility_checks":6773,"candidate_centers_checked":104,"B_C_result_path_distance_identical":true,"errors":0}
```

它覆盖分叉树、内部owner、AND/OR、四种tag语义、空tag、负值、int64边界、字面单侧−1、稀疏活ID、跨word/不满page，以及B/C相同候选与搜索路径/距离数；不是WQR生命周期审计或完整动态实现的替代。

## 计时与读数

- E1在B搜索轨迹的前8个实际扩展中心上重放；每条Q分别冷启动B/C资格缓存，中心之间共享本次Q缓存，交替B/C执行顺序。生成中心轨迹的完整搜索不属于候选取得计时。
- E1 `acquire_ns` 包含同样的候选结果拷贝开销；核对发生在计时外。输出独立 `.B.csv`/`.C.csv`，不是ANN查询延迟。
- E2仅初始快照：源轨迹的全部更新被有意排除，抽出的Q保留字面语义；这不是完整动态replay。
- 每臂只尝试ef=64/256/1024三个校准预算，依非空macro和pair均≥0.999、合法性零错误挑选最低均值预算。三臂在两个数据集均选ef=64，之后冻结，确认集串行轮换重复3次。
- 两个程序都在ANN计时结束后计算独立GT；构建和前序GT距离不混入本次ANN距离数。GT不参与在线路由/停止，跨臂GT ID集合亦核对。
- C `qualification_us` 只计二跳取得中的首次资格page构造；B为二跳候选过滤批次。它们是 `neighborhood_us` 的子集，不能再相加。一跳资格开销在剩余search时间内；没有把该列冒充完整目录解析延迟。
- B/C整次查询结果、距离数、一跳访问、扩展数、visited检查及合法二跳取得数在1200对记录上逐项相同。B读取的二跳邻接ID与C读取的页容器/位图字数是不同逻辑单位，不是DRAM流量。
- `confirmation-populations.csv` 含每次重复的mean/p50/p95/p99；`population-aggregates` 汇总三个重复各自的统计量，未将重复查询假装成独立新样本。
- `graph_bytes`/NodeANN `hnsw_serialized_bytes` 是hnswlib序列化尺寸，不是完整resident内存；邻域页为实际vector容量计数。`peak_rss_bytes` 是整个进程的构建/查询高水位，含构建时输入与图同时存在的阶段，不是稳态RSS或单独重建峰值。

## 已保留的失败与未放行项

1. 正确性smoke、E1集合核对、36次E2运行没有记录状态/资格错误；不编造“先失败再修复”的运行证据。
2. C的候选取得和相对B查询收益为正，但同质量下慢于真正NodeANN；没有转调更多预算、减少召回、改变基础图或换算法追正结果。
3. 初版analysis生成的主题变量SVG在独立raster预览中不可读，保留原文件作为渲染失败记录，不作正式图。随后去掉该SVG生成路径，新增只读实测数据的可移植PNG渲染器，正式两图均已实际打开核验。该修正不改变任何性能记录或原始数值。
4. E3未放行，见 [decision](../data/neighborhood-run1/audit/e3-decision.json)。原型没有实现或测量动态二跳维护；不能声称已经支持图插删、R后版本一致性或低写放大。

主报告：[neighborhood-report.md](../neighborhood-report.md)。源码、逐query、逐配置、原始日志、SHA和全部预算均随本次提交保存，原向量/大索引/二进制不提交。
