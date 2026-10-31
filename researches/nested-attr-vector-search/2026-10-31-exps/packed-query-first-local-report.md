# 查询优先与驻留索引复用：本地报告

## 决策与交付

用户将近期目标收窄为“效率超过基线，复用已有资源；增删改先做理论分析”。已落实为[精简协议](packed-query-first-plan.md)和[新服务器提示词](packed-query-first-server-prompt.md)。旧维护/扩规模任务被新协议替代，历史结果不删除；这不代表已远程终止服务器进程，需服务器AI执行新的调度要求。

源码 **d4add37**：`dynamic_triad --static-batch FILE` 一次构建，多遍真正执行查询；新 `run_packed_query.py` 固定A/P-cap/P-aware各3遍。A只建一次，P两个策略共用同一图和资格索引，只建一次。共2build/9pass。没有新增算法、磁盘图恢复、结果缓存或扫描基线。

`--from-static`校验已有输入、源trace、static trace及GT的SHA后复用答案与分组；无需等待旧矩阵全部完成。显式data/trace路径也支持，只生成一次GT。旧未保存且已经释放的图不能恢复，首次建图仍需执行。三遍为resident重复，按臂成组，不称独立构建或cold-start。

## 实际运行和结果

固定hnswlib ca426729609b3221563047ceb269cc1213e0d376，实际重建dynamic_triad成功；仍有已有库warning，无编译错误。12项CLI回归通过：static_batch2、packed5、eligibility2、dynamic_triad3。

新增回归验证：

- A三遍和P在cap/hybrid/exact及ef15/20之间切换后，每遍结果、距离、hops、图调用、扫描调用和有效ef与独立运行一致；这些额外policy/预算仅是本地行为测试，不扩服务器固定ef20矩阵。
- 每遍GT重新读取，query/距离/图计数不累加泄漏；同一session只计费一次构建；P物理指纹不变。
- 混合更新trace在构建前被拒绝；重复输出prefix拒绝，避免覆盖或误做动态实验。

### 真实程序烟测

| 路径 | pass数 | 索引构建数 | query记录 | 复用既有GT |
|---|---:|---:|---:|---|
| 从旧scale静态目录接管 | 9 | 2 | 2304 | 是 |
| 显式data/trace生成一次GT | 9 | 2 | 1620 | 否 |
| 从新query-first目录再次接管 | 9 | 2 | 2304 | 是 |
| GT已就绪、矩阵complete=false接管 | 9 | 2 | 1620 | 是 |

总36pass、8次索引构建、7848条query。非法、重复、state、mask、计时GT距离均为0；更新0、完整repack0。最后一种是本地复制manifest并设置complete=false的**控制夹具**，不是伪称真实远端作业失败。

另将中途接管测试的三臂各3遍，与cdc28f3时代final-diagnostic-static的同ef20、同输入结果配对：每遍180query，result_ids/gt_ids/distance_calls/ann_hops/graph_calls/scan_distance_calls/effective_ef/fanout全部无差异。共1620条配对。新功能改变构建复用和执行调度，不把搜索算法调快或把基线调慢。

上述烟测输入仍是80对象/4D或4096对象/128D合成数据，**不拿烟测wall time做算法加速比**；真实 SIFT 结果见下一节。证据：[data/packed-query-first-local.json](data/packed-query-first-local.json)。


## 真实 SIFT 三臂结果（本轮新增）

固定 M16 / efConstruction200 / ef=20 / k=10 / B4096 / 阈值512 / 纯 NodeANN `scan 0 / policy off`；每单元 **2 次构建 + 9 遍查询**（A 建一次跑 3 遍；P 建一次，cap 与 hybrid 交替跑 3 轮）。GT 与查询集由 `--from-static` 经 SHA 校验复用，不缓存查询答案。256 条 query，`all_nonempty` 组，3 遍合并。

### 100k

| 单元 | A-graph | P-dfs | P-dfs-aware | A macro/pair | aware macro/pair |
|---|---:|---:|---:|---|---|
| l64-random | 6.263 ms | 6.504 (1.038×) | **3.734 (0.596×)** | .99840/.99833 | .99880/.99875 |
| l64-coordinate | 6.168 ms | 6.283 (1.019×) | **3.551 (0.576×)** | .99760/.99749 | .99760/.99749 |
| l2048-random | 6.231 ms | 6.957 (1.117×) | **3.760 (0.604×)** | 1.00000/1.00000 | .99959/.99957 |
| l2048-coordinate | 6.187 ms | 6.744 (1.090×) | **3.687 (0.596×)** | 1.00000/1.00000 | .99959/.99956 |

### 1M

| 单元 | A-graph | P-dfs | P-dfs-aware | A macro/pair | aware macro/pair |
|---|---:|---:|---:|---|---|
| l64-random | 75.99 ms | 71.95 (0.947×) | **37.60 (0.495×)** | .99297/.99285 | .99336/.99325 |
| l64-coordinate | 76.82 ms | 71.54 (0.931×) | **36.87 (0.480×)** | .99258/.99246 | .99297/.99286 |
| l2048-random | 71.81 ms | 67.89 (0.945×) | **51.17 (0.713×)** | .99921/.99919 | .99921/.99919 |
| l2048-coordinate | 72.61 ms | 65.42 (0.901×) | **48.69 (0.671×)** | .99881/.99878 | .99920/.99919 |

### 判定

- **目标达成**：8/8 单元 P-dfs-aware 查询均值低于纯 NodeANN，1.40×–2.08×（1M 四单元 `quality_no_worse`、`faster@0.95`、`faster@0.99`、`pareto` 全为 True）。
- **P-dfs 单独不够**：只合并分块、保持 cap 策略时，100k 反而慢 1.02–1.12×，1M 才 0.90–0.95×。加速来自资格前置 + 小选择集精确扫描这一执行策略，与 `3aaa0ed` 的结论一致；分块合并本身不产生收益。
- **距离调用**：1M l64 272.9k → 39.5k（6.9×），1M l2048 365.8k → 62.3k（5.9×），100k 36.5k → 15.3k（2.4×）。
- **质量不靠降召回**：100k 的 l2048 两单元 aware 略低 0.04pp（macro .99959 对 1.0，732×10 中约 3 对），其余 6 个单元 aware ≥ A；1M 四单元全部高于 A（+0.04 至 +0.06pp）。
- **尾部**：1M l64-random p95 296.3 ms → 79.9 ms，l2048-random 220.5 ms → 137.2 ms；但 100k l2048-random 的 p95 反而 16.5 → 21.6 ms，收益集中在均值与中位数，极少数超大 scope 查询仍由扫描路径支配。

### 成本与边界

- 运行成本：1M 单单元 2 次构建（l64 84.2s / l2048 32.4s）+ 9 遍查询，四单元共 31 分钟；旧协议每单元 33 次独立构建约 64 分钟。
- **驻留重复不是独立构建、不是 cold start**：同进程热缓存与分配器状态，不能与历史轮换协议的数字混池。
- 本轮**未跑维护**，repack 与更新成本未计入；相关理论分析见上文，未测部分不记为 0。
- 1M l64-coordinate 两臂 macro 均低于 0.99（.99258 / .99297），是该单元自身难度，不是本轮引入的退化。

数据：[data/query-first-all.csv](data/query-first-all.csv)（含 `all_queries`、`confirmation_nonempty` 分组），汇总脚本 [experiments/summarize_query_first.py](experiments/summarize_query_first.py)。

## 复现命令

代码目录 `codes/directory-aware-vector-attr-search`：

```bash
bash build_dynamic_triad.sh
python -m unittest discover -s tests -p 'test_static_batch.py' -v
python -m unittest discover -s tests -p 'test_packed.py' -v
python -m unittest discover -s tests -p 'test_eligibility.py' -v
python -m unittest discover -s tests -p 'test_dynamic_triad.py' -v
python scripts/run_packed_query.py \
  --from-static ../../work/packed-local/final-scale-static --name toy \
  --out ../../work/packed-local/query-first-reuse --smoke
python scripts/run_packed_query.py \
  --dataset toy=../../work/eligibility-execution-local/data \
  --trace toy=../../work/eligibility-execution-local/mixed.trace \
  --out ../../work/packed-local/query-first-explicit --smoke
python scripts/run_packed_query.py \
  --from-static ../../work/packed-local/query-first-reuse --name toy \
  --out ../../work/packed-local/query-first-chained --smoke
python scripts/run_packed_query.py \
  --from-static ../../work/packed-local/query-first-partial-source --name toy \
  --out ../../work/packed-local/query-first-partial --smoke
```

真实 SIFT（每个单元 2 构建 + 9 遍）：

```bash
for NAME in sift100k-l64-random sift100k-l64-coordinate \
            sift100k-l2048-random sift100k-l2048-coordinate \
            sift1m-l64-random sift1m-l64-coordinate \
            sift1m-l2048-random sift1m-l2048-coordinate; do
  python scripts/run_packed_query.py \
    --from-static ../../work/packed/static-${NAME}-run1 --name ${NAME} \
    --out ../../work/packed/query-first-${NAME}-run1 --cpu 2
done
python ../researches/nested-attr-vector-search/2026-10-31-exps/experiments/summarize_query_first.py \
  ../../work/packed/query-first-sift*-run1 \
  > ../researches/nested-attr-vector-search/2026-10-31-exps/data/query-first-all.csv
```

partial-source是final-diagnostic-static的manifest副本，complete=false，数据路径/哈希不变，并复制其toy/static/events.trace和oracle.queries.csv；未更改原数据或GT。fixture来源和128D输入生成见前一轮[复现记录](experiments/packed-local-reproduction.md)。真实服务器任务禁止smoke。

四个输出目录manifest保存完整命令、源/输入/二进制SHA，source SHA均与最终交付代码一致。大raw/图二进制留work不提交，紧凑计数和manifest SHA持久化。无需重新生成原数据来复现新功能。

## 维护理论与范围

新plan逐项给出V/T固定宽度位更新、M posting对数维护、B至少Ω(m)、R当前O(D)rank重算、I/D局部HNSW/压实/split及条件摊销。不把未知HNSW代价伪写为最坏O(log b)。本轮允许先完成查询效率判断；更新实现保留，但未新增更新性能结果，不能写“动态已高效”。

下一步只需服务器选一个已有SIFT单元，优先复用GT，运行2build/9pass并交付三臂质量—e2e表。胜负均可交付，不再以maintenance、内存胜出、20%加速或1M作为前置门禁。
