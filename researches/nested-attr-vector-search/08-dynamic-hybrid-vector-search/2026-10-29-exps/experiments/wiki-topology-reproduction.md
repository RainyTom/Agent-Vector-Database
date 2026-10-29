# 原始 WIKI 子树 / 等占用拓扑配对复现

日期2026-10-29，仓库根目录执行。问题/选样规则见 [wiki-topology-protocol.md](../wiki-topology-protocol.md)。代码维护策略未变，仅新增分项计量和数据生成；不将新查询预算/负载下的时间与上一轮直接相减。

## 构建与语义门

```bash
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_dynamic_triad.py -v
```

已通过2项回归：原动态谓词/移动/重挂语义，以及墓碑重建、扫描→图、图→扫描三类触发、恢复旧标签、类别插入归因、初末真实图状态。每次更新计量分项和summary可以加回combined总量。hnswlib revision、g++15.2、`-O3 -march=native`、M16/efC200/seed100与前轮相同；所有被测搜索/GT仍为C++ `RawL2Space`。

## 小规模完整原始子树

```bash
PY=production/nested-attr-vector-search/work/venv/bin/python
GEN=codes/directory-aware-vector-attr-search/scripts/generate_wiki_topology_triad.py
RUN=codes/directory-aware-vector-attr-search/scripts/run_dynamic_triad.py
EXP=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps
$PY $GEN
MAN=$EXP/data/dynamic_triad/wiki-topology/manifest-wiki-topology-s20261029-q456-f7046c642b34.json
```

实际生成12配置：root410(11964条/2382节点/0局部图)、root3626(11869条/151节点/2局部图) × native/balanced × Q/U0.01/0.1/1。配对仅tree不同、同字面trace；没有对原始子树ITEM下采样。采用456个发布query vectors池，低Q/U仅60/59个查询事件，实际用到60/59个query vectors，不能宣称每点456个独立query。

独立provenance审计另逐项核对：所选原始vector rows恰好等于子树全体绑定行，复制向量逐FP32 bit相等，456个query vectors bit相等，原始父边/本地绑定与映射一致，配对其余输入hash一致。见 `data/dynamic_triad/wiki-topology/{source-audit,provenance-audit}.json`；原始ID映射/向量/trace在ignored work保留。

```bash
# 初始统一ef256：12 pilot，4个B/C点未过Recall门；失败点保留。
$PY $RUN --manifest $MAN --select '*qu0p01' --repeats 1 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/pilot \
  --work-root $EXP/work/dynamic_triad/wiki-topology/pilot-runs
# 统一ef1024：同12 pilot全部过门。
$PY $RUN --manifest $MAN --select '*qu0p01' --ef 1024 --repeats 1 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/pilot-ef1024 \
  --work-root $EXP/work/dynamic_triad/wiki-topology/pilot-ef1024-runs
# 正式108个计划运行；是否全部完成以results.json为准。
$PY $RUN --manifest $MAN --ef 1024 --repeats 3 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/main-ef1024 \
  --work-root $EXP/work/dynamic_triad/wiki-topology/main-ef1024-runs
```

4个ef256失败来自root410同一root query30：scope11964、eligible4228，B/C缺少GT ID1055；native与balanced的GT和漏项完全相同。非当前谓词/状态错误，不将低质量延迟用于同质量胜负，也没有为了补召回启用精确扫描兜底。

## 放大到完整root828子树

小规模正确性与统一ef1024 pilot通过后，激活预留的完整root828：48403条/4076节点/11初始局部图、最大直接节点3028、native相对深度12。其向量/cohort与小规模不同，不拟合固定数据上的N渐近规律。

```bash
$PY $GEN --cohorts 828 \
  --work-root $EXP/work/dynamic_triad/wiki-topology/scale \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/scale
SMAN=$EXP/data/dynamic_triad/wiki-topology/scale/manifest-wiki-topology-s20261029-q456-f34c73d7cae4.json
$PY $RUN --manifest $SMAN --select '*qu0p01' --ef 1024 --repeats 1 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/scale/pilot-ef1024 \
  --work-root $EXP/work/dynamic_triad/wiki-topology/scale/pilot-ef1024-runs
$PY $RUN --manifest $SMAN --ef 1024 --repeats 3 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/scale/main-ef1024 \
  --work-root $EXP/work/dynamic_triad/wiki-topology/scale/main-ef1024-runs
```

6个ef1024放大pilot全部过门；完整主矩阵为54个计划运行，最终完成/质量见results.json。独立原始源绑定/向量bit/原始边/配对输入核对见 `scale/provenance-audit.json`。未触发预登记的ef4096分支，不描述为已运行。

## 分析、审计与图

```bash
python $EXP/scripts/analyze_wiki_topology.py \
  $EXP/data/dynamic_triad/wiki-topology/main-ef1024/results.json \
  --out $EXP/data/dynamic_triad/wiki-topology/main-ef1024/analysis \
  --figures $EXP/figures/wiki-topology
python $EXP/scripts/analyze_dynamic_triad.py \
  $EXP/data/dynamic_triad/wiki-topology/main-ef1024/results.json \
  --out $EXP/data/dynamic_triad/wiki-topology/main-ef1024/analysis
# scale同入口，--figures使用 $EXP/figures/wiki-topology/scale，--out使用scale/main-ef1024/analysis。
```

新审计验证每update/summary的维护分项、B/C纯移动零ANN写、A初始图数、native/balanced同trace/非tree文件、root-recursive与nonrecursive GT一致及A维护事件计数序列一致。宽/窄recursive合法集合可变，不要求跨拓扑GT相同；同一拓扑三个臂的GT仍由原driver和通用分析核对。

每driver jobs2，建图/搜索/更新单线程；小规模/放大driver窗口重叠时最多4个benchmark进程，不称隔离串行。GT在计时外但在同进程，会影响下一查询缓存。吞吐是服务时间口径，不是GT/CSV/构建在内的墙钟吞吐，不是并发到达率压测。原始路径字符串不在源tree文件内，执行器路径键仍为稳定数字ID片段，不声称复刻原始Wikipedia标题字符串成本。

## 两遍GT缓存控制（最终计时采用）

inline-GT读取base，而A扫描复用base、HNSW有内部向量副本，因而计时外GT仍可能不对称热身。旧数据保留诊断，新增两遍协议，不更改索引/搜索/维护/预算。旧inline源快照为commit `4dcf430`；中断的放大任务用该版本driver、原binary与原hash身份resume，不能用新版driver假复用旧配置。

为避免覆盖仍被旧运行使用的binary，以下构建在 `codes/directory-aware-vector-attr-search/` 工作目录执行：

```bash
g++ -O3 -march=native -std=c++17 -Wall -Wextra \
  -o build/dynamic_triad_gt_tape src/dynamic_triad.cpp -Isrc -Ideps/hnswlib -lpthread
DYNAMIC_TRIAD_BINARY=/home/daihua/projs/2026040404-yiming/Agent-Vector-Database/codes/directory-aware-vector-attr-search/build/dynamic_triad_gt_tape \
  python -m unittest discover -s tests -p test_dynamic_triad.py -v
```

3项行为回归通过。回到仓库根目录，复用前文相同MAN/SMAN：

```bash
BIN=codes/directory-aware-vector-attr-search/build/dynamic_triad_gt_tape
$PY $RUN --manifest $MAN --select '*qu0p01' --binary $BIN --gt-mode precomputed \
  --ef 1024 --repeats 1 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/tape-pilot \
  --work-root $EXP/work/dynamic_triad/wiki-topology/tape-pilot-runs
# 同一命令追加 --resume 已验证：4个oracle/12个测量复用，stamp不变。
$PY $RUN --manifest $MAN --binary $BIN --gt-mode precomputed --ef 1024 --repeats 3 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/tape-main-ef1024 \
  --work-root $EXP/work/dynamic_triad/wiki-topology/tape-main-ef1024-runs --resume
$PY $RUN --manifest $SMAN --binary $BIN --gt-mode precomputed --ef 1024 --repeats 3 --jobs 2 \
  --tracked-root $EXP/data/dynamic_triad/wiki-topology/scale/tape-main-ef1024 \
  --work-root $EXP/work/dynamic_triad/wiki-topology/scale/tape-main-ef1024-runs --resume
```

两矩阵按顺序执行，内部每个先oracle pool结束、再benchmark pool，jobs上限2。完整计划108+54，不与另一矩阵oracle阶段交叉测量。长任务使用持久服务 `WikiTopologyTape20261029`，外部会话中断后可恢复，不用重写成功证据；只要数据/binary/tape/原始hash不匹配就拒绝复用。所有新模式用独立 `tape-*` 输出，不覆盖inline结果。

### 已观察到的oracle正确性与独立性

- 12个真实pilot全部过门：4个oracle实际执行94051次GT距离计算；12个benchmark GT距离调用合计0；714次query与71502次update的非计时行为、GT/result IDs、ANN导航和维护计数与inline逐条一致。
- 独立被修改的tape副本遭 `ValueError: oracle tape SHA mismatch` 拒绝；原tape/stamp保持有效。副本已移除，观测保存在 `tape-pilot/integrity-audit.json`。
- `--ef 1 --arms A --select wiki-root3626-native-item_move-r0p5-qu0p01` 的低预算控制：oracle ANN pair recall0.9879518未过门，但仍完成23272次独立精确GT调用；后续benchmark也被正确标记低召回失败且GT调用0，说明不把ANN结果充当GT、不因ANN低召回而禁止生成精确GT。保存在 `oracle-quality-control/`，不进入正式性能胜负。
- 使用相同4个pilot配置实跑 `--dry-run`：输出4个oracle与12个benchmark命令，阶段顺序明确，尚不存在的tape hash为null而非伪造值，所有benchmark均带 `--gt-in`；未启动C++、未创建目标run目录。CLI使用 `--select` 与 `--tracked-root`，观测保存在 `tape-pilot/dry-run-audit.json`。

`oracles.json` 保存真实GT生成时间/调用与data/trace/binary/tape/stamp SHA；测量的 `gt_us` 仅是当前canonical元数据核对和tape读取，不能称GT生成成本0。测量仍可能有元数据审计/文件缓存/多进程干扰，只消除“每查询额外向量GT读取”这一明确不对称因素。

### 对照分析入口

```bash
python $EXP/scripts/analyze_wiki_topology.py \
  $EXP/data/dynamic_triad/wiki-topology/tape-main-ef1024/results.json \
  --reference-results $EXP/data/dynamic_triad/wiki-topology/main-ef1024/results.json \
  --out $EXP/data/dynamic_triad/wiki-topology/tape-main-ef1024/analysis \
  --figures $EXP/figures/wiki-topology
# scale同样传入其inline results作为reference，写入scale/tape-main-ef1024/analysis。
```

GT-mode-control审计只核对输入身份、当前状态、排名ID、ANN距离/hops和维护操作数，不把不同阶段背景负载下的延迟差全解释为缓存贡献；最终胜负只用两遍协议自身的三重复同质量点。

## 完成与实际产物

`WikiTopologyTape20261029` 已以exit0结束：旧inline放大54次恢复完成；两遍GT正式small108+scale54均完成且全部过质量门。4dcf430保存旧测量源码；两遍执行器binary SHA为 `e51a31368ceba046dbb6a29917d29e51718409a22b8562c9381c9525906a8a7c`。编译命令、源码SHA、GCC15.2.0、Python3.13.16、Ryzen5800X/16逻辑CPU/32MiB L3见 [wiki-topology-build.json](wiki-topology-build.json)。未绑核，计时不宣称资源隔离。

- `tape-main-ef1024/analysis/`：108运行，238104 query/643518 update工作量等价；12 oracle、12400619次真实GT调用。
- `scale/tape-main-ef1024/analysis/`：54运行，483552 query/1306908 update工作量等价；6 oracle、101782460次真实GT调用。
- 两者benchmark GT调用均0；topology checks共81组；generic分析器分别输出4527/3141行query slices与108/54行update slices，错误均空。
- `figures/wiki-topology/tape-main-ef1024_root{410,3626,828}_{frontier,scope}.png` 为正式图，六张均已实际打开检查；inline图仅诊断。
- 新C++行为的3项回归测试通过；真实两阶段pilot、resume、tape损坏拒绝、低ef的ANN失败仍可生成精确GT、dry-run无run目录写入均已执行。完整数字与失败点见报告，不将oracle replay计为正式benchmark。

small分析/制图在scale末段运行时短暂重叠；三重复SD只是本机非隔离运行波动，不据此估算纯缓存贡献、并发吞吐或统计置信区间。大型向量/索引与逐事件CSV仍在ignored work中；tracked保存manifest、SHA、汇总CSV/JSON、协议、源码和图。
