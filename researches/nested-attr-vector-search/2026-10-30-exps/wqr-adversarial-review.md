## Summary

该机制可以实现为**资格精确、路由与局部搜索近似**的 filtered ANN；但 **4 个 farthest-first portals 不保证保留最近合法块**，固定取 `P=L` 个 portals 也不保证得到 `L` 个不同块。两者必须分开：前者是已知近似边界，后者是需要冻结的执行语义。

必须确定的修正是：portal 去重后的块预算规则、删除后的 portal 覆盖不变量、callback 只控制结果资格的语义、Euler MOVE 后缓存失效、内部节点直接归属、完整布尔/tag mask、预算不足与空结果的停止/报告规则。不需要因此重设计整套机制。

**证据边界：**本轮只读，没有运行作者代码、tests、build、lint。`dynamic_triad.cpp` 是现有 A/B/C replay，不是拟议 witness-block 实现。其目标 hnswlib 子模块目录本地为空；我定位了另一个本地 hnswlib 副本，并进一步读取构建脚本指定提交的原文，避免以不同版本冒充目标依赖。

## Files

| 文件 | 内容与用途 |
|---|---|
| `codes/directory-aware-vector-attr-search/src/dynamic_triad.cpp:94-110` | Item、Query、普通属性 AND/OR 与四种 tag 语义 |
| 同文件 `:228-265` | 目录祖先判断、子树枚举、内部节点直接归属的精确定义 |
| 同文件 `:267-353` | 图创建、删除标记、同 immutable-vector ID 恢复、局部压实与 membership 维护 |
| 同文件 `:354-427` | 对象 M、HNSW 搜索 wrapper、external-label callback |
| 同文件 `:486-568` | A/B/C scope 构造、局部/全局搜索、合并 |
| 同文件 `:570-635` | 搜索完成后独立 oracle、空 GT recall 约定、错误检查 |
| 同文件 `:637-691` | R：禁止移动根、禁止环、维护 parent/children 和 B/C 元数据 |
| 同文件 `:713-764` | V/T/M/B/R/D/I 事件语义 |
| `codes/directory-aware-vector-attr-search/src/raw_l2_space.h:1-41` | 使用 hnswlib raw FP32 **squared-L2**；不是欧氏距离开方值 |
| `codes/directory-aware-vector-attr-search/tests/test_dynamic_triad.py:23-145` | 包含内部节点直接绑定、空 tag、AND/OR、M/R、删除恢复的已有测试定义；未执行 |
| `codes/directory-aware-vector-attr-search/build_dynamic_triad.sh:5-18` | 固定 hnswlib revision 与真实 include 路径 |
| `codes/vector-join-reproduction/build/_deps/hnswlib-src/hnswlib/hnswalg.h:334-469` | 本地可读副本的 filtered traversal；不能据此宣称就是目标子模块 |
| [目标提交 hnswalg.h](https://github.com/nmslib/hnswlib/blob/ca426729609b3221563047ceb269cc1213e0d376/hnswlib/hnswalg.h#L318-L440) | 已直接读取目标 revision 的 callback、候选队列、停止条件原文 |
| [同提交删除/替换代码](https://github.com/nmslib/hnswlib/blob/ca426729609b3221563047ceb269cc1213e0d376/hnswlib/hnswalg.h#L930-L1085) | tombstone、unmark、label 替换及向量更新 |
| `researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/literature-mechanisms-review.md` | 文献边界；本报告不新增首次性主张 |

## Architecture

现有 triad：A 为目录直接归属的局部图，B/C 为全局图加目录 posting；oracle 在搜索结束后独立执行。拟议机制不能直接复用 A 的“目录节点就是 ANN 分区”维护：新机制的**几何块与 owner 必须独立**，M 只改 owner posting，R 只改目录拓扑；只有向量插删才影响块图/portal。

## Report

### 1. Portal 不能保证最近合法块：有限、可手算反例

取二维 FP32，容量 `B=5`，`r=4`，query `q=(0,0)`，`k=L=1`。

块 A 按 slot 顺序：

```text
a0=(10,0), a1=(-10,0), a2=(0,10), a3=(0,-10), a4=(0,0)
```

确定 farthest-first 的第一点为最小 live slot，之后取最大“到已选集合最近距离”的点，平局按 slot。

- 先选 `a0`，再选 `a1`。
- 接着选两个纵轴端点。
- 中心 `a4` 到任一端点的 squared-L2 为 `100`；未选纵轴端点到已选横轴端点最近 squared-L2 为 `200`，所以四个 portal 恰为四个端点。
- 设置 A 中**只有中心合法**；端点都不合法。

块 B：

```text
(1,0), (1,0.01), (1.01,0), (1.01,0.01), (1.005,0.005)
```

B 的全部对象合法，任取四个 portal 到 q 的距离都小于 A 的 portal 距离。

所以：

- A 的完整资格 mask 非空，A 的四个 portal 符合 block callback。
- 即使 portal 检索是**精确搜索**，先选中的仍是 B。
- `L=1` 仅搜 B，返回距离约为 `1` 的对象，漏掉 A 中距离 `0` 的合法最近邻。

这不是 HNSW 误差、不是 marginal OR、不是 callback 错误，而是代表点排序本身的损失。将 A 的半径放大后，对“零最优距离”的近似比不存在有限保证；把中心改为靠近原点的非零小距离也能使比值任意大。

**确定修正：**

1. 冻结 farthest-first seed、平局规则和小块规则 `r_b=min(4,n_b)`。
2. 明文保证仅为“portal routing heuristic”，不得宣称保持 nearest 合法块，也不能以 portal 距离对未探测块作安全剪枝。
3. 报告路由覆盖损失与局部 HNSW 损失，而不是将全部 recall 损失归于 local ef。

无需换机制；这是合法的 ANN 近似边界。

### 2. 固定返回 P 个 portals，distinct 块数量可能严重不足

令每块最多 4 个 live portals。

取 `L=P=4`，q 为原点，块 A 恰有四个点：

```text
(0.01,0), (0.02,0), (0.03,0), (0.04,0)
```

另有三个合法块，各自 portal 均更远。即使 portal 精确检索，返回的四个 portal 全属于 A，去重后只有 **1 块**。

一般，返回恰好 P 个有效且唯一 portal label 时，只能保证：

\[
\#\mathrm{distinct\ blocks}\ge \left\lceil P/4\right\rceil.
\]

要仅凭每块最多 4 个的 multiplicity 保证至少 L 块，需要：

\[
P\ge 4(L-1)+1.
\]

**可直接冻结、不引入重试的新规则：**

- 一次调用 portal `searchKnn(q,P,block_filter)`，设 `P=min(4L,N_{\text{live portals}})`。
- 实际 portal ef 为 `max(ef_portal,P)`。
- 将返回结果按 `(squared_distance, portal_label)` 升序排序；依次取首次出现的不同块，最多 L 块。
- 返回不足 L 块时停止并记录实际块数；不默默全局枚举、不 GT 驱动重试。

这里的计数保证以**确实返回了足量有效 portals**为条件，不是对 HNSW 可达性/召回的保证。

不要让 callback 使用“这个块之前见过，所以之后 portal 返回 false”的状态性去重：callback 调用顺序不是最终距离顺序，候选可被 heap 淘汰，被首次接受的 portal 也未必留在最终结果中。这会把临时搜索状态误当最终块选择。

### 3. hnswlib callback：只过滤结果，不是过滤导航图

目标 revision 原文显示：

- 上层 greedy descent 不调用资格 callback。
- 底层先将候选放入导航 `candidate_set`，再检查删除状态和 callback，决定是否加入 `top_candidates`。
- callback 接收 `getExternalLabel(...)`，不是 internal graph ID。
- tombstone 在结果资格判断中短路排除，但其邻接关系仍可用于导航。
- 默认底层搜索在 filtered 情况使用 `searchBaseLayerST<false>`。

这**正好支持**“portal 本人不合法但所属块有共同资格见证，仍可返回；任何不合法 portal 仍可能用于导航”。

应冻结两个不同 functor：

```text
PortalAllowed(label):
    portal metadata identifies current live block
    AND cached ExactMask(block,query) != ∅

LocalAllowed(label):
    label resolves to current slot
    AND ExactMask(block,query)[slot] == 1
```

两者均不得把 portal 自身的 category/tag/owner 合法性加入路由资格。

现有 `dynamic_triad.cpp:413-427` 的 Filter 是**对象资格** callback，不能原样套在 portal graph 上；其 `v.alive && extra(v, query)` 会错误拒绝“自身不合法、块内另有合法见证”的 portal。

slot label 复用也要同步 hnswlib 的 external label 语义；不能按 internal ID 访问 mask，更不能将旧 portal label 继续映射到已复用的另一块。

### 4. 删除 portal、重复块与块覆盖

反例：沿用第 1 节块 A，删除四个端点，中心仍 live 且合法。若只对被删 portal 调用 `markDelete`，不补 portal，A 无可返回 portal，成为永久路由盲块。

更小反例：某块两个点、两个 portals，删除其中一个后保持另一个仍有覆盖；但如果更新政策容许继续删除最后一个 portal 而不重选，就留下非空无代表块。

**必须冻结的不变量：**

\[
n_b>0\Rightarrow |R_b|=\min(4,n_b),
\]

其中 `R_b` 是当前可返回的 live portal 集合。空块没有可返回 portal。

一个确定且符合原方案的维护规则是：每次该块向量插入/删除后，在当前 live slots 上按固定 seed/tie-break **重算 farthest-first**，对新旧 portal 集合差集增删图节点。这样也保证“当前 portals 为 farthest-first”，而非仅保证初始化时成立。

- 原 portal 集合交集不重插。
- 被删/替换 portal 的 label、block 映射、live 状态必须同时更新。
- tombstone 可导航不等于可返回，不可借此声称仍有路由覆盖。
- 属性 V/T、对象 M、目录 R 不重选 portals。
- 若重用 label 但对应向量改变，不能沿用 `unmarkDelete` 的 immutable-vector 快捷路径；必须实际更新向量/图连接。

现有 `insertGraph()` 的 unmark 快捷路径之所以成立，是代码明确限定每个 potential ID 的向量 immutable。

### 5. Euler MOVE、内部节点直接归属与缓存

#### 5.1 R 后固定 DFS rank 必然过期

初始：

```text
root
├── A
│   └── C   // x.owner=C
└── B
```

执行 `R C B` 后：

- `scope=A recursive` 必须排除 x；
- `scope=B recursive` 必须包含 x；
- `scope=C recursive/nonrecursive` 均仍包含直接归属 C 的 x。

如果 block posting 按旧 rank 排序、缓存旧 ancestor 集，结果错误，即使稳定 node ID 未改变。

**确定修正：**

- posting 存稳定 owner ID，不持久化会过期的 Euler 数字作为唯一真值。
- 查询使用当前 Euler sequence 的 rank；例如双 token 约定：

\[
tin(a)\le tin(owner)<tout(a)
\]

- owner→ancestry/资格 mask 缓存限定在一个 query 内；不得跨 R/M/V/T/D/I 使用旧状态。
- R 原子完成 cut/link 与 parent 更新；当前单线程 replay 可用事件边界固定快照，不必扩大为并发/MVCC项目。
- 禁止根移动、禁止移到自身或后代；与现有 `reparent()` 的约束一致。

**成本警告：**普通数组 DFS rank 全量重标不是 `O(log D)` cut/link。只有采用支持 subtree sequence cut/link 与 order-statistics rank 的动态序列实现，才能主张相应复杂度；owner 无闭包不代表 rank 获取免费。

#### 5.2 内部节点必须允许直接 owner

树 `root→A→C`，x.owner=A，y.owner=C：

| 查询 | 资格对象 |
|---|---|
| A recursive | x、y |
| A nonrecursive | 仅 x |
| C recursive/nonrecursive | 仅 y |

不能“递归 scope 只遍历叶子”，也不能用“子树非空”等价于“该目录直接非空”。现有测试 fixture 已包含 owner 为内部节点及 root 的对象。

#### 5.3 M 与 R 不是同一个维护操作

- `M x dst`：保持 x 的几何块/slot 不变，更新旧 owner posting、新 owner posting 与 x.owner；不动 Euler 拓扑、不动向量图。
- `R src dst`：保持对象 owner ID 及块 posting 不变，仅改目录拓扑。

“只触达 bitplanes”适用于普通属性修改，不宜用于 M：M 至少还要改 owner posting。

### 6. AND/OR 与四种 tag 必须在同一 slot 上完成

设块 live mask 为 `V_b`，目录 mask 为 `S_b`，启用的普通字段 mask 集合为 `F_1,...,F_m`。

\[
E_b=V_b\cap S_b\cap
\begin{cases}
V_b &m=0\\
\bigcap_iF_i&AND\\
\bigcup_iF_i&OR
\end{cases}
\]

目录始终 AND；禁用字段不参与 OR；没有启用字段时，普通属性条件恒真，而不是空 OR=false。

**共同见证反例：**

```text
x: category=1, numeric=100, in_scope=true
y: category=2, numeric=5,   in_scope=true
query: category=1 AND numeric∈[0,10]
```

类别 mask 与数值 mask 都非空，但交集为空，块不得被接受。

**scope/OR 反例：**

```text
x: outside scope, category=1
y: inside scope, category=2
query: scope AND (category=1 OR numeric∈[0,10])
```

若 y 的 numeric 也不匹配，正确结果为空；不能计算为 `(scope AND category) OR numeric`，也不能只比较块级非空标志。

令 tag universe 为当前契约的 64 bits，`T_t` 为含 tag t 的 slot mask，Q 为 query tag 集：

| tagmode | 集合关系 | 精确 mask |
|---|---|---|
| 1 | `Q ⊆ ITEM` | `∩_{t∈Q} T_t` |
| 2 | `Q ∩ ITEM ≠ ∅` | `∪_{t∈Q} T_t` |
| 3 | `ITEM = Q` | `mode1 ∩ mode4` |
| 4 | `ITEM ⊆ Q` | `V_b \ (∪_{t∉Q} T_t)` |

所有补集都在 live/有效 slot universe 内计算，避免末尾 padding 和 deleted slots 污染结果。

`Q=∅` 时必须分别是：

```text
mode1: 全部 live slots
mode2: 空
mode3: 仅空 tag 对象
mode4: 仅空 tag 对象
```

`dynamic_triad.cpp:101-110` 与上述关系一致。数值谓词是闭区间；其当前接口以 `-1` 表示禁用字段，不能未经声明扩大成完整 int64 任意取值接口。若新设计仍对齐该 replay，应保留现有范围契约，不暗中引入另一种单侧无界语义。

### 7. 预算、停止与 empty query

#### 7.1 ef 不是硬访问预算

目标 hnswlib filtered 搜索默认停止条件：

```cpp
candidate_dist > lowerBound && top_candidates.size() == ef
```

`top_candidates` 只计通过 callback 的点。因此：

- `ef_portal` 不限制 portal 距离计算次数；
- `ef_local` 不限制局部访问/距离次数；
- L 只限制最终进入局部 ANN 的不同块数量；
- 资格 mask 可能对大量未进入 local ANN 的块求值。

**可复现退化：**portal graph 的可达底层连通分量有 n 个节点，所有块资格 mask 均为空。callback 恒 false，`top_candidates.size()==0`，停止条件始终无法靠 `==ef` 成立；候选扩张可遍历整个可达分量。每个节点仍计算距离，虽然最终返回空。

如果可返回 portals 总数少于 ef，同样可能遍历整个可达分量。局部图中合法对象少于 local ef 时也存在相同现象。

**确定修正不是必须新增硬 cap**，而是准确冻结预算含义：

- 参数名/文档明示 L 为块 probe 上限、ef 为 beam 参数；
…

…

…

…


[Showing lines 1-300 of 400. Use :301 to continue]