# 三个基线的复述（存储 / 查询 / 更新 / 成本）

阶段 1 前半。目的：在提出任何新方法前，把 PE-ONLINE、PE-OFFLINE、NODEANN 的定义固定下来。依据：TrieHI 论文（arXiv:2606.16903 §III–V）原文、本仓库 2026-10-26/27 复现与 2026-10-29 hnswlib 实测。**以下即本方向的基线定义，后续实验不得偷改。**

## 0. 符号（沿用任务书）

$N$ ITEM 总数；$M$ 目录节点数；$d$ 平均层级深度；$f$ 目标范围的后代节点数；$C$ 过滤后候选数；$U$ 更新频率；$k$ 结果数；补充 $m_u$ 被移动子树的目录数、$c$ 直接子目录数、$t$ 路径深度。

## 1. PE-ONLINE（查询时展开层级范围）

**存储**
- entry→目录 catalog（每条 ITEM 一条）；
- parent-path 倒排：每个目录 path key → 直接绑定条目 posting；
- 辅助 path-key 索引：支持前缀枚举与直接查找。
- 每条 ITEM 只贡献 **1 条 posting**，空间三者最小。

**查询**
- Recursive：用辅助索引枚举锚点子树内全部目录 key（$m_q$ 个），逐 key 取 posting 求并集得到候选集，再交给 ANN/精确执行器过滤排序。元数据成本 $\Theta(m_q)$。
- Non-recursive：锚点 key 一次直接查找，1 次 posting 读取。

**更新**
- 属性改动/插入/删除：改对应 posting 与 catalog，$O(1)$ 条 posting。
- MOVE：枚举被移子树 $m_u$ 个目录 key，前缀替换生成新 key，逐对重映射 posting、增删辅助索引 key，$O(m_u)$，与条目数无关但与子树目录数成正比。
- MERGE：$O(m_u)$ key 合并 + 目标同名 key 冲突的集合并，≥ MOVE。

**主要成本（实测锚点）**
- recursive 目录解析随子树目录数线性放大：本仓库全量 WIKI 复现 directory-only 均值 2,029 µs、P99.9 28,954 µs（2026-10-27，`wiki_full_ivf_exact` PE-Online 行）；原论文 365 ms 均值（48 核 1.94M 条目）。
- non-recursive 最快（WIKI 3.9 µs）。

## 2. PE-OFFLINE（写入时物化祖先）

**存储**
- 在 PE-ONLINE 之上增加 path expander（父 path → 祖先序列）与 **ancestor-materialized 倒排**：每个目录 key → 位于其下**任意深度**的条目集。
- 深度 $t$ 的条目贡献 $t$ 条 ancestor posting；浅层 key（如 `/`）物化巨大集合。空间三者最大。

**查询**
- Recursive：锚点 key 一次 posting 命中，$\Theta(1)$ 次集合读取（WIKI 9.7 µs）。
- Non-recursive：取锚点全集，枚举 $c$ 个直接子目录取并集，算差集，$\Theta(c)$ 次集合运算（WIKI 67.6 µs）。

**更新**
- 属性改动/插入/删除：改 catalog + 该条目全部祖先 posting（$O(t)$ 条）。
- MOVE/MERGE：$O(m_u)$ key 重映射/合并 **再加** 子树外 proper ancestor 的 posting 增删（旧独有祖先删、新独有祖先加），$O(t)$ 次大集合更新；MERGE ≥ MOVE。

**主要成本**
- 摄入与维护写放大；深树（WIKI 均深 11.95）上存储放大明显。
- 参考点：TrieHI 是其"原生树"变体——节点稳定身份 + 每节点 Inc(v) 聚合 bitmap，MOVE $O(t)$ 与子树目录数无关；本任务基线按 brief 只含 PE-OFFLINE，TrieHI 仅作对照，不混入基线定义。

## 3. NODEANN（前缀树 + 每节点局部 ANN）

**存储**
- 目录前缀树（节点 ID、父子邻接）；
- 每个非空直接绑定节点一个局部 ANN 索引（本仓库实现：hnswlib `HierarchicalNSW`，M=16、efC=200；允许的小节点走共享距离精确扫描，即 B1 配置）；
- 每条 ITEM 只进其**直接绑定节点**的局部索引。

**查询**
- Recursive：DFS 枚举锚点子树内 $f$ 个非空节点，每个局部索引搜 top-$\min(k,|L(v)|)$，全局按 (distance, global_id) 合并取 top-k。
- Non-recursive：只查锚点一个局部索引。

**更新**
- 属性改动/插入/删除：局部索引内增删（hnswlib 标记删除 + 插入），不动其他节点。
- 条目跨节点移动：从旧节点局部索引删除、插入新节点局部索引。
- 子树 MOVE：**局部索引作为整体随节点重挂**（节点身份稳定，不重建）；但依赖 DFS 区间/Euler 编码的辅助布局需要重编号。
- MERGE：非冲突子树整体重挂；目标节点冲突时两个局部索引需要合并——**基线无协议**，是明确的研究缺口。

**主要成本（2026-10-29 统一 hnswlib 后端实测，串行）**
- fan-out：WIKI 递归查询候选节点中位数 1、最大 138,993。
- 小图固定开销：hnswlib 每索引固定 65,536 把锁 ≈2.6 MB，纯全图配置在 363,467 节点上 ≈950 GiB，**结构性不可行**（18/18 SIGKILL 实测）；B1（小节点扫描 + 大节点 HNSW）可行为 WIKI 2.63 ms（recall 1.0）。
- 局部搜索本身便宜：merge ≤0.6%、目录枚举 ≤2.5%；**主成本是大 scope 内必须扫描的条目数**（≥10k 桶 33 条查询占 91% 时间）。
- 局部召回损失只在含局部 HNSW 的配置出现，随 ef 单调恢复；合并与 ID/tie 无错误。

## 4. 三基线对照表

| 维度 | PE-ONLINE | PE-OFFLINE | NODEANN |
|---|---|---|---|
| 空间 | 最小（1 posting/条） | 最大（$t$ posting/条） | 向量一份 + 每节点图 |
| recursive 目录解析 | $\Theta(m_q)$ 枚举，慢 | $\Theta(1)$ posting | $\Theta(f)$ 局部搜索 |
| non-recursive | 最快 | $\Theta(c)$ 差集 | 单节点，最快 |
| 属性更新 | $O(1)$ posting | $O(t)$ posting | 局部索引增删 |
| 子树 MOVE | $O(m_u)$ key 重写 | $O(m_u)$+$O(t)$ 集合 | 整体重挂（无 ANN 重建） |
| ANN 调用次数 | 1（全局索引） | 1（全局索引） | $f$（fan-out） |
| 已知死穴 | 浅锚点目录爆炸 | 写放大、空间 | 小图固定开销、大 scope 扫描量 |

## 5. 理解确认点（与任务书逐条对齐）

1. 任务书"PE-ONLINE：查询时展开层级属性范围，再过滤候选 ITEM" ⇔ 本文 §1 的"展开→候选集→执行器过滤排序"，一致。
2. 任务书"PE-OFFLINE：写入时物化祖先/层级关系" ⇔ 本文 §2，一致。
3. 任务书"NODEANN：前缀树 + 每节点局部 ANN + 多局部索引 fan-out 合并" ⇔ 本文 §3，一致；本仓库的 B1（小节点精确扫描分支）是对局部实现的选择，不改变基线语义。
4. 任务书把"联邦"限定为**逻辑多索引协调**（非网络/多组织），本方向照此执行，通信优化不作贡献。

## 6. 与任务书需进一步对齐的问题

1. PE-ONLINE/PE-OFFLINE 后面的"向量执行器"在任务书里未指定（本仓库复现用过全局 HNSW 与全局 IVF）；新实验默认沿用**统一 hnswlib 全局 HNSW** 作为两者的向量后端，与 NODEANN 同一距离内核，保证公平。
2. 任务书要求的朴素全局 ANN + 属性过滤基线，与 PE-ONLINE/PE-OFFLINE 的差别只在谓词解析方式；若谓词含层级前缀，两者就是它的两种实现——对比时将按"目录解析延迟 vs 端到端延迟"分开报告，不重复计为两种独立基线。
