# 基线评估：Directory-Aware / TrieHI 与 OpenViking

日期：2026-10-20。问题：该论文是否适合作为 LES 的基线，且其方案是否真的“直接嵌入到了 openviking 当中”。

## 1. 语义对应

论文（arXiv:2606.16903v1）把 **recursive DSQ** 定义为：在指定目录及其全部后代中检索向量，合格集合 $\operatorname{Scope}(p)=\{o_i:p\preceq_{pref} s_i\}$，与我们 Subtree(u) 的可行集 $\mathcal D_u$ 完全同构。其 **non-recursive DSQ** 通过“当前目录的 inclusive 集合减去直接子节点的 inclusive 集合”恢复本地对象，对应我们的 Exact(u)。其 **MOVE/MERGE** 对应目录结构维护（DSM）。

三种实现（PE-Online / PE-Offline / TrieHI）都在 scope 层，不改 ANN 布局；TrieHI 把目录表示为有稳定节点身份的 segment Trie，存 Local + 子节点 inclusive 的 bitmap。

## 2. 与我们的两种模式的映射

| 论文 | 我们 | 对应 |
|---|---|---|
| recursive DSQ | Subtree(u) | 合格集 = 目录及后代 = $\mathcal D_u$ |
| non-recursive DSQ | Exact(u) | 本地对象 = 直接挂载于 $u$ 的 $B_u$ |
| MOVE/MERGE | 目录结构维护 | 未在本期范围 |
| PE-Online | 直接目录 posting，在线枚举后代 | B0 Subtree（祖先展开+扫描） |
| PE-Offline | 祖先 posting 物化 | B4 逐作用域物化（祖先成员复制） |
| TrieHI | 稳定 segment Trie + 逐节点 inclusive bitmap | LES 的前序区间切片近似（但 LES 共享边级邻接而非 bitmap 集合） |

因此，**作为“目录层基线”非常合适**：它与我们问题的 scope 语义完全一致，且已有 PE-Online / PE-Offline / TrieHI 三个实现级对照。但要区分：它不改 ANN，基线是“目录表示 + 既有 PG/IVF”的组合，不是新的 filtered ANN。

## 3. “直接嵌入到 openviking”核查

- 论文明确写 “We integrate TRIEHI into the open-source OpenViking context database”（paper.md 第 177 行），OpenViking 的 README 也列了 “Search a directory, not the whole index”。
- 但当前 `volcengine/OpenViking` 代码里，`src/index/detail/scalar/bitmap_holder/dir_index.{h,cpp}` 只是一个**目录 trie → 逐目录 bitmap 的路由层**：`find_node` 沿 segment 定位，`collect_bitmaps_recursive` 收集该路径下 leaf bitmaps，交给上层 `BitmapGroupBase` 做合并/过滤；它**没有** inclusive-set 物化、没有 MOVE/MERGE、也没有改 ANN。论文声称的“inclusive IDs / segment trie / DSM”在当前仓库里没有对应实现（代码搜索无 `triehi`、`inclusive`、`dsq`、`segment` 命中）。
- 因此更准确的说法是：**OpenViking 嵌入了“目录语义检索（PE-Online 式目录 routing + bitmap 合并）”，而不是论文里完整的 TrieHI 结构**。把 OpenViking 的 dir_index 当作 TrieHI 基线会低估/错配其能力。

## 4. 作为基线的适配方式

- **PE-Online 式**：直接目录 posting，查询时枚举后代 bitmap 并取并（我们已有 B0 Subtree：祖先展开+精确扫描）。
- **PE-Offline 式**：祖先 posting 物化（B4：逐作用域物化）。
- **TrieHI 式**：稳定节点身份 + 逐节点 inclusive bitmap + 结构重连。需要我们实现一个“目录树 + 逐节点 inclusive bitmap”的变体，并使其与既有 ANN 组合；这不是 LES 的边共享，而是集合级共享。
- **OpenViking dir_index 式**：目录路由 + 逐目录 bitmap 合并（更接近 PE-Online/混合），可作为一个强系统基线，但不能当作 TrieHI。

## 5. 结论

**适合作为基线，且语义上是最近的前序。** 推荐把 PE-Online、PE-Offline、TrieHI 三种目录表示与 LES 同协议对照，并补充 OpenViking 的目录路由（dir_index + bitmap 合并）作为系统参考；但**不要**把 OpenViking 的 dir_index 直接命名为 TrieHI——它目前是目录路由 + 集合合并，没有 inclusive-set 物化或 DSM 重连。论文自己的代码未找到独立实现，OpenViking 当前实现与论文声明并不完全一致，需谨慎引用。
