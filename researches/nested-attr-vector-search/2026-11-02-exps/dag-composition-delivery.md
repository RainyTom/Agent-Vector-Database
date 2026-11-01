# 交付文档：属性内 HNSW + DAG 门户合并 + 单队列搜索

本文描述当前实现的完整方案：从原始数据形态开始，到属性内建图、DAG 合并、查询算法、参数、实测结论与验证。所有数字均来自本仓库实际运行（证据见 §11），推测与未验证项单独标注。

---

## 0. 从原始数据开始：一开始没有任何节点

**原始输入是三个互不相关的东西，没有任何图结构、没有节点、没有边。**

| 输入 | 形态 | 规模 | SHA256 |
|---|---|---|---|
| `datasets/sift-128-euclidean.hdf5` | 1,000,000 × 128 的 uint8 描述子，按 ann-benchmarks 镜像以 float32 重封装 | 516 MB | base `21f66e29…`，query `f7fc9be1…` |
| `CCS 2012.html` | ACM 计算分类系统本体的一个 HTML 页面（纯文本层级列表） | — | `bf037ffd…` |
| SciHTC labels（train/dev/test 三个 csv） | `paper_id → 分类标签` | — | 三个文件各自 SHA |

这三样东西本身：

- **向量只是浮点数组**，既不知道自己属于谁，也不知道彼此关系；
- **本体只是字符串树/图**，没有任何向量；
- **标签只是一张多对一的表**，把论文指到一个分类。

**所以：一开始没有节点。** 第一个图节点出现在 §3 建 hnswlib 图时；第一组跨属性边出现在 §5 的合并阶段。中间必须先把这三者拼成"结构 + 向量"的联合形态。

### 0.1 第一步：把本体解析成属性 DAG

从 CCS HTML 抽出 `child → parent` 关系，得到一个**多父有向无环图**：

| 量 | 值 |
|---|---:|
| 属性数 | 1,929 |
| DAG 边数 | 2,040 |
| 多父属性数（≥2 个父） | 103 |

它不是树：103 个属性有多个父，因此同一个后代子树会被多条路径共享。后续所有"下闭集"计算都必须按集合去重，不能按路径枚举。

### 0.2 第二步：把论文挂到属性上

SciHTC 标签给出 `(论文, 直接属性)`，每个 item 只挂**一个**直接属性（终叶标签）：

| 量 | 值 |
|---|---:|
| item 数（= `objects.txt` 行数） | 168,937 |
| 有直属 item 的属性数 | 768 |
| 直属为空但子树有 item 的属性数 | 66 |
| **子树完全为空的属性数** | **1,095（占 57%）** |
| 每属性直属 item 数 | 中位 66、均值 220、p90 492、最大 9,823 |

⚠️ 1,095 个属性的整个子树没有任何 item（本体的空分支远多于能挂上论文的分支）。它们没有本地图、portal 也为空，只能当导航结构里的一环；查询落在这些分支上就是空结果（24 条取样 query 里有 1 条如此）。做"属性内建图"时这 57% 是纯开销。

向量侧只取 SIFT base 的**前 168,937 行**与该表逐行对齐——item 数由标注数决定，不是由 SIFT 决定。

### 0.3 第三步：生成 workload 三文件

| 文件 | 格式 | 内容 |
|---|---|---|
| `objects.txt` | 每行一个整数 | 第 i 行 = 第 i 个 item 的直属属性 id |
| `edges.tsv` | `child parent` | 本体边 |
| `queries.tsv` | `k id1 … idk` | 一条查询的 scope 属性列表（单属性或 antichain 多属性 OR） |

规模：1,234 条查询，其中 400 条是 antichain 多属性 OR；scope 平均 368.95 个 item（min 0，max 168,937）。`meta.json` 记录 counts 与所有输入的 SHA。

### 0.4 第四步：读入并派生结构（`load_workload`）

```cpp
while (objects >> c) w.owner.push_back(c);          // 每个 item 的直属属性
while (edges >> c >> p) links.emplace_back(p, c);   // 注意顺序：先 child 后 parent
// Kahn 拓扑排序；存在环则直接失败
std::shuffle(w.owner.begin(), w.owner.end(), std::mt19937(20261101));   // 关键一步
for (Id i = 0; i < w.owner.size(); ++i) w.members[w.owner[i]].push_back(i);  // 每属性直属 item
for (Id start = 0; start < nc; ++start) { /* DFS 求下闭集，按集合去重 */ }
```

四点必须说明：

1. **拓扑序**用于后续所有"子先父后"的遍历；有环直接 `require` 失败。
2. **`owner` 被整体打乱**（种子 20261101），只保留"每个属性的 item 数量"。
3. **`members[c]`** = 该属性的直属 item 列表，是 §3 建图的输入。
4. **`desc[c]`** = 该属性的下闭集（属性 id 集合，含自身），是查询时算合格集的输入。

### 0.5 为什么要打乱 owner：以及它的代价

原始 `objects.txt` 按属性连续排列（`0,0,0,…,1,1,…`），而 SIFT1M 按源图像排序。两者对齐会制造**假几何相关**：实测属性内距离比 `ρ=0.2027`，置换零模型 `0.1249`——但随机对随机只有 `lift=1.013`，说明 1.62 的 lift 是顺序伪影。打乱 owner 就是为了消掉它。

**代价（必须随本文一起读）**：打乱后**属性层级不携带任何几何信息**（lift≈1）。因此本负载测的是**结构机制**——连通性、入口选择、预算分配、资格判断成本——而**不是**"属性内聚类让 ANN 更容易"。任何"属性内图更准"的结论在这份数据上都不可得。

### 0.6 第五步：scope、入口与 GT（查询侧的准备，一次性）

```cpp
for (Id c : scope) for (Id d : w.desc[c]) allowed[d] = 1;   // 合格属性位图
for (Id c : w.topo) if (allowed[c]) {                       // 入口属性 = 无合格祖先
    if (!ancestor[c]) roots.push_back(c);
    for (Id d : w.children[c]) ancestor[d] = 1;
}
truth = 对 { u | allowed[owner[u]] } 做精确 FP32 L2 的 top-10;
eligible = |{ u | allowed[owner[u]] }|;
```

- 基准只取 1,234 条中的 **24 条**，按 `i·(n-1)/23` **均匀取样**（不是随机抽样）；每条固定绑定一条 SIFT query。
- 空结果 query 从召回与延迟均值中排除（因此报的是 23 条非空查询的均值）。
- GT 用与 ANN 完全相同的 `RawL2Space`，只算一次，不计入延迟。

**到这里，我们已经有了"属性 DAG + 每 item 的归属 + 向量 + 查询/GT"，但仍然没有一张图。** 下一步（§3）才第一次产生节点和边。

---

## 1. 问题与术语

- **属性**：DAG 上的一个节点（原实现代码里叫 `concept`/`directory`，本文统一称属性）。属性构成一个多父 DAG，允许共享后代。
- **直属 item**：`owner[item] = 属性`，即该 item 直接挂在哪个属性下。本负载共 168,937 个 item，1,929 个属性，2,040 条 DAG 边，其中 **768 个属性有直属 item**。
- **scope（查询范围）**：一个或多个属性的**下闭集**（自身 + 全部后代）。合格 item = 直属属性落在下闭集内的 item。
- **合格集**：`eligible = { item | owner[item] ∈ scope }`。搜索只能返回其中的 item。
- **入口属性（roots）**：合格集合中**没有合格祖先**的属性，即下闭集的最小元素。

DAG 结构不计入本文的搜索创新，只作为"属性之间的包含关系"使用。

---

## 2. 总体结构

```
item 向量 ──► 每个属性一个 hnswlib 图（M=16, efC=200）
                    │  只取 level-0 边，双向
                    ▼
             合并邻接（单层，无层次）  ◄── 跨属性连接边（MST / portal 最近邻 / 分散桥 / 跳转）
                    │
                    ▼
             查询：从 roots 的 portal 进图，单队列 best-first，只走合格 item
```

三个关键取舍：

1. **属性内部保留完整本地边，一条不减**（"保边合并"）；跨属性只加边、不改本地边。
2. **合并图是扁平单层**：本地图的 level-1+ 在合并时被丢弃。
3. **跨属性边只落在 portal 上**，portal 是每个属性对外的 ≤8 个代表节点。

---

## 3. 属性内部建图

### 3.1 代码

```cpp
// 每个有直属 item 的属性 c
m.local[c] = std::make_unique<Graph>(&l2, members.size(), /*M=*/16, /*efC=*/200, /*seed=*/100 + c);
for (Id item : members) m.local[c]->addPoint(base.at(item), item);   // item id 升序

// 只抽 level-0，双向写入合并邻接
for (size_t i = 0; i < members.size(); ++i) {
    Id u = g->getExternalLabel(i);
    auto* list = g->get_linklist0(i);
    auto* ids  = reinterpret_cast<hnswlib::tableint*>(list + 1);
    for (size_t j = 0; j < g->getListCount(list); ++j) {
        Id v = g->getExternalLabel(ids[j]);
        m.edges[u].push_back(v);
        m.edges[v].push_back(u);      // 反向补齐 → 无向图
    }
}
```

### 3.2 算法（hnswlib `addPoint` 原生行为，未改动）

```
插入 q（第 i 个）：
  l = floor(-ln(U) / ln(M))                      // 指数跳层，M=16
  若图为空 → 设为 entrypoint
  for level = max_level .. l+1:  ef=1 贪心下降
  for level = min(l, max_level) .. 0:
      W = searchLayer(q, ep, ef=200, level)      // 候选堆 + 结果堆
      N = selectNeighborsHeuristic(W, M)         // 多样性剪枝：若存在已选 s 使 dist(c,s) < dist(c,q) 则丢弃 c
      建立 q↔n 双向边；若 n 度数超限（level0 = 2M = 32，其余 = M）用同一启发式裁回
  if l > max_level → 更新 entrypoint
```

⚠️ 建图的输入是 §0.4 打乱后的 `members[c]`：**同一属性内的向量之间没有语义相关性**（lift≈1）。本地图因此只是"随机向量上的 HNSW"，它的连通性由 SIFT 的几何决定、与属性语义无关。这不影响机制测量，但任何"属性内图质量"的说法在这份数据上都不成立。

### 3.3 参数与规模

| 参数 | 值 | 说明 |
|---|---|---|
| M | 16 | level-0 上限 32 |
| efConstruction | 200 | |
| 随机种子 | `100 + 属性 id` | 每属性独立、可复现 |
| 插入顺序 | item id 升序，未打乱 | ⚠️ 保留原始数据顺序偏置；全局图那边是显式打乱的 |
| 距离 | `RawL2Space`，FP32 欧氏 | 与 GT 一致 |

属性内 item 数分布（直属只算，不含继承）：

| item 数 | 属性数 | item 总数 |
|---|---:|---:|
| 3–10 | 78 | 575 |
| 11–50 | 262 | 7,253 |
| 51–200 | 228 | 24,156 |
| 201–1,000 | 170 | 72,717 |
| >1,000 | 30 | 64,236 |

中位数 66、均值 220、p90 492、最大 9,823。**30 个大属性决定实际图质量**，78 个小属性基本退化为全连接表。

### 3.4 成本

- 建图时间包含在总构建 7.66 s 内。
- 每个属性图自己 `memcpy` 一份向量：额外 ≈ 86 MB（168,937 × 128 × 4）。这部分驻留成本不计入查询延迟对比。

---

## 4. Portal（属性对外的 ≤8 个接口）

### 4.1 定义

一个属性的 portal = 其**整棵子树**的代表节点集，≤8 个。它只决定跨属性边落在哪里、查询从哪里进入，不删 item、不改本地边。

### 4.2 算法：最远点采样

```cpp
static std::vector<Id> representatives_k(std::vector<Id> candidates, const Vectors& base,
                                         RawL2Space& l2, size_t k) {
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.size() <= k || k == 0) return candidates;    // 不足 8 个全留

    std::vector<Id> selected{candidates.front()};               // 起点：id 最小候选
    std::vector<float> nearest(candidates.size(), INFINITY);
    std::vector<char> taken(candidates.size());
    taken[0] = 1;
    while (selected.size() < k) {
        size_t best = none;
        for (size_t i = 0; i < candidates.size(); ++i) {
            nearest[i] = std::min(nearest[i], l2.dist(base.at(candidates[i]), base.at(selected.back())));
            if (taken[i]) continue;
            if (best == none || nearest[i] > nearest[best]) best = i;   // 严格 > → 平局取小 id
        }
        taken[best] = 1;
        selected.push_back(candidates[best]);
    }
    return selected;
}
```

即贪心 k-center（Gonzalez）：每轮选**离已选集合最远**的候选，保证最大覆盖半径 ≤ 最优解的 2 倍。

### 4.3 确定性与复杂度

- 确定性：起点固定（最小 id）、平局固定（严格 `>`）、候选集是子树纯函数 → 与遍历顺序、兄弟顺序无关（实测：升序/降序建出逐边相同的图）。
- 单次采样 `(|C| - 1) × k` 次 L2。`|C| = 8 + 8 × 子属性数`：2 个子属性 → 24 候选 → 168 次距离调用。
- 全库候选约 `8×768 + 8×2040 ≈ 2.2 万`，按每轮 `k·|C|` 粗估约 **1.6×10⁵ 次 L2**（估算值，未单独计时），占构建时间很小——大头是分散桥。

### 4.4 已知局限

Portal 优化的是**几何覆盖**，不是"离 query 近"。8 个点概括 9,823 个 item 时，接口之间的空隙无人负责 —— 这是大 scope 召回上不去的结构性来源（§9.4）。

---

## 5. DAG 合并算法

共四趟：本地边入图（§3）→ 自底向上算 portal → 自顶向下插连接边 → 跳转边。

### 5.1 第 2 趟：portal（逆拓扑序，子先父后）

```cpp
for (auto it = w.topo.rbegin(); it != w.topo.rend(); ++it) {
    Id c = *it;
    std::vector<Id> pool;
    if (!own[c].empty()) pool.insert(pool.end(), own[c].begin(), own[c].end());       // 直属代表 ≤8
    for (Id child : w.children[c])
        if (!m.portals[child].empty())
            pool.insert(pool.end(), m.portals[child].begin(), m.portals[child].end());
    m.portals[c] = representatives_k(std::move(pool), base, l2, /*k=*/8);
}
```

**这一步不写边**，所以 portal 与合并顺序完全解耦。

### 5.2 第 3 趟：逐属性插连接边

对属性 `c`，把输入切成**组**：`[直属代表(若非空)] + [每个有 portal 的子属性的 portal 集]`。

```cpp
std::vector<std::vector<Id>> groups;
if (!own[c].empty()) groups.push_back(own[c]);
for (Id child : w.children[c])
    if (!m.portals[child].empty()) groups.push_back(m.portals[child]);
if (groups.empty()) continue;
```

三件事：

```cpp
// (a) 组间最小生成树：只连 |groups|-1 条边
for (size_t step = 0; step < groups.size(); ++step) {
    ... 选 best[] 最小的未用组 g；if (step) connect(endpoints[g].first, endpoints[g].second);
    ... 用 groups[g] 更新其余组的 best/端点
}

// (b) 每个 portal 连它在"其他每个组"里的最近 1 个 portal（connector_k=1）
for (size_t g = 0; g < groups.size(); ++g)
    for (size_t h = 0; h < groups.size(); ++h) if (g != h)
        for (Id u : groups[g]) {
            nearest = 按 dist(u, v) 排序的 groups[h];
            connect(u, nearest[0].second);
        }

// (c) 分散桥：source 的每个直属 item 向每个组连最近点
std::vector<Id> sources{c};
sources.insert(sources.end(), w.children[c].begin(), w.children[c].end());
for (Id source : sources)
    for (Id u : w.members[source])
        for (const auto& group : groups) {
            Pair nearest = min over v in group with owner[v] != source;
            m.edges[u].push_back(nearest.second);                  // 正向必加
            reverse_bridges[nearest.second].emplace(dist, u);      // 反向进堆，只留最近 16 个
        }
```

最后刷出反向桥（每目标最多 16 个最近来源），`normalize()` 排序去重。

### 5.3 第 4 趟：跳转边

```cpp
for (Id c : w.topo)
    for (Id u : own[c])            // 直属代表
        for (Id v : m.portals[c])  // 本属性 + 全子树代表
            connect(u, v);
```

### 5.4 每属性边数

设 `n` = 直属 item 数，`g` = 组数，`p = 8`：

| 来源 | 无向对数 |
|---|---|
| MST | `g - 1` |
| portal 最近邻 | `g(g-1)·p` |
| 分散桥正向 | `(n + Σ n_child) · (g-1)` |
| 反向桥 | 每目标 ≤16，距离优先 |
| 跳转 | `|own[c]| · |portals[c]|` |

### 5.5 实测产出

| 边类型 | 有向边数 | 占比 |
|---|---:|---:|
| 本地 level-0（双向） | 2,374,436 | 58.4% |
| connector（MST + portal 最近邻 + 分散桥 + 反向） | 1,652,935 | 40.7% |
| 跳转 | 37,901 | 0.9% |
| 合计 / 平均出度 | 4,065,272 / **24.06** | — |

构建耗时 **7.66 s**。

### 5.6 边界情况（均被不变量测试覆盖）

- **空属性**：不建图、无直属代表；靠子属性 portal 得到自己的 portal，仍可作导航中转。
- **多父 + 共享后代**：后代 item 的边由每个父各插一次，去重后只留一份；`connect(u,u)` 被拦。
- **零距离连接**：共享后代使同一 item 出现在两个组时，MST/最近邻会取距离 0 的对，只拦自环不拦 0 距离。
- **单子属性**：g=2，MST 贡献 1 条边，退化为"与子属性根相连"。

---

## 6. 搜索算法

### 6.1 代码

```cpp
template<class Qualification>
static std::vector<Id> search_counted(const Merged& m, const Vectors& base, RawL2Space& l2,
    const float* query, const std::vector<Id>& roots, size_t ef, Scratch& scratch,
    Qualification qualifies, SearchStats& stats) {
    ++scratch.epoch;
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;  // 小顶堆
    std::priority_queue<Pair> best;                                            // 大顶堆

    auto offer = [&](Id u) {
        if (scratch.seen[u] == scratch.epoch) { ++stats.duplicate_edges; return; }
        if (!qualifies(u)) { ++stats.rejected_edges; return; }      // 不合格：不入队、不扩展
        scratch.seen[u] = scratch.epoch;
        Pair p{l2.dist(query, base.at(u)), u}; ++stats.distances;   // 只有合格才算距离
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };

    for (Id c : roots) for (Id u : m.portals[c]) offer(u);          // 入口：根属性 portal

    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;             // 终止
        ++stats.expansions;
        for (Id v : m.edges[p.second]) offer(v);                    // 扫全邻接，不剪
    }
    while (best.size() > 10) best.pop();
    ...
}
```

### 6.2 查询侧流程

```cpp
for (Id c : w.scopes[...]) for (Id d : w.desc[c]) allowed[d] = 1;   // 下闭集位图
for (Id c : w.topo) if (allowed[c]) {                               // 入口属性 = 无合格祖先
    if (!ancestor[c]) roots.push_back(c);
    for (Id d : w.children[c]) ancestor[d] = 1;
}
search_counted(..., roots, std::min(ef, eligible_count), ...);
```

### 6.3 要点

1. **单层入口**：没有高层可下降，种子 = 所有根属性的 portal（通常 8–32 个点）。
2. **诱导子图遍历**：不合格节点不入队 → 跨属性桥只在两端都合格时有效。scope 变化会显著改变有效连通性。
3. **唯一终止条件**：结果堆满 `ef` 且前沿最小距离劣于结果堆最差；或前沿耗尽。
4. **ef 是唯一质量旋钮**（`k=10` 固定），既是结果堆容量也是前沿宽度。
5. **visited**：`seen[u] == epoch` 的 O(1) 清零数组（675 KB）；可换 21 KB 位图（§9.5）。
6. 无 hop 上限、无扫描回退、无 GT 驱动入口。

### 6.4 一次查询的开销（ef=512，23 条非空查询均值）

| 计数 | 值 |
|---|---:|
| 距离调用 | 723 |
| 扩展节点 | 261 |
| 资格拒绝的边 | 1,218 |
| 重复边 | 3,786 |
| 合计边检查 | 5,727 |
| 延迟 | 93.1 µs（dense visited：81.9 µs） |

**每个距离要付 7.9 次边检查**，只有 12.6% 的邻接扫描变成距离计算。

---

## 7. 参数表

| 参数 | 默认 | 实测结论 |
|---|---|---|
| 属性内 M | 16 | 未扫描 |
| 属性内 efConstruction | 200 | 未扫描 |
| portal 数 `p` | 8 | 4/16/32 无稳定收益，8 是合理点 |
| connector_k | 1 | 2/3 无收益 |
| 反向桥上限 | 16（距离优先） | 关闭后召回 .9000→必须保留 |
| 分散桥 | 开 | 关闭后延迟 −37% 但召回 −6 点 |
| 出度剪枝 | **关闭**（建议 16–24） | prune24：R .9696 / 86.9 µs |
| 跳转边 | 开（37,901） | jump0/1/2/3 无可测差异 → 可关 |
| visited | epoch 数组（建议位图） | 位图配对 −7.6% |
| 查询 ef | 512 | 见 §9.2 |

---

## 8. 统一 ef 口径（重要）

所有臂用**同一个 ef 数字**，含义为**单查询搜索预算**：

- 合并图 / ACORN / 前置过滤：直接作为搜索宽度。
- NodeANN：把 ef 分摊到各合格小图，`ef_local = max(10, ef / G)`，G = 合格小图数。
- 同一表格另列旧的 per-graph 口径作对照，所有臂都记录实际距离调用数，残余语义差异可见。

---

## 9. 实测结论

固定 SIFT×CCS，24 条 query（23 条非空），预热 1 次 + 计时 11 次取中位数，`taskset -c 3`，GT 与 scope 解析不计时。

### 9.1 噪声带（必须知道）

`baseline`、`order_size_asc`、`order_size_desc` 构建**逐边相同**的图，ef=512 延迟分别 93.1 / 91.3 / 80.6 µs。**单次运行均值噪声 ±8–15%**，小于该幅度不解释；因此主要依据**同 query 配对中位数比值**。

### 9.2 合并图的召回-延迟曲线

| ef | R@10 | µs | 距离/查询 |
|---:|---:|---:|---:|
| 32 | 0.8391 | 5.4 | 165 |
| 128 | 0.9130 | 21.4 | 375 |
| 512 | 0.9565 | 93.1 | 723 |
| 2,048 | 0.9957 | 220.5 | 1,263 |
| 168,937 | 1.0000 | 691.2 | 2,000 |

### 9.3 组合顺序：当前不可观测

- 距离优先准入 → 与顺序无关（逐边相同的图）。
- 改成先到先得后顺序可观测，但 ef=512 召回 **.9565 → .9391**（升序/降序同）。

**不调顺序。**

### 9.4 比 NodeANN 慢：只发生在大 scope

| 合格 item | query 数 | 合并图 µs / R | NodeANN µs / R |
|---|---:|---:|---:|
| <50 | 8 | **1.5** / 1.000 | 2.1 / 1.000 |
| 50–500 | 7 | 27.4 / 1.000 | **4.7** / 1.000 |
| 500–5000 | 6 | 225.3 / 0.983 | **23.2** / 0.983 |
| ≥5000 | 2 | 318.8 / **0.550** | **215.0** / 1.000 |

配对中位比值（nodeann/merged）：ef=32 为 1.318（23 条里只有 2 条更快），ef=512 为 1.035 —— **均值差被少数大 scope 主导**。

机制：合并图是一次扁平搜索，`ef` 是全局候选表宽度；合格集变大时候选表成为瓶颈，19,596 个合格 item 只访问约 3,839 个（20%），召回掉到 .50。NodeANN 每个合格小图各做一次有界搜索，工作随图数而非合格 item 数增长。

### 9.5 执行层消融（同 query 配对中位比值，图不变）

| 变体 | 比值 | 更快 query | 备注 |
|---|---:|---:|---|
| CSR 扁平邻接 | 1.335 | 2/23 | **更慢，不采用** |
| CSR + 边内联 owner | ~1.03 | — | 无收益 |
| item bitmap 资格 | 0.985 | 13/23 | −12% 均值，但 +96 µs 构建/查询 |
| **21 KB visited 位图** | **0.924** | 19/23 | 采用 |
| **prune16 + 位图** | **0.899** | 18/23 | 当前最优执行组合，ef=32 时 23/23 更快（0.839） |

### 9.6 bitmap 属性判断

| 实现 | 构建 | ns/check |
|---|---:|---:|
| char 数组（当前） | 0 | 0.45–0.56 |
| owner 位图（1,929 位） | 0 | 0.63–0.78（**更慢**） |
| item 位图（21 KB） | 72–99 µs/query | 0.36–0.49 |
| 多标签：扫标签 vs item 位图 | — | 2.548 → **0.308（8.3×）** |

结论：单 owner 谓词不要换位图；多标签/合取谓词才划算；跨查询复用同一 scope 时可摊薄构建。

---

## 10. 正确性验证（已实际运行通过）

`dag_composition_test.cpp`，`PASS baseline equivalence, order-invariant portals, nested highway modes, degree cap/connectivity, counted search, qualification kernels`：

1. **基线等价**：参数化构建器在 baseline 参数下与已提交 `build()` **逐边一致**（含三阶段边计数）。
2. **portal 与顺序无关**：升序/降序组合的 portal 集合相同。
3. **跳转模式嵌套**：jump0 ⊆ jump1 ⊆ jump2 ⊆ jump3。
4. **剪枝不变量**：出度 ≤ 上限、有序、无重复、无自环，且不断开任何范围内 item。
5. **FCFS 顺序可观测**：证明"顺序敏感"分支确实改变了图。
6. **dense visited 等价**：与 epoch 数组的结果、距离数、重复边数完全一致。
7. **搜索等价**：计数版搜索与已提交搜索返回一致，且从不返回不合格 item。
8. **资格 kernel 一致**：三种实现在全部属性与 item 上一致。

---

## 11. 复现

```bash
# 构建 + 运行（17 个组合变体 + 11 臂统一 ef 对比 + kernel 微基准）
bash researches/nested-attr-vector-search/2026-11-02-exps/experiments/run_dag_composition.sh

# 汇总成 markdown 表
python3 researches/nested-attr-vector-search/2026-11-02-exps/experiments/analyze_dag_composition.py
```

- 输入：`work/packed/sift-fvecs-run1/sift_base.fvecs`、`sift_query.fvecs`、`work/dag-scope/workload-run1/`（SHA256 见 `data/dag-composition-input-sha256.txt`）。
- 输出：`data/dag-composition-{variants,arms,kernels,scope}.csv`、`data/dag-composition-run.log`。
- hnswlib 固定在 `ca426729609b3221563047ceb269cc1213e0d376`，脚本会校验。
- 实验报告：[dag-composition-report.md](dag-composition-report.md)。

---

## 12. 局限与未验证

1. **仅一个 fixture**：24 条 query（从 1,234 条均匀取样，非随机）、单一 SIFT 采样、OR 语义 scope。未做选择率分层与多次独立构建。
2. **owner 绑定是打乱的**，属性层级与向量几何无关（实测 lift≈1.013）。本负载能测结构机制，**不能**测"属性内聚类是否让 ANN 更容易"；真实语义绑定下的曲线未知。
3. **大 scope 召回 .55 是质量缺陷**，不是延迟问题；任何均值延迟对比必须同时给召回。
4. **NodeANN 侧边检查次数未测**（需改 hnswlib 才能计数），标为未测而非 0。
5. **内存预算未对齐**：本地图各自保留一份向量副本（≈86 MB），合并邻接又一份。
6. **CSR 反而更慢的原因未定位**：只测到结果（配对 1.335），未做进一步归因。
7. **跳转边结论只覆盖本负载**：合格集是下闭集、portal 已落在目标子树内，跨层跳转没有可抄的近路；不代表在其他分布上无用。
8. **未做新颖性排查**：本文是工程方案与实测，不主张贡献。

## 13. 下一步（最小实验）

1. **隔离图与执行器**：图不变，只把执行器换成"每合格属性一次有界 searchKnn + 归并"，与 NodeANN 共用同一批本地图，量化"图结构 vs 执行器"各自贡献。
2. **恢复层次**：合并时保留本地 level-1+，把 portal 作为层间跳板，测大 scope 的召回-距离曲线。
3. **出度剪枝默认开启**（16–24）+ visited 位图，作为下一版默认配置。
