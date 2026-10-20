# LES 下一版如何改：实现规格与取舍（2026-10-20）

回传状态更新：E0已实现，S1 full-list exact interning因空间过大关闭；下一步见 [B1作用域预算指导](remote-round2-budget-policy.md)。本文件保留设计历史，其他条件分支未因此自动启动。

状态：**研究侧设计，尚未实现/验证**。与 [remote-next-round.md](remote-next-round.md) 配套，测量规则以该文件为准。以下名称是本地候选标识，不是文献方法名；新颖性尚未充分排查。

## 1. 决策：保留共享存储，恢复作用域搜索结构

推荐顺序：**E0 搜索内存复用 → S1 保留作用域邻接的共享表示 → R1 按需使用并集补充搜索**。A1 多入口只作为有条件分支；不直接重写 JAG 式联合构图。局部 u16 是编码候选，不是主算法改进。

当前 `les_build.cpp` 遍历 `c_v.bin` 时知道原 scope v，但写入的是 `(source, activation, target)`，之后按三者去重。activation 说明端点在哪些 Subtree 中可行，**不能说明哪些 scope 的底图实际选择了这条边**。查询按 activation 取并集，因此恢复了可行边的超集，却没有恢复 PSG 的稀疏邻接。

设底图有序邻接为 `G_u(x)`，当前合法并集邻接为 `U_u(x)`。S1 要提供可直接访问的 `G_u(x)`，无需先解码整个 `U_u(x)` 再过滤。研究假设是：**物理共享邻接内容，同时保留作用域的逻辑邻接，可能比“所有可行边都展开”更接近 PSG 的吞吐**。是否还能比 PSG 省空间必须实测，不保证两全。

| 候选 | 主要改变 | 不应声称 |
|---|---|---|
| E0 | 复用候选堆内存、复用入口距离 | 改善图的导航能力 |
| S1 | 从并集枚举改为 scope 对应邻接直接访问 | activation 本身能恢复底图 |
| R1 | 较低预算 core 搜索，困难查询再用 union 搜索 | stall 是召回不足的证明 |
| A1 | 保留图，增加少量可行的多样入口 | 完成 fiber/JAG 论文复现 |

## 2. E0：先做不改变边集的查询执行优化

修改 `src/les_query.cpp`，一个独立 commit：

1. 将每次 scope 搜索新建的两个 `priority_queue` 换成查询线程内复用的 `vector + push_heap/pop_heap`；跨查询 `clear()` 保留 capacity。第一次扩容允许发生，但分别记录 warmup 和稳态分配次数，所有内存计入 RSS。
2. 保留原比较器、全局 id 的平局规则、push/pop 顺序和停止条件。不能直接照搬 PSG 的局部 id 比较器，否则相等距离时搜索路径可能变化。
3. upper descent 得到的距离 d 对应 `start=U.gid[ep]`，level-0 入口复用 d，避免重复计算；所有数值运算类型不变。
4. 原顺序与 scope 调度分别测；调度不是 E0 的收益，prefetch 暂不混入这一 commit。

验收：固定/自适应、u32/varint 下与原 LES 输出逐位一致；除入口重复计算外搜索计数一致。严格 r95/r98 比较中记录 QPS、分配次数和 RSS。如果收益在噪声内，保留简洁版本，不据此写算法贡献。

## 3. S1：共享邻接字典 + 每作用域引用（首选结构改进）

### 3.1 先估算，后写查询引擎

在 `src/les_build.cpp` 读取每个 `c_u.bin` 时额外收集完整的 **有向、有序** 邻接 `G_u(x)`，包含空列表；记录 local source id 与 global source id。以导出后的 PSG 邻接为真值，不以导出前 hnswlib 图为真值。

第一阶段只生成统计，不改默认 LES 查询：

- `Nref = sum_u |F_u|`，每个 (u,local_source) 一个引用；
- 将每条邻接的 target 转成 global id，以完整有序序列做 content interning；hash 相同还须逐项比较，不能依赖无碰撞假设；
- 统计 unique list 数、长度分布、完全重复率、每对象不同 scope 列表的重合率；
- 计算引用、字典 offsets/lengths、ids、upper、gid、向量及所有元数据总字节。

初版编码：`list_ref_u[local_source]` 为 u32；字典 `list_offsets` 为 u64，`list_targets` 为 u32，空列表共享同一项。这样静态字节上界明确。若空间不理想，再**只做估算**：保序块共享（每4或8个 id一个块）或 source 专属邻居字典 + 列表选择序列。后者必须算入每个选择 id/offset，不能只统计唯一边。

不可先按 target 排序来提高字典命中率，再宣称与原 PSG 逐位等价。也不可把其他 source 的 local ids 当作可共享的 global ids。

**进入实现的门槛：** 不含 union 的 S1 完整部署字节应低于同一底图的共享 PSG；理想目标不超过 LES-v3 的实测完整字节。达不到时输出字节构成和失败原因，不实现大规模全量选择表。阈值是工程筛选条件，不是已有实验结论。

### 3.2 查询直接访问，避免扫完再筛

建议新布局 `layout_scope_dict`，不能覆盖原 varint 目录。文件示意：

```text
base_u8.bin                         唯一向量副本
upper/                             复用同一底图上层、gid、入口
scope_refs/<u>.bin                  以 scope local source id 索引的 list_id
list_offsets.u64 / list_targets.u32 有序 global 邻接字典
scope_lookup/<u>                    global -> local 的查找结构（必须计费）
```

必须解决 source 查找，不能假设 O(1) 免费：

- `upper[u].gid` 保持原 local 编号，新增排序 `(global, local)` 数组用于二分查找作为正确性原型；
- 查询可将 global target 转为 local id，候选堆与 visited 保持原 PSG 的 local id；向量读取仍通过 gid。查找时间计入查询；
- 两个明确对照：A 使用 global 字典+查找；B 使用每 scope local 邻接（PSG 参照）。A 的映射成本可能抵消共享收益，需要回传；
- 不按所有 `(u,global_id)` 分配稠密 n×scope 表；后续若用 hash/direct map，构建和内存必须进入总成本。

初版 **core-only**：从 scope 原 upper 入口下降，每次扩展只读取 `G_u(x)`。统一 core 的候选比较器、邻接顺序、距离实现、ef、入口与 PSG 后，要求每个 (u,x) 的邻接序列全量一致、逐查询 id 一致。若使用全局 id tie-break，先让 PSG 控制组也采用同一规则，并保留旧规则对照；不能把 tie 导致的变化称为压缩误差。

验收优先级：图恢复正确 → 输出可复核 → 空间满足门槛 → 在严格召回下吞吐可接受。S1 是压缩 PSG 邻接的候选，不再靠并集提高召回；若与 PSG 一样快且更小可成为空间贡献，若映射成本太高则保留负结果。

### 3.3 全量字典不划算时：有预算的少数 scope 覆盖

保留原 LES-v3，仅对少数“查询频繁且 union 扫描放大明显”的 scope 存连续 core 邻接。选择只用 calibration：按 `frequency × 实测节省时间 / 新增字节` 排序，在固定预算内取 scope；这是启发式，不保证最优。

初筛新增预算为完整 LES 大小的 5%、10%；另设“不超过实测共享 PSG 总大小”的硬上限。历史约9 MB余量只是提示，必须用本轮字节重新计算。引用表/映射/元数据全部计入增量。单 scope 查询：被选中走 core，其他走原 union；路由不查看 test GT。所有查询仍以共同 recall 目标调参。

这是空间换吞吐的混合方案，应标注新增预算。若只在训练高频 scope 有效，必须报告不同 scope 频率分布下的退化，不能称普遍优于 LES。

## 4. R1：并集作为按需补充路径，避免硬裁边

仅在 S1/覆盖方案已成立、且 union 对困难查询确有召回收益时做；不是 E0/S1 的必选依赖。保留 core+union 会增加空间，必须重新通过总空间门槛。

第一版用**两次独立搜索**，避免复杂的“中途扩 beam”错误：

```text
A = core_search(q, u, ef_core)
if trigger(A, query_statistics):
    B = original_union_search(q, u, ef_union)  # 原始 upper 入口、清空 visited
    result = exact_distance_topk(unique(A ∪ B))
else:
    result = A
```

新增预算全部计入耗时，包括第一次搜索的浪费。不能直接增加已经剪枝的 top/candidate heap 的 ef 并假设等价于从头大 ef：被丢弃节点无法恢复。先实现独立重跑，后续若做增量版本须记录被延迟的候选/邻接，另验语义。

触发特征：返回不足 `min(k,|F_u|)` 时必触发；其余只在 calibration 上试固定 scope-size 桶策略，或“最近 w 次扩展没有改善第 k 距离”的 stall。后者只是启发式，可能对已经正确的查询误触发。记录 trigger rate、被补救的漏召回、误触发时间、最慢查询和总体召回；test 不使用 GT 做路由。空/|F|<k 必须用完整可行集枚举补足，额外数据结构也计费。

消融：core-only、union-only、always-rescue、calibrated-rescue；不能只与低 recall 的 core 比。搜索初筛 `ef_core∈{12,24}`，`ef_union∈{24,48,96}`，适用时均不小于 k；最终从 calibration 冻结参数。

只有总耗时在目标 recall 下改善、且含双结构空间有意义才保留。若重跑开销吞掉收益，停止，不继续堆规则。

## 5. A1：导航受限时才试少量多样入口

适用证据：同顺序下 LES 的扩展/距离计算仍高，且离线诊断显示更好的初始入口能降低工作量。当前 LES 已使用每 scope 原 upper 图下降，**不能默认它缺少进入可行区的入口**。

实现保留图不变，每 scope 最多增加 r=2/4 个 anchor：从可行对象中固定 seed 采样最多256个，按 farthest-first 选取，多标签对象去重；记录采样 seed、构建距离次数与时间。小 scope 对象不足则全取，空 scope 不取。

查询先做原 upper descent，再计算 r 个 anchor 的距离，将原入口和 anchor 一起放入相同 level-0 beam，按 visited 去重。不取消原入口，不读取 scope 外 anchor；anchor 距离和额外队列操作计入查询。r=0 为控制组；same-ef 和 matched-recall 两种对照都要有。

多入口可能扩大搜索而非减少工作，不保证提高 QPS；输出无需逐位等于原 LES，但必须满足过滤正确性与 recall 目标。只在可重复收益下进一步研究训练无关的 anchor 选择；不要直接声称跨选择率导航已解决。

## 6. 远端实现任务与停止条件

| 顺序 | 交付 | 本轮验收 |
|---|---|---|
| 1 | 测量修正 + E0，分独立 commit | 同语义回归；公平性能表；分配/搜索计数 |
| 2 | S1 静态分析 | 完整空间估算、重复率、映射成本预算；决定全量或少数scope |
| 3 | S1 最小查询实现（空间门槛通过后） | 全量邻接恢复、PSG等价控制、matched-recall性能 |
| 4 | R1 或 A1 选一个，不并行混改 | 单机制消融；总空间、总距离计算、总耗时 |

本轮至少完成1和2；若2不通过，转有预算的 scope 覆盖估算，记录为什么放弃全量字典。若所有候选在 recall–time–space 上仍被共享 PSG 支配，应收敛为负结果/适用边界，不为维持 LES 名称而继续无上限优化。

对真实多标签层级、Exact、antichain、1M规模的有效性仍待验证。先在 sift100k完成机制判别，再扩展现有五个原生 L2 点；量化代理语料的正结果不能代替原生任务验证。
