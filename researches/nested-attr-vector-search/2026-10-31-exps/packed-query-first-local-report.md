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

所有本地输入仍是80对象/4D或4096对象/128D合成数据；**没有真实SIFT/WIKI性能结论**。不拿此次烟测wall time做算法加速比。证据：[data/packed-query-first-local.json](data/packed-query-first-local.json)。

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

partial-source是final-diagnostic-static的manifest副本，complete=false，数据路径/哈希不变，并复制其toy/static/events.trace和oracle.queries.csv；未更改原数据或GT。fixture来源和128D输入生成见前一轮[复现记录](experiments/packed-local-reproduction.md)。真实服务器任务禁止smoke。

四个输出目录manifest保存完整命令、源/输入/二进制SHA，source SHA均与最终交付代码一致。大raw/图二进制留work不提交，紧凑计数和manifest SHA持久化。无需重新生成原数据来复现新功能。

## 维护理论与范围

新plan逐项给出V/T固定宽度位更新、M posting对数维护、B至少Ω(m)、R当前O(D)rank重算、I/D局部HNSW/压实/split及条件摊销。不把未知HNSW代价伪写为最坏O(log b)。本轮允许先完成查询效率判断；更新实现保留，但未新增更新性能结果，不能写“动态已高效”。

下一步只需服务器选一个已有SIFT单元，优先复用GT，运行2build/9pass并交付三臂质量—e2e表。胜负均可交付，不再以maintenance、内存胜出、20%加速或1M作为前置门禁。
