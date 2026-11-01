# DAG 索引图文样例：实际核心核对

## 目标与输入

对应[方案正文 §2–4](../hierarchical-dag-attribute-index.md)的图 1–3。向量与索引全驻内存；图示表达对象到局部图的成员关系，不约束向量存储次序。固定输入为[JSON](../data/dag-worked-example.json)：七概念、八对象、二维向量，h=1，q=(1,1)，k=2，τ=2，ef=20。b_R0/b_A1/b_B2/b_F3 的末尾为图编号，概念 ID 则为 R0/A1/B2/C3/D4/E5/F6。

B 的物化位图为显式讲解配置，不宣称预算启发式在 N=8 时会选中 B。位图按 o0…o7 从左到右展示，不按数值二进制串解释。该例不提供性能、内存优势或一般 ANN 保证。

## 已执行的 C++ 命令

从仓库根目录运行：

```bash
g++ -std=c++17 -O2 -pthread \
  -Icodes/directory-aware-vector-attr-search/src \
  -Icodes/directory-aware-vector-attr-search/deps/hnswlib \
  researches/nested-attr-vector-search/2026-11-01-exps/experiments/dag_worked_example.cpp \
  -o /tmp/dag_worked_example

/tmp/dag_worked_example \
  --out researches/nested-attr-vector-search/2026-11-01-exps/data/dag-worked-example-result.json
```

编译器为 g++ 15.2.0。驱动把生产 `dag_scope_ann.cpp` 的 CLI main 改名后包含，直接调用其 `run_query`、`search_graph`；没有新增查询实现或改变生产 API。固定对象 ID 不经过工作负载 loader 的 shuffle。暴力距离 oracle 独立计算二维 squared-L2，按距离、全局 ID 排序；scope 参考从原始边和直接标签遍历，不依赖缓存的后代列表。

输出：[实际核心结果](../data/dag-worked-example-result.json)。六个查询调用均通过内部核验，另用 Python 从输入 JSON 独立重算集合与距离，逐项比对[检查记录](../data/dag-worked-example-check.json)。

| 实际调用 | scope ID | 分区合格数 R/A/B/F | 路径 | 返回 ID |
|---|---|---|---|---|
| B 在线展开 | 1,3,4,5 | 0/2/2/0 | graph | 1,3 |
| B 读取物化 | 1,3,4,5 | 0/2/2/0 | graph | 1,3 |
| D 小集合 | 2 | 0/1/0/0 | exact | 2 |
| A OR B | 0,1,2,3,4,5 | 0/4/2/0 | graph | 1,0 |
| o6:F→B 后，B 在线 | 1,3,4,5,6 | 0/2/2/1 | graph | 1,3 |
| o6:F→B 后，B 物化 | 1,3,4,5,6 | 0/2/2/1 | graph | 1,3 |

B 查询的实际局部候选：b_A 为 (距离1,o1)、(距离2,o3)；b_B 为 (距离5,o4)、(距离5,o5)。更新后的 b_F 另有 (距离18,o6)。D 查询只有一个合法对象，k=2 时也只返回一个。

## 实际图记录与更新边界

驱动保存全局对照图和四张分区图的入口、层数、节点 payload、external label、局部 ID 和每层局部邻居列表。图 1 的 b_A 示意边取自该次实际构建：max_level=0，入口 local0/o0，四节点在 L0 两两相连；local1/o1 的 payload=(1,0)，邻居局部 ID 为 [0,2,3]。绘图位置只是排版，不表示原向量坐标，也不表示实际查询访问顺序。

实际记录的向量值、局部/外部 ID 映射以及邻边均已核对；标签变化前后图快照相同。这些检查仅说明普通 HNSW 节点内容与图结构不变性，不提供访存效率证据。没有采集完整节点扩展/距离计算计数，不能据本例推断算法加速。

驱动按指定的 o6:F→B 事件直接应用标签、posting 和 M(B) 位变化，再执行查询；它不是新的生产维护 API。图中的 C(B)、C(F)、C(R) 由参考范围计算，用于解释应维护的基数，并未把派生计数伪称生产 ANN 已有的持久计数。当前生产查询核心通过 `scope.count()` 得到候选数。此样例不替代一般多标签、插删或并发维护验证。

## SVG 导出与实际显示检查

源图为 `figures/dag-index-layout.svg`、`dag-query-walkthrough.svg`、`dag-label-update.svg`；正文引用同名 `-outlined.svg`。已执行：

```bash
/home/daihua/envs/cpp-tools/bin/python \
  researches/nested-attr-vector-search/2026-11-01-exps/experiments/outline_svg_labels.py \
  --font work/svg-fonts/NotoSansCJKsc-Regular.otf \
  --bold-font work/svg-fonts/NotoSansCJKsc-Bold.otf \
  --out-dir researches/nested-attr-vector-search/2026-11-01-exps/figures \
  --report researches/nested-attr-vector-search/2026-11-01-exps/data/dag-worked-example-figures.json \
  researches/nested-attr-vector-search/2026-11-01-exps/figures/dag-index-layout.svg \
  researches/nested-attr-vector-search/2026-11-01-exps/figures/dag-query-walkthrough.svg \
  researches/nested-attr-vector-search/2026-11-01-exps/figures/dag-label-update.svg
```

三图标签均转换为字形路径、0 个 `<text>`，背景透明、前景 currentColor；数量与 SHA 以[图形记录](../data/dag-worked-example-figures.json)为准。已逐图用 CairoSVG 白底栅格化检查文字和连线；未在原生 Stencil Tern 窗口核验。图示不表达向量地址排布或局部性的优化。
