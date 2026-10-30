# 第2轮联合信息诊断：实际命令和保留失败

接续[首轮环境/数据准备](reproduction.md)。无新依赖安装、无新ANN构建，C++公共distance矩阵复用；不把离线Python时间与C++ANN比较。

## 事前冻结与smoke

运行前保存 `../joint-information-protocol.md`。其SHA写入每run provenance；本轮未事后改变阈值、属性、r/g/order或输入。

实际执行：

```sh
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/experiments/joint_information_probe.py \
  --smoke-only
```

观测：不同见证g1=0、同对象g2=1；随机7×8例的嵌套group minima为1.0978353、2.2226338、3.7058490、4.8930886，full group等于真实min；径向lift满足收缩并在full-d退化。不是新索引性能测试。

## 两进程实际并行

工具分别启动以下两个独立进程，时间有重叠；下列命令可在两个terminal复现。每次必须使用不存在的输出目录，程序拒绝覆盖。

```sh
EXP=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps
PY=production/nested-attr-vector-search/work/venv/bin/python
$PY "$EXP/experiments/joint_information_probe.py" \
  --dataset wiki-root3626 --out "$EXP/data/joint-information-run1/wiki-root3626"
$PY "$EXP/experiments/joint_information_probe.py" \
  --dataset sift12000 --out "$EXP/data/joint-information-run1/sift12000"
```

run1完整结束，但两个 `oracle_feature_terms` 列把group的C*r坐标项与lift的C个预计算标量混成一个成本。保留该instrumentation错误及全部数据，不能横向比较旧列。修正只涉及工作计数定义，不改几何：

- min_reduction_values记录实际传给各block min的标量数，group为C*(r/g)，lift为C；
- transform/group-sum形成不在此计数，provenance另记录全维平方差坐标项，不能冒充完整在线成本；
- 失败说明：`../data/joint-information-run1/measurement-note.json`。

以相同两进程方式完整重跑：

```sh
$PY "$EXP/experiments/joint_information_probe.py" \
  --dataset wiki-root3626 --out "$EXP/data/joint-information-run2/wiki-root3626"
$PY "$EXP/experiments/joint_information_probe.py" \
  --dataset sift12000 --out "$EXP/data/joint-information-run2/sift12000"
```

run2 WIKI105984行、SIFT67584行，合计173568；wall约98.0/16.8秒只是并行离线作业观测，不是隔离算法基准。每dataset仍只有8条旧query。原始逐query CSV gzip无损存放；不删失败、零增益或permuted分组。没有生成新数据集或把reserve当活对象。

## 分析/图：实际执行

run1分析在计数修正前也实际执行过，保留其analysis和第一张图；当前源码为修正后的run2口径。旧几何原始列仍可用于对照，旧成本聚合不参与主结论。

```sh
$PY "$EXP/experiments/analyze_joint_information.py" \
  "$EXP/data/joint-information-run2" \
  --reference "$EXP/data/joint-information-run1" \
  --figure "$EXP/figures/joint-information-run2.png"
```

实际返回：173568条信息记录与run1相同（仅排除改名计数字段），9216个g1旧case的候选数/排除数/raw oracle权重与首轮一致，observed false-pruned blocks=0。pooled452行/grid1808行；by-basis/by-scope保留。最终图实际打开检查，不把两个run当独立重复测量。

## 输入和正确性边界

- 从首轮run2 provenance定位四个输入文件，并重新核对其SHA；重复相同kmeans seed/参数，逐块size和g1 case核对，未静默换场景。
- 共享C++原始FP32矩阵路径/文件SHA保存在新provenance，`raw_distance_new_calls=0`，不能重复报告首轮的190952次调用为本轮新调用。
- bound检查对所有非空块执行；PCA Gram guard沿用首轮，观测无违规不等于通用浮点认证。
- gap/联合min均为离线枚举oracle；F构造和投影没有变免费。ANN返回的近点距离是min的上界，不能直接当组下界。
- 无在线C++候选原型、无HNSW队列改动、无PE/NodeANN新性能或更新实测。代码冻结来源、输入版本、numpy/faiss、随机种子都在provenance中。
