# 交付方案：从原始数据到属性 DAG 合并图

本文定义当前可运行方案及实验接口。统一使用“属性”。属性 DAG 的节点与 ANN 的 item 节点是两类对象，不能混称。当前入口为 `bash autoresearch.sh`；各方案使用独立 hnswlib 源码和独立进程。

## 1. 原始输入：先有记录，没有 ANN 图

本实验组合三份来源独立的数据，不存在 SIFT 描述子与论文标签之间的真实语义对应关系。

| 数据 | 最初形态 | 在实验中的用途 |
|---|---|---|
| SIFT | base 为 1,000,000×128，query 为 10,000×128；本地容器存 FP32，值来自原始整数描述子 | item 向量、查询向量 |
| CCS 2012 HTML | 属性名及分类关系的文本表示，已经编码属性层级，但不是 ANN 索引 | 解析属性 ID 与父子关系 |
| SciHTC 标签 CSV | 论文 ID 与分类标签记录 | 生成实验用直属属性数量分布 |

**一开始没有 ANN 节点、相似边、portal 或入口点。** CCS 文件已经表达属性关系，不能说原始数据完全不存在任何图关系。属性记录先被解析成 DAG，之后才将向量记录插入 ANN 索引。

### 1.1 生成结构文件

- `objects.txt`：第 i 行为 item i 的一个直属属性 ID（单 owner 工作负载）。原始标签经过 workload 生成过程归约成一个终端直属属性；不声称原始论文只有一个标签。
- `edges.tsv`：每行 `child parent`；读入后转换成 `children[parent]`。
- `queries.tsv`：每行 `count attr_1 ... attr_count`。本实验将多个指定属性按 OR 组合；是否包含后代由独立查询模式决定。
- `meta.json`：数据来源、输入 SHA256 和规模。查询文件本身不携带向量，也不携带 ANN 结果。

现有 fixture 为 168,937 个 item、1,929 个属性、2,040 条属性边、103 个多父属性。其中 768 个属性有直属 item，66 个属性没有直属 item 但后代有 item，1,095 个属性及其全部后代都没有 item。这些空属性不建向量图，也不产生虚假 portal。

### 1.2 绑定向量与属性

取 SIFT base 前 168,937 行，与 `objects.txt` 的行数对齐。C++ 用固定 `mt19937(20261101)` 打乱 owner 数组，保留每属性 item 数，消除两份独立数据按行排序造成的偶然对齐。

这是**随机属性绑定的结构实验**，不是把 SIFT 的图像语义标注成 CCS。真实向量—属性相关性、多个直属标签和多属性 AND 均未由这份数据验证。随机化也不能作为“所有真实层级均无几何信息”的证据。

### 1.3 派生内存对象

1. 校验属性 ID、边及查询表达式。
2. Kahn 拓扑排序；有环则停止，不把 DAG 偷换成树。
3. `members[a]` 收集直属 a 的 item ID，按 item ID 升序。
4. `desc[a]` 对 DAG 做去重遍历，保存 a 自身及全部后代属性 ID。共享后代只计一次。
5. item 原始向量保持 FP32，尚未有 ANN 边。

## 2. 查询语义：递归与非递归严格分开

设查询指定属性集合 A，item u 的直属属性为 owner(u)。

| 模式 | 合格条件 | 搜索入口代表 |
|---|---|---|
| 非递归 `direct` | owner(u)∈A | 各指定属性的直属 item 代表 `direct_portals[a]` |
| 递归 `recursive` | owner(u)∈并集 desc(a)，a∈A | 去掉有已选祖先的冗余属性后，使用对应子图代表 `portals[a]` |

例如属性 A→B，item x 直属 A、item y 直属 B：查询 A 的非递归模式只能返回 x；递归模式允许 x、y。若 A 没有直属 item，非递归结果为空，不得自动改查 B。

**非递归不能直接复用子树 portal**：子树代表可能全部属于后代。当前索引同时保存直属代表和子图代表，查询根据模式选择，无需查询向量或 GT 来补入口。

GT 对当前模式的合格 item 做精确 squared-L2 top-k，分母为 `min(k,eligible)`；本轮比较格为k=10与k=100，GT在计时外缓存top-100并按格取对应距离边界。使用距离边界 ties-aware hits：允许距离不大于第 k 名的任意不同合格 item 命中，但返回数不得超过 k。欠填计入召回损失，不能按返回数归一化。空查询保留数据行、独立统计耗时，不计入非空 Recall 均值。

## 3. 属性内部：由向量记录建立本地 HNSW

每个非空直属集合建立原生 `hnswlib::HierarchicalNSW<float>`：

```cpp
local[a] = std::make_unique<Graph>(&raw_l2, members[a].size(), 64, 200, 100 + a);
for (Id item : members[a]) local[a]->addPoint(base.at(item), item);
```

- 当前统一 M=64、efConstruction=200；四方案的本地/全局原生 HNSW 均按此构建。原生 level-0 邻接容量为 128，上层为 64；最终合并图不受这个 128 的全局度上限约束。此前 M=16 的 CSV 是历史结果，不改写为 M64。
- 默认按 item ID 插入；局部随机层数的种子为 `100+a`。
- 随机种子与插入顺序作为独立实验因素，不改变 owner 分布。
- hnswlib 原生建图用高层 greedy descent、逐层 efConstruction 搜索、多样化邻居选择及反向邻接更新，不等同于全连接或全扫描。
- 距离由共同 `RawL2Space` 调用 pinned FP32 squared-L2 核，无量化。

这一步才为 item 创建实际 ANN 节点及相似边。没有直属 item 的属性跳过，但仍保留在属性 DAG 中。

## 4. Portal：挑代表，不压缩向量、不删除 item

两类代表都是真实 item ID，不是新造的中心点：

- `direct_portals[a]`：从 `members[a]` 选至多 p=8 个代表。
- `portals[a]`：从“a 的直属代表＋每个直接子属性的子图代表”的去重候选池再选至多 8 个。

选点算法是确定性的 farthest-first：

```text
候选排序去重；不足 p 个则全留
S = {最小 ID 候选}
循环直到 |S|=p：
    更新每个候选到 S 的最小 squared-L2
    加入该最小距离最大的未选候选；平局取较小 ID
```

对子图代表先处理子属性，再处理父属性。复杂度为 O(p·候选数·维数)，直属代表需要从全部直属 items 选，不能只按“每个属性有 8 个候选”估算总成本。

farthest-first 鼓励代表分散，不保证靠近未来查询。原始欧氏候选集上的 2 倍 k-center 半径性质，不能直接作为这里的 squared-L2 保证，更不能扩展到经过多层代表筛选的全部原始子图；本方案不主张这个理论保证。

## 5. 沿属性 DAG 组合一张 item 图

基线只加边，不删原本地 level-0 边。按以下阶段构建：

### 5.1 本地边入图

抽取每个本地 HNSW 的 level-0 边，补反向边并去重。合并图保留 item ID，不把共享后代的 item 复制成多个节点。本地 HNSW 的高层不进入当前合并图；NodeANN 则继续使用其各自完整原生层次。

### 5.2 自底向上得到子图代表

```text
按逆拓扑序遍历属性 a：
    pool = direct_portals[a] ∪ 各 child 的 portals[child]
    portals[a] = farthest_first(pool,8)
```

### 5.3 每个属性连接输入组

组包括非空直属代表，以及各非空直接子属性的子图代表：

1. **组间 MST**：组间距离为两组 portal 最近距离，Prim 选择连接端点；共享 item 可产生零距离合并，自环不入图。
2. **portal 最近邻**：每组每个 portal 向其他每组最近的一个 portal 加双向边。
3. **分散正向桥**：对自身及直接子属性的每个直属 item u，向每个组中 `owner(v) != owner(u)` 的最近 portal v 加 u→v。
4. **受限反向桥**：v 收集候选来源，按 `(距离,来源ID)` 保留最近 16 个，再加 v→u。去重前重复来源可能占槽，因此是最多 16 个不同反向来源，不保证恰有 16 个。

分散桥的起点是**全部相关直属 items**，不是只有 8 个接口。因此跨属性边数量和构建距离次数依赖 item 数、属性分支数及 portal 数，不能说只随属性数增长。

距离优先保留集合与插入次序无关；portal 计算仍必须遵守子先父后。FCFS 反向准入只是顺序敏感的消融，不是当前默认算法。

### 5.4 可选平面跳转边

将 a 的直属代表与子图代表相连。其他消融包括关闭跳转、子代表连父代表、连根代表。这些仍是同一邻接中的平面边，**不是 HNSW 高层**。

### 5.5 可选出度剪枝

按距离及多样化启发式压到 16/24/32 等上限，是实验配置，不是已验收的默认优化。它可能删除本地边，因而不再满足“保留全部本地边”；也可能破坏资格诱导子图可达性。整图可达不能代替有过滤条件的可达性。

## 6. 查询如何走图

1. 由模式得到精确 allowed 属性集合；递归/非递归分别处理。
2. 选择对应代表表，所有入口必须先判资格。
3. 全部入口加入同一个 best-first 搜索；不是对每个属性独立做 top-k 再归并。
4. `frontier` 为待扩展小顶堆，`best` 为容量 ef 的结果大顶堆。
5. 邻居若已访问则跳过；未访问者先判断资格。不合格者不算查询距离、不入队、不借其邻接导航。
6. 合格未访问节点计算 squared-L2；能进入当前 best 时加入 frontier。
7. best 已满且当前最近待扩展项劣于 best 最差项时按启发式停止，或 frontier 耗尽。
8. 从 best 取至多 k 个不同合格 item；本轮k=10或100由固定比较格指定，搜索函数不内嵌固定10。

**ef 不是距离调用上限、扩展次数上限，也不是 frontier 的硬容量。** best 的旧候选仍可留在 frontier。早停只是近似搜索规则，不证明尚未访问节点不可能含更近结果。访问 20% 的合格 item 既不是召回 20%，也不能单凭访问比例证明失败原因。

visited 消融包括 epoch 数组和按触及 word 清理的位图。两者必须保持相同资格检查/标记顺序，并验证 ID、距离数和扩展数一致。

2026-11-03更新：主`search_counted`在固定资格的严格b=0查询内缓存拒绝结果；按查询epoch重置资格状态，拒绝仍不评分、不导航。资格调用/拒绝trace可少于旧CSR/visited位图消融，验收要求是返回有序ID及距离/扩展/邻接计数相同，而非资格调用数相同。主路径复用堆与pending容量、满堆replace-root并仅预取合格向量；图与入口不变。[实测及边界](../2026-11-03-exps/report.md)。

## 7. CSR 与 bitmap 分别是什么

### CSR：邻接存储，不是另一种 ANN 算法

`offsets[u]..offsets[u+1]` 指向连续 `neighbors` 数组中 u 的邻接区间。例：0→[2,5]，1→[0]，则 `offsets=[0,2,3]`，`neighbors=[2,5,0]`。它不压缩向量、不改变边、不自动减少距离调用。是否提速由实测决定。

### 三种属性资格表示

| 表示 | 查询时判断 | 额外成本 |
|---|---|---|
| 属性 byte mask | `allowed[owner[u]]` | 小属性表＋item→owner 访问 |
| 属性 bitmap | owner 对应 bit | 仍需 owner 访问，再做移位掩码 |
| item bitmap | item 对应 bit | 先物化当前查询的 item 合格集合 |

item bitmap 的构建、分配、初始化不能免费。复用同一 scope 时只允许在同一批次的 K 个查询间复用，批次开始的构建和结束释放都计入总时间。visited bitmap 是另一用途，不能称为属性判断加速。

合成多标签微基准是“8 个随机标签与 16 个选中标签中**任一个**匹配”的 OR，不能解释为两个必需标签或 AND 的证据。

## 8. 每个方案独立目录、独立 hnswlib

| 方案 | 目录 | 实际查询实现 |
|---|---|---|
| 本方案 | `codes/dag-composition-methods/dag_merge/` | 自有库扩展中的构建器及过滤单层搜索；CSR/bitmap/剪枝为内部消融 |
| NodeANN | `codes/dag-composition-methods/nodeann/` | 自有库的原生 `searchKnn`，逐合格属性图搜索及归并，仅作对照 |
| ACORN-1 搜索策略移植 | `codes/dag-composition-methods/acorn1/` | 自有库的全一/二跳邻接扩展，过滤后截断；全局单队列 |
| 严格全局过滤控制 | `codes/dag-composition-methods/global_prefilter/` | 自有库的资格前置搜索，不通过被拒绝节点导航 |

每目录包含独立普通源码文件、Apache LICENSE、`UPSTREAM.json` 和 `main.cpp`。native header 来源统一固定为 `ca426729609b3221563047ceb269cc1213e0d376`；不是符号链接，也不在运行时共享另一方案的搜索库。构图与搜索扩展位于各自 `hnswlib/hnswlib/` 内，`dag_composition_method.h` 为本地修改入口。

共同代码仅负责输入、资格语义、精确 GT、计时、计数与 CSV，不调用旧共库实验程序。编译器依赖清单验证每个二进制确实只引入自己目录的 hnswlib。

ACORN 条目仍是搜索策略移植，不是作者完整索引复现：当前保留原生 RNG 构图，本地入口、枚举及队列口径与作者 FAISS 版本有已记录差异，扫描回退关闭，不是 ACORN-gamma。独立库不等于完成论文系统复现。

ACORN每次邻域过滤后的截断上限读取实际`graph.M_`，当前为64。提高M同时改变原生图密度与该搜索上限，不是只扩大内存预留；portal=8、反向桥上限16、可选剪枝16/24/32均保持独立定义。

## 9. 实验验证协议

- 同一 NQ 下，两种模式使用相同 source scope ID 和 query vector ID，独立生成 allowed 与 GT。
- 当前正式M64矩阵固定120查询，分别测direct/recursive；非held-out。此前M16的24/120构图变体与bitmap复用数据保留为历史证据。
- 固定 M=64、efConstruction=200；从实际native index保存`index-parameters.json`。portal=8和反向桥16不变，不把它们当成M。
- 只比较两组固定设置：`k=10, efSearch=32` 与 `k=100, efSearch=128`。组内不调ef、k或构图参数；不把不同ef的点连成曲线。
- 全局k按组固定；NodeANN每个合格图返回同一n，格内扫全部整数`n=1…k`（k=100即1…100），再取全局top-k。n<k的欠填按`min(k,eligible)`分母计入损失。
- 原生hnswlib宽度至少为返回数，故四方案及每个n统一按`max(k,efSearch)`执行；同时保存请求值与有效值两列。本轮两格均满足`efSearch≥k`，有效值等于请求值。
- 同格比较以本方案实际query-average Recall为R*，在NodeANN同格已测n中取达到R*的最快点，报告QPS比、Recall超额、最小Recall、欠填与返回数。达不到R*就标未达标，不插值、不把低召回快点当同质量收益，也不称可部署n选择器。
- 正常与计数执行分离，验证ID一致；正常执行不装native计数hook、不写trace。所有不合格查询距离为0，未知native邻接次数留空。
- 当前按整批查询测QPS：预热一批，每计时批遍历非空query16遍，10个批次交错随机顺序测配置；用批次耗时上中位数计算吞吐。包含搜索/归并/结果消费，不含scope准备、GT、构建。
- 空查询保留审计行、不计非空Recall/QPS；保存全部批次耗时和min/max，不冒称统计置信区间或端到端吞吐。
- 各点保留mean/min Recall、返回数、实际L2调用、item/owner资格判断和可测邻接条目；不能以平均Recall替代逐query达标，也不把总时间除以距离数当L2核时间。
- 当前未统一服务驻留字节；独立进程峰值RSS包括GT/构建/原生图副本和导出表示。后续层次与剪枝比较必须另核全层容量、元数据、向量副本及scratch。
- 原24/120全变体驱动仍可显式运行，但本轮未把它在M64重跑的全部变体作为已完成成果。

## 10. 复现与证据

```bash
bash autoresearch.sh
# 或直接运行同一个入口：
bash researches/nested-attr-vector-search/2026-11-02-exps/experiments/run_dag_pareto.sh
```

工作输出为`work/dag-two-cases-m64/`，由`DAG_PARETO_OUT`改目录。全部正式小型CSV/JSON/图、真实编译依赖、源码指纹及资源日志见[两格固定比较报告](dag-two-cases-report.md)。此前[M16验证报告](dag-validation-report.md)与九格/跨ef探索数据保留为历史，不提交向量、索引、二进制。

行为回归覆盖直属/递归 GT、共享后代、空属性、后代 portal 不得代替直属入口、普通/计数/CSR/visited 结果一致、剪枝度约束、无法通过不合格桥导航及 native hook 恢复。有限测试不提供普遍连通性保证。

## 11. 验收边界与后续

本轮验证的是可运行方案、查询语义和已测性能，不声称胜过 NodeANN 或完整 ACORN。历史 24 查询共库原型观察见 [历史组合报告](dag-composition-report.md)，不能替代当前独立库、递归/非递归的正式结果。

单 owner、OR、随机属性绑定、小规模 query、未统一常驻内存仍是限制。剪枝、平面跳转、bitmap 均保留为实验因素，不能先宣布默认优化成功。下一步若测保留局部高层，应仍维持一张图和共享搜索前沿；不把 NodeANN 的多图 top-k 归并包装成我们的新算法。新颖性尚未充分排查。

新增分层、入口、b与预算剪枝设计（原设计文档已随方案清理删除）明确哪些边保持direct/recursive资格、单真实入口与虚拟源的区别、剩余预算状态、OR替代条件和一般多父DAG的度预算反例；目前仅有限语义/内存探针已运行，分层ANN尚未实现，不能混入当前曲线。
