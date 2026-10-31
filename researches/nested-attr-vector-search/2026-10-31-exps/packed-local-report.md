# 整目录分块共享图：本地交付报告

> 本文是完整实现阶段的历史报告。用户随后改为查询效率优先，原矩阵/维护不再继续调度；最新[任务](packed-query-first-server-prompt.md)仅2build/9pass，新增[验证报告](packed-query-first-local-report.md)不覆盖本文150次记录。

## 目标与实际交付

用户要求完整方案和本地实现，而非继续给服务器“待实现”的候选。已实现[完整方案](packed-index-plan.md)：整目录DFS合并、shuffle/node消融、精确资格、多块过滤HNSW、全事件同步、局部压实/split和显式完整repack。真实WIKI/SIFT/SIFT1M性能仍交服务器执行，不预先声称优于NodeANN。

新增 `src/packed_index.h`、`scripts/run_packed.py`、`scripts/prepare_packed_sift.py`、`tests/test_packed.py`，扩展已有 `src/dynamic_triad.cpp` 的P臂。主A臂不加规划或扫描；旧H及closure代码不改，closure程序仅提供独立GT。当前ID空间固定，单线程，向量payload不变。

## 已运行的检查

编译真实 `dynamic_triad`、`closure_research`。固定hnswlib为 `ca426729609b3221563047ceb269cc1213e0d376`；编译存在库内既有unused/sign警告，无编译错误。

最终10项CLI行为回归通过：5项packed、2项eligibility、3项dynamic_triad。新增覆盖：

- 1200对象大目录在B1024下仍一图；18个100对象小目录合并为两图；全部直接owner查询一图。
- V/T/M/B/R/D/I混合轨迹，三种layout与独立canonical GT一致；元数据事件不产生图插删或距离。
- 初始全空、首次I、D释放空图、同ID复活、内部节点owner、R后资格。
- 超过2target的prospective split，不先向已满图插入；删除达到墓碑阈值后实际重建。
- 空块槽复用：2052→1027→2050活对象时槽数保持2；最终完整repack降至1图/1槽，结果不丢。
- 既有资格测试覆盖signed int64、字面−1、空tag模式、跨word、AND/OR等，不重复另造谓词实现。

### 最终冻结流程烟测

均为**合成数据**，不是SIFT或WIKI速度实验。diagnostic使用80对象/4D/6源query的既有fixture；scale路径使用4096对象/128D/256源query的随机FP32 fvecs，实际运行转换器、原程序、独立GT、串行轮换、维护与repack。

| 流程 | 运行数 | query记录 | update记录 | 完整repack次数 |
|---|---:|---:|---:|---:|
| diagnostic static | 57 | 10260 | 0 | 0 |
| diagnostic maintenance | 30 | 4590 | 690 | 24 |
| scale static | 33 | 8448 | 0 | 0 |
| scale maintenance | 30 | 6360 | 30420 | 24 |

总计150次回放、29658条query、31110条update。非法、重复、资格mask不一致、state violations、计时GT距离均为0。这里的query记录不是独立源query数；不以toy Recall或时延推断真实数据竞争力。

**纯NodeANN未被放慢或换算法**：最终程序在ef15/20各运行同一180Q混合轨迹，与改动前保存的纯A结果逐条比较。result_ids、gt_ids、distance_calls、graph_calls、fanout、scan_distance_calls、scalar_scan_group_calls共360条均无差异。这个检查证明已覆盖轨迹的行为一致，不是所有输入的形式化等价证明。

输入转换另核：原fvecs到base.f32逐FP32 bit保留；random/coordinate所有权的73个目录计数完全相等，向量payload一致。默认真实任务使用64/2048叶、100k/1M，不能用此次73节点toy代替规模结论。

## 开发修正与证据边界

最终运行之前补齐两项生命周期/成本边界：R的affected统计改用已有资格索引，避免为计数遍历全体对象；split复用空槽，full repack重置dead loc并回收旧块元数据。随后重新构建并完整运行上述最终矩阵，全部manifest源码SHA与交付源一致。

早期同名无`final-`目录的150次烟测保留在work，只是开发过程，不与最终运行相加伪称更多独立实验。聚合初稿曾使用不存在的summary键`state_violation_count`，只读分析报KeyError；改用实际`state_violations`后完成汇总，未改ANN结果。未发现最终回放语义错误。

证据：[data/packed-local-check.json](data/packed-local-check.json)，包含五个交付源SHA、各manifest SHA、计数和基线对照字段。大raw、二进制、合成fvecs保留 `work/packed-local/` 不提交；复现见[命令记录](experiments/packed-local-reproduction.md)。

## 研究结论与下一步

可采信：实现已贯通查询/维护/生成器/实验调度，完整owner、不漏选块、资格正确和有限生命周期边界通过实际程序验证。不可采信：尚无真实数据性能优势、新颖性通过、长期稳态或并发安全结论。

前序核查新增Window Filters ICML2024 §4.2–4.3/§5.2这一直接风险；已有TFANNS Packing和历史owner/place解耦继续保留。当前只允许把“层级相邻整组共用图”作为待检验的组织机制，不能把旧组件新名字当论文贡献。

下一步由服务器按冻结任务执行原数据诊断、100k四控制单元、有限维护；达到固定效率/质量门再跑1M。服务器只调整路径、CPU绑定和输出位置，不承担方案制定。
