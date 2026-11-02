# 固定topk/efSearch的有质量约束对照：复现

从仓库根目录执行。本协议替代此前跨ef主曲线，不运行尚未实现的分层/b/预算剪枝设计。

## 主命令和两个固定分组

```bash
bash autoresearch.sh
# 同一入口：
bash researches/nested-attr-vector-search/2026-11-02-exps/experiments/run_dag_pareto.sh
```

| topk | 请求efSearch | 各方案共同有效宽度 |
|---:|---|---|
| 10 | 32 | 32 |
| 100 | 128 | 128 |

只跑这两格；不生成跨ef曲线，也不测量未被要求的格子。原生hnswlib搜索宽度至少为返回数，故所有方案及NodeANN的每个n统一使用`max(topk, requested_ef)`，请求值和有效值两列都保存。这两格本来就满足`ef>=k`，实际宽度等于请求值；保留规范仍用于防止把未来ef<k的格子悄悄改写。

M64、efConstruction200、向量/owner/种子、构图/入口固定。每格只有一个DAG参考点；NodeANN仅在格内扫全部整数n=1…k（各合格局部图使用同一个n，k=100即n=1…100）；不跨格调ef、k或DAG参数。四方法独立目录/源码/进程，recursive/direct分别运行，正式NQ120。

默认输出`work/dag-two-cases-m64/`。`DAG_PARETO_OUT`、`DAG_PARETO_CPU`、`DAG_PARETO_NQ`可改目录、绑核、query数；本次默认CPU3、单线程。每进程构建一次索引复用于两格，GT在计时外缓存top100，按格内k取正确距离边界。

- 编译：`g++ -std=c++17 -O3 -march=native -pthread -MMD`；真实include依赖和各库SHA分别存档。
- 每点预热整批一次，测10批次，每批遍历非空query16遍；参数点按固定随机序交错。正常批次无距离计数hook。
- QPS=`非空query数×16 / 批次耗时上中位数秒`；含搜索、归并、结果checksum消费，不含scope、GT、构建。
- 计数单独执行并验证返回ID一致，所有不合格距离0。未知native邻接次数留空，空scope审计行保留、不计非空QPS/Recall。
- 所有批次、点和逐query数据保留，不能从组间点连出跨ef曲线。

## 主图到底比较什么

每格用DAG实际query-average Recall作R*，从同格NodeANN全部已测n中，选Recall≥R*的最快点；ACORN也只在同格判断是否达到R*。达不到者标记未达标，不画零QPS柱，不用低召回时间冒充同质量结果。

这是**离线观测上包络**，不是可部署的n预测器，也不是精确同Recall/同minRecall/同pairRecall；保留实际Recall超额、尾部质量、返回数和原始批次范围。主图只画同格有质量约束的QPS对比，不把只有一个DAG点与另一方案跨ef曲线混画。未经统计显著性检验，不称普遍最优。

## 已执行行为回归

```bash
g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/dag-composition-methods/nodeann/hnswlib \
  -Icodes/directory-aware-vector-attr-search/src \
  researches/nested-attr-vector-search/2026-11-02-exps/experiments/nodeann_topn_test.cpp \
  -o /tmp/nodeann-topn-test
/tmp/nodeann-topn-test

g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/dag-composition-methods/dag_merge/hnswlib \
  -Icodes/directory-aware-vector-attr-search/src \
  researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_composition_test.cpp \
  -o /tmp/dag-composition-semantic-test-m64
/tmp/dag-composition-semantic-test-m64

python3 researches/nested-attr-vector-search/2026-11-02-exps/experiments/analyze_dag_pareto_test.py
```

覆盖局部n≠全局k、全局top-k集中于一属性、k50/100不足k的正确分母、全量预算下真实top10/50/100、top100缓存边界、ef<k输出容量、直接/递归/共享后代/空属性/CSR及资格路径；同文件新增“按查询删除不合格目标邻接后结果与距离/扩展工作量不变”的检查。Python回归阻止跨k/ef选点、排除低Recall快点、避免只选最高Recall、保持不达标缺失值及真实超额Recall。

## 跨owner边剪枝消融（归因诊断）

```bash
bash researches/nested-attr-vector-search/2026-11-02-exps/experiments/run_edge_prune.sh
```

同一入口先在本会话重跑`dag_merge`与`nodeann`参考（归档绝对时间对同一binary与输入最多慢约3倍，逐query距离数一致），再运行`edge-prune`模式：按查询删除目标owner不合格的邻接条目，入口表与所有合格邻接行逐字保留、顺序不变。

程序强制检查结果ID、距离数、扩展数、CSR布局、hits与每遍校验和一致，任一不符即中止且不写CSV。每查询剪枝图构建28.2–30.2 ms，排除在所有计时外，因此该消融是诊断性反事实，不是可实施优化。计时为16遍/批×10轮中位数，两臂每轮交替顺序。输出`work/edge-prune-m64/`，分析写同目录`analysis/`（反事实与拒绝比例两张表）。

## 单属性查询的表示分解（§4.3）

```bash
g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/dag-composition-methods/dag_merge/hnswlib \
  -Icodes/directory-aware-vector-attr-search/src \
  researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_induced_probe.cpp \
  -o work/edge-prune-m64/build/dag_induced_probe
taskset -c 3 work/edge-prune-m64/build/dag_induced_probe \
  work/packed/sift-fvecs-run1/sift_base.fvecs work/packed/sift-fvecs-run1/sift_query.fvecs \
  work/dag-scope/workload-run1 work/edge-prune-m64/induced-probe.csv 120

g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/dag-composition-methods/dag_merge/hnswlib \
  -Icodes/directory-aware-vector-attr-search/src \
  researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_degree_probe.cpp \
  -o work/edge-prune-m64/build/dag_degree_probe
taskset -c 3 work/edge-prune-m64/build/dag_degree_probe \
  work/packed/sift-fvecs-run1/sift_base.fvecs work/dag-scope/workload-run1 \
  work/edge-prune-m64/degree-probe.json 120
```

三臂分别是全局合并图、同邻接的紧凑局部表示、原生局部HNSW；局部id按全局id升序编号，前两臂的结果ID、距离数与扩展数必须逐条相同，否则中止。探针绝对水平比fanout协议高约18%，只用于同进程比值。

## 设计有限探针（不是分层ANN运行）

```bash
python3 researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_budget_semantics.py \
  --out work/dag-budget-semantics-reproduction.json

g++ -std=c++17 -O3 -march=native -pthread \
  -Icodes/dag-composition-methods/dag_merge/hnswlib \
  -Icodes/directory-aware-vector-attr-search/src \
  researches/nested-attr-vector-search/2026-11-02-exps/experiments/dag_memory_probe.cpp \
  -o /tmp/dag-memory-probe
/tmp/dag-memory-probe
```

原始探针JSON在同日data，复现改写work，不覆盖正式历史。逻辑删边不等物理回收，新分层/b算法与等服务字节预算尚未作为SIFT ANN执行臂。

## 归档分析

```bash
python3 researches/nested-attr-vector-search/2026-11-02-exps/experiments/analyze_dag_pareto.py \
  --dir researches/nested-attr-vector-search/2026-11-02-exps/data/dag-two-cases-m64 \
  --out work/dag-two-cases-archive-check --nq 120
```

分析器读取CSV或无损`.csv.gz`，核两格×所有局部n、k相关GT、共同实际宽度、距离/资格与批量QPS。旧跨ef与九格探索只保留历史原始点和源码快照，其图已撤下，不再由默认入口产生，也不作为当前交付对比。
