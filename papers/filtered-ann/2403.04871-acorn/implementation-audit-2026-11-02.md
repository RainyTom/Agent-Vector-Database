# ACORN-1 搜索策略移植：原文、作者代码与本地实现边界

## 文献身份与核对版本

- Liana Patel、Peter Kraft、Carlos Guestrin、Matei Zaharia：*ACORN: Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data*，PACMMOD 2(3), Article120,2024，[DOI](https://doi.org/10.1145/3654923)。类别B：filtered per-query top-k ANN，可迁移借鉴，不是 vector join。
- 正文核对本地 [arXiv2403.04871v1](https://arxiv.org/pdf/2403.04871v1) 的 §5.1/Algorithm2、§5.2、§5.3，PDF页4–6；正式DOI稿与v1的完整差异仍未逐项核实。
- 既有作者入口 `stanford-futuredata/ACORN` 重定向到 `guestrin-lab/ACORN`。本轮静态核对固定 [commit c259f11cdbec880671658eaa0d8747e2cc9de79d](https://github.com/guestrin-lab/ACORN/commit/c259f11cdbec880671658eaa0d8747e2cc9de79d)。未编译或运行作者FAISS代码；该快照是否精确对应论文实验版本未核实。

## 原文明确规则

1. §5.3：ACORN-1 在搜索期收集当前节点的所有一跳及二跳邻居，再过滤谓词并截取前M个。**合格和不合格一跳中间点都要展开**，不是只越过被拒绝节点。
2. §5.1/Algorithm2：使用候选集合C、ef大小的W及visited集合T；过滤后邻域的前M个才进入visited/距离/候选维护。W满且最近待扩展点严格远于W最差点时停止。提取文本中的终止行写efc，而输入及其余行使用ef；本地按查询ef理解，不将构建ef混作查询预算。
3. §5.2/5.3：ACORN-1 的“without pruning”包括不使用普通HNSW的RNG几何多样化剪枝，不只是关闭ACORN压缩。
4. §5.1脚注与§5.2描述针对极低选择率的pre-filter/exact-scan调度；它是系统策略，不是Algorithm2的必需邻居展开步骤，不能机械地对ACORN-1应用1/gamma=1而让每条查询都精扫。

## 作者代码与正文不完全一致

| 项 | 原文v1 | 固定作者快照 |
|---|---|---|
| 一/二跳枚举 | 全部一/二跳，未规定扁平顺序 | 原生邻接顺序交错处理每个一跳点及其二跳列表 |
| 邻域截断 | 过滤后前M | 底层使用名义2M，计数/visited/提前退出交错 |
| 去重 | Algorithm2有搜索visited；局部邻域去重细节未给 | 底层搜索visited抑制重复评分，但重复出现可消耗计数；高层可重复评分 |
| 构图 | 不使用普通RNG剪枝，gamma=1、M_beta=M | gamma=1实验配置M_beta=2M；候选与空间分配另有底层约定 |
| 入口 | 固定全局入口 | 先无条件评分入口，不保证所有被拒绝入口零距离 |
| 队列 | Algorithm2的C/W描述 | 默认使用bounded候选与count_below停止机制 |

原始定位：
- [ACORN构图剪枝511–565](https://github.com/guestrin-lab/ACORN/blob/c259f11cdbec880671658eaa0d8747e2cc9de79d/faiss/impl/ACORN.cpp#L511-L565)、[候选生成699–718](https://github.com/guestrin-lab/ACORN/blob/c259f11cdbec880671658eaa0d8747e2cc9de79d/faiss/impl/ACORN.cpp#L699-L718)。
- [底层搜索1188–1227](https://github.com/guestrin-lab/ACORN/blob/c259f11cdbec880671658eaa0d8747e2cc9de79d/faiss/impl/ACORN.cpp#L1188-L1227)、[入口与高层1485–1514](https://github.com/guestrin-lab/ACORN/blob/c259f11cdbec880671658eaa0d8747e2cc9de79d/faiss/impl/ACORN.cpp#L1485-L1514)。
- [demo参数161–176](https://github.com/guestrin-lab/ACORN/blob/c259f11cdbec880671658eaa0d8747e2cc9de79d/demos/test_acorn.cpp#L161-L176)。

## 本地实现选择与不能声称的事项

[实现](../../../researches/nested-attr-vector-search/2026-11-02-exps/experiments/global_acorn1_hnsw.h)移植的是 **ACORN-1搜索策略**，采用原文Algorithm2/§5.3口径，不复制作者快照的全部队列行为：

- 保留固定hnswlib `ca426729609b3221563047ceb269cc1213e0d376` 的全局图，**包括其RNG多样化剪枝及M/2M邻接存储**。这是构图差异，不能称完整ACORN-1索引复现，更不是ACORN-gamma。
- 每次查邻居先按本地原生一跳顺序，再按每个一跳的原生二跳顺序收集；局部稳定去重、排除零跳自身，过滤后保留前M，底层也用论文M而非作者快照2M。去重在搜索visited之外单独维护，计数和序列规范化是本地明确约定。
- 高层与底层都使用扩展邻域。底层只有一个队列。保留已有合格owner静态代表入口，以满足本项目不合格item零查询距离的要求；不复制作者无条件评分原始入口的行为。
- 所有查询距离经RawL2Space固定FP32内核；没有FAISS运行时、量化或精确扫描回退。
- 原文s_min扫描策略未启用，以隔离图搜索机制并满足图主导航约束；极低选择率下可能达不到目标召回，必须如实报告。
- 邻接检查、二跳列表展开（含合格中间点）、局部唯一合格候选分别计数；不把不算距离的邻接读取当作零成本。

合成回归已核验上下层越过不合格中间点、合格但更远的中间点也必须展开、稳定去重与M截断、零不合格距离及严格二跳界。固定SIFT运行证据与当前研究结论统一见[当日报告](../../../researches/nested-attr-vector-search/2026-11-02-exps/report.md)，不能由本地移植结果替代论文原始实验结论。ACORN-gamma构图、作者完整运行时复现、真实语义DAG及新颖性认证均不在已完成主张内。
