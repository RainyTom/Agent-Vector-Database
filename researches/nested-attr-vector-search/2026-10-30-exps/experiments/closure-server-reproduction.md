# 7654f1e服务器实测：构建、输入与复现

## 版本与边界

- 权威任务：[closure-server-plan.md](../closure-server-plan.md)，commit `7654f1e79bef6ce3b1aaaffe399b6e17609c7da1`。
- 执行分支基点为 `502753c6c903be9f8ce34ddd9d99797cc78f1c74`。仅导入任务列出的closure源码、脚本、回归及本地证据，不整分支合并旧CSV、论文索引或研究历史。
- [preflight.json](../data/closure-server-run1/preflight.json) 逐文件记录源码Git blob，与7654f1e一致；原NodeANN和RawL2Space未修改。全部保留输入及原trace的SHA与502753c manifest逐一匹配。
- 固定hnswlib `ca426729609b3221563047ceb269cc1213e0d376`。已有正确依赖，未重新下载或替换。两个构建脚本均实际成功；告警来自未修改的vendored hnswlib。
- C++完成图/候选池、见证、当前层级编译、搜索、全部更新及GT；Python只调度、检查和离线打包。没有换候选池、停止条件、阈值、N、scope、入口或数据属性分布。

## 已执行环境检查

```bash
bash codes/directory-aware-vector-attr-search/build_closure.sh
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python3 -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_closure.py -v
```

3项回归通过（1.485秒）：实际核心语义；六臂/NodeANN共用GT和坏GT拒绝；完整smoke调度及静态门不能授权真实dynamic。合成fixture只证明该环境中的功能路径，不作为WIKI/SIFT效率证据。未在正式实验使用 `--smoke`。

## 实际static调用

从仓库根目录运行。以下相对路径形式与实际传入的绝对路径等价；完整解析路径和SHA保存在preflight和调度manifest。输出目录首次调用前不存在。

```bash
WIKI_DATA=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/work/dynamic_triad/wiki-topology/wiki-topology-s20261029-q456-f7046c642b34/root-3626/native
WIKI_TRACE=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/work/dynamic_triad/wiki-topology/wiki-topology-s20261029-q456-f7046c642b34/root-3626/traces/qu1.txt
SIFT_DATA=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/work/dynamic_triad/crossover/datasets/sift-n12000-r1000-q64-d3-s20261029
SIFT_TRACE=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/work/dynamic_triad/crossover/traces/n12000-s20261029-1d584b6d2829/sift-d3-uniform-mixed-r0p1-qu1.txt
python3 codes/directory-aware-vector-attr-search/scripts/run_closure.py \
  --dataset "wiki=$WIKI_DATA" --trace "wiki=$WIKI_TRACE" \
  --dataset "sift=$SIFT_DATA" --trace "sift=$SIFT_TRACE" \
  --out work/closure/static-7654f1e-run1 --cpu 2
```

未传入实验参数覆盖：max_queries=200，ef=64/256/1024，repeats=3，k=10，pool=64，NodeANN小节点扫描阈值512，质量门nonempty macro和pair均≥0.999。每数据集18次校准＋18次确认，另有两次独立GT和一次离线机制审计。

CPU2在实际可用affinity中，静态程序串行轮换，子进程继承绑核和单线程环境；开始前loadavg为0.0718/0.0576/0.0088，详见preflight。没有并跑其他ANN任务、绑核外性能对照或隔离整机噪声的主张。

WIKI为11869对象/1024维的原始完整子树，SIFT为12000初始live＋1000 reserve/128维的已有层级；不是全量WIKI或SIFT1M。静态按旧轨迹前200条字面Q和初始属性快照执行，不消费源更新。相同qidx不跨校准/确认，不是新的独立query集。

## 输出、计时与解释

- `work/closure/static-7654f1e-run1/<name>/result.json` 是唯一自动dynamic放行来源；本轮是否实际执行dynamic与维护包络，见最终服务器报告及保存的stage decision。本文不给未放行数据集运行dynamic的授权。
- 真实NodeANN为原 `dynamic_triad --arm A --scan-threshold 512`。U/G/R/H/H0共享同一64候选池、次序骨架、距离核和停止规则。R是同池between消融，不是完整RNSG复现。
- 每臂消费同一C++预计算GT tape；计时回放的GT向量读取/距离计算为0。GT输入不参与选边、入口或停止，校验和输出IO不属于查询内核时间。
- `compile_us` 包含当前层级/资格相关邻接编译，是e2e的一部分；`dir_us`为范围解析。统计原样保留，不把编译成本排除后宣称加速。
- 只比较非空均值对均值；空GT单列。分位数使用原脚本nearest-rank定义，重复汇总取每次重复各自统计量的平均。新图expanded不与NodeANN跨层ann_hops当成同一单位。
- 同ef校准对照与冻结ef确认对照分别保存：前者隔离邻接规则，后者比较相同质量下各自选定计划；不同ef的expanded差不能全归因层级机制。
- `index_bytes`是容量估计，H不含借用Dataset和暂态workspace；NodeANN是其既有元数据估计＋序列化尺寸。输入向量、HNSW payload副本、验证元数据、候选池/见证/token均有成本；RSS包含整个进程与构建高水位，不是稳态内存或可相减的结构字节。
- 内存不设胜出门。没有运行的真实维护/插删成本不能填0或从E0外推；R的拓扑cut/link也不等于整个查询/更新O(1)。

## 小结果打包

提供的调度器不变；另加的离线打包器只读结果，不执行搜索或授权阶段：

```bash
python3 researches/nested-attr-vector-search/2026-10-30-exps/experiments/package-closure-results.py \
  --raw work/closure/static-7654f1e-run1 \
  --out researches/nested-attr-vector-search/2026-10-30-exps/data/closure-server-run1/static
```

打包保留manifest/runs/result/selection、字面静态trace、全部配置（含未达质量点）、非空及预设分组、实际expanded/编译等工作、采样邻接差异及原始文件路径/SHA；不复制大raw CSV。所有输出要求新目录，复现请换新的run目录，不覆盖本轮历史。

原始向量、候选池/图二进制和work目录不上传。GitHub上的小JSON/CSV不能代替这些原始输入；复现者需先取得与manifest一致的文件。

## 完成状态与实际绘图

static完整结束，共72次计时、4次GT生成、2次离线审计，原脚本报告WIKI/SIFT均 `dynamic_admitted=false`。因此本轮没有执行真实dynamic或合成维护包络命令；已有实现和E0通过不等于维护成本已实测。完整门控及qidx覆盖分别保存在 `static/stage-decision.json` 和 `dynamic-stage.json`。

实际正式绘图命令（仅读取已完成的校准记录，未重跑ANN）：

```bash
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/2026-10-30-exps/experiments/plot-closure-results.py \
  --raw work/closure/static-7654f1e-run1 \
  --package researches/nested-attr-vector-search/2026-10-30-exps/data/closure-server-run1/static \
  --out researches/nested-attr-vector-search/2026-10-30-exps/data/closure-server-run1/figures-v2
```

首版figures的预算标注/横轴刻度重叠，保留旧图；修正的是绘图，不是算法或性能点。正式图使用figures-v2并实际打开检查。逐漏召回诊断按 `recall<1` 逐CSV行提取，key为(dataset,phase,arm,ef,event)，只合并GT/结果相同的重复并保留repeats与raw_paths；故85条分组不是85个独立新query。

