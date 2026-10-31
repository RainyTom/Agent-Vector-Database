# 给实验服务器AI：整目录层级分块共享图，完整执行任务

> **已被用户后续指令替代，不再执行本文件的完整矩阵/维护/扩规模。** 当前只验证查询效率，复用已有数据和GT、同进程索引；请执行[最新9pass任务](packed-query-first-server-prompt.md)。本文件保留历史合同，不删除已完成结果。

## 交付入口与不可变要求

源码提交：**`cdc28f3`**（`review/handoff-20261029`）。已有服务器状态3aaa0ed/资格执行接入4fc4b00是前置，不整分支覆盖已有报告，不恢复旧H。需要依次读：

1. `researches/nested-attr-vector-search/2026-10-31-exps/packed-index-plan.md`：全部算法、理论边界、控制、门槛。
2. 同目录 `packed-local-report.md` 与 `data/packed-local-check.json`：150次本地合成回放，不是真实性能。
3. `codes/directory-aware-vector-attr-search/src/packed_index.h`、`src/dynamic_triad.cpp` P路径。
4. `scripts/run_packed.py`、`scripts/prepare_packed_sift.py`、`tests/test_packed.py`。
5. `eligibility-3aaa0ed-review.md`：旧结果勘误与每图65536锁开销；旧H仍停止。

本次核心和实验驱动已实现。你的工作是集成、路径微调、构建、真实实验和报告，不是另造算法。不得把“缺实施”当阻塞继续交空方案。

## 1. 方案合同

- P-dfs：真实目录DFS顺序，**整direct-owner组不可拆分**；小组next-fit合并至B4096；大组≥B单独一图。内部目录直接owner必须保留。
- 精确资格mask→枚举eligible ID→按loc分块→搜索全部非空eligible块，不用proxy挑子集。局部ef根据E_b封顶。基础HNSW M16/efConstruction200/seed100，不改图边或停止规则。
- 逻辑M/R不搬向量；D墓碑≥1/3同步局部压实；I可触发prospective split；空槽复用；完整repack全部计费。
- A-graph是纯NodeANN：每个非空direct目录一图，scan0、eligibility off。不要为了使基线慢而额外加工作；也不要给它加小目录扫描、资格前置或其他规划。
- P-node是资格/预算/放置消融，**不是A-graph**。P-shuffle打乱目录组；P-dfs-aware单列全局小E扫描；Exact不是免费oracle。

## 2. 集成与E0

在当前服务器实验分支保留已有工作，fetch后cherry-pick `cdc28f3`。若dynamic_triad.cpp发生冲突，按现有A/B/C保持不变、只接入P臂和新增计数；不得用旧整文件覆盖服务器新增结果。源文件SHA必须与本地证据一致，若确有必须的环境兼容修改，独立提交、披露差异、重新E0。

从仓库根进入 `codes/directory-aware-vector-attr-search`，执行：

```bash
bash build_dynamic_triad.sh
bash build_closure.sh
python -m unittest discover -s tests -p 'test_packed.py' -v
python -m unittest discover -s tests -p 'test_eligibility.py' -v
python -m unittest discover -s tests -p 'test_dynamic_triad.py' -v
```

应为5+2+3项行为回归。若失败，先修真实实现错误，保留失败记录；不要把错误实现运行称为索引负结果。现有库编译warning不是忽略新错误的理由。

## 3. E1：真实旧数据诊断

下面从代码目录执行；CPU选择一颗实际允许的空闲CPU，示例2；输出目录必须不存在。

```bash
python scripts/run_packed.py \
  --previous-raw ../../work/closure/static-7654f1e-run1 \
  --out ../../work/packed/old-diagnostic-static-run1 \
  --suite diagnostic --stage static --cpu 2
```

原始static manifest所在路径如不同，只改路径；不得将eligibility新manifest冒充该输入，必须保留原始data/source_trace/qidx组及SHA。这是两个数据集共114次计时，独立GT不在查询计时里。不是旧H重跑；每条配置均保留。不因旧数据输赢取消下一步100k的规模假设检验。

## 4. E2：真实SIFT100k，四个控制单元

服务器先从已有数据清单/下载记录定位真正 `sift_base.fvecs` 和 `sift_query.fvecs`。若只有1M f32，优先寻找原fvecs或根据**已核实格式**做无损封装并披露，不生成随机向量冒充SIFT。不存在真实输入时记录缺少的确切文件/已查来源，完成可运行的旧数据任务；不能声称已完成SIFT1M。

在代码目录设置真实路径；下列命令中的两个路径是唯一需要按机器替换的输入，CPU可替换：

```bash
export SIFT_BASE=/actual/path/sift_base.fvecs
export SIFT_QUERY=/actual/path/sift_query.fvecs
export PACK_CPU=2
for LEAVES in 64 2048; do
  for OWNERSHIP in random coordinate; do
    NAME=sift100k-l${LEAVES}-${OWNERSHIP}
    DATA=../../work/packed/input-${NAME}-run1
    python scripts/prepare_packed_sift.py \
      --base-fvecs "$SIFT_BASE" --query-fvecs "$SIFT_QUERY" \
      --n 100000 --leaves "$LEAVES" --ownership "$OWNERSHIP" \
      --queries 256 --query-offset 1024 --out "$DATA"
    python scripts/run_packed.py \
      --dataset "${NAME}=${DATA}" --trace "${NAME}=${DATA}/mixed.trace" \
      --out "../../work/packed/static-${NAME}-run1" \
      --suite scale --stage static --cpu "$PACK_CPU"
  done
done
```

总计4×33=132次计时；每单元独立GT。脚本串行轮换3次、限制单线程、记录source/input/binary SHA、CPU affinity、每query CSV。

实际向量来自SIFT；层级/属性是明确合成控制。粗64叶不能省略；细2048叶不能自行再加细去拖慢A。random/coordinate保留每目录对象数，后者使用基向量固定坐标投影排序；不能用query/GT做owner分配。记录每owner分布、内部owner比例、独立源qidx数、实际scope/E。

主ef20，ef15敏感性；k10；不得加ef64/1024、扩大B、换SIMD核或修改算法追正。所有点都报告，不只挑最优点。固定选择率与期望E128/512两族分别列，不能把期望E称为精确E。

### 唯一允许的1M推进门

100k四个单元全部完成后，读取各 `comparisons.json` 的 `group=confirmation_nonempty, ef=20, right=A-graph`：

- 至少一个**细目录2048叶**单元的P-dfs或P-dfs-aware，A/P双方macro及pair同时≥.99；
- 确认非空独立qidx≥20；
- 三次各耗时比≤1，三次耗时比算术均值≤.80。

注意脚本比较对含P-dfs/A；P-dfs-aware对A用同一runs.json确认分组配对计算，不改运行配置。不允许把其对P-dfs的比较当对A。若仅aware通过，明确是包含执行策略的推进理由，不宣布pack机制通过。

失败就**停止1M**，完成100k维护与报告，不追加参数、不改门槛。未运行1M记“未放行”，不是1M实验失败。覆盖不足单独标注。通过后执行同一四单元，唯一变更N=1000000和NAME为sift1m；其他参数不改，每单元33次。不是只跑获胜的那一个布局/所有权控制。

内存估计超过80%可用RAM时程序会在执行前停止；先换足够资源/查进程，不静默减少N、目录数、图容量或删基线。失败manifest保留。

## 5. E3：有限维护与恢复代价

对已经生成的100k四单元全部执行；1M获准时也对1M四单元执行。代码目录运行：

```bash
for LEAVES in 64 2048; do
  for OWNERSHIP in random coordinate; do
    NAME=sift100k-l${LEAVES}-${OWNERSHIP}
    DATA=../../work/packed/input-${NAME}-run1
    python scripts/run_packed.py \
      --dataset "${NAME}=${DATA}" --trace "${NAME}=${DATA}/mixed.trace" \
      --out "../../work/packed/maintenance-${NAME}-run1" \
      --suite scale --stage maintenance --cpu "$PACK_CPU"
  done
done
```

每单元30次：A/P-node/P-shuffle/P-dfs/P-dfs-aware，ef20，3轮×mixed/envelope。100k共120次。1M替换NAME后同120次。

- mixed前256Q包括持续属性变更、单对象移动、D/I及两次1%-N固定迁移burst。查看burst前后分组，不能只报总体均值掩盖物理分散。
- envelope包含R往返、B及逐M恢复、初始ceil(N/3)删除、实际压实和一次I，是应力包络，不是自然应用分布。
- 每个P结束都执行完整repack，`final_repack_complete=1`。repack费用在summary，不伪造trace事件。A不强加无必要全rebuild。
- 必须查看更新CSV的 `graph_distance_calls,packed_splits,packed_rebuild_us`，旧update_metrics摘要尚不包含这些新增列；不能因摘要空值说计数为0。
- summary `total_workload_us=query_total_us+update_total_us+final_repack_us`。完整生命周期另加`1e6*build_s`。RSS含整个运行峰值，不拿序列化indexFileSize代替。

维护包络在大N含大量真实更新，要保证日志磁盘，不删昂贵事件、不减少三轮。若运行失败保留日志和失败点，先分类是资源/实现还是机制问题。

## 6. 固定报告要求

先给结论，再给数据：

1. 语义及图路径检查：非法/重复/state/mask/计时GT错误；A扫描距离/扫描组=0；A graph_calls=fanout；P所有eligible块覆盖；cap不偷用扫描。
2. 同预算全点及共同质量.99/.95层：macro和pair一起，非空均值/p50/p95/p99、独立qidx及空GT单列。ef20是主点，不在确认数据重新选参数。质量未过层只能给诊断耗时比。
3. 机制分解：P-node→P-dfs、P-shuffle→P-dfs、cap→aware；图调用/距离/hops、资格/搜索/e2e；粗细目录及random/coordinate；归因不得只看一个计数。
4. 构建和空间：build、图数、capacity、graph payload/metadata估计、峰值RSS；固定65536锁/图在服务器ABI的开销单列，不把全部RSS归因于向量副本。
5. 动态：按事件类型均值/p95/p99/max，split/压实次数和距离，最后全repack停顿/距离/插入；总生命周期成本和查询净收益的break-even，不只报M便宜。
6. 问题分类：未实现/实现失败/近似质量不足/覆盖不足/真正同质量效率负结果/未放行分开。
7. 新颖性只据证据：若dfs≈shuffle，或仅小E扫描、固定容器开销减少，降级成组织/维护策略；不要凭“用了层级/bitmap/packing”宣布论文创新。

保存到新的日期实验目录，或在本日新命名文件中追加，不能覆盖历史：report、experiments复现、data小CSV/JSON、manifest/source SHA、raw inventory。大向量、二进制和raw留work，不提交；小聚合必须足以复算结论。提交fetch后推送当前实验分支，不强推、不整分支合并、不更改历史H负结果。

最终回复用：研究问题→实现/前序边界→实际矩阵→全部质量与成本→失败归因→是否支持假设→提交与阅读入口。**本任务不再向用户询问“继续实现还是先交付”；所有可执行阶段按此合同完成，1M只按既定门控。**
