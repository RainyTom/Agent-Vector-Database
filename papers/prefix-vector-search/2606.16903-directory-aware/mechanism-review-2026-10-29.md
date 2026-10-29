# Directory-Aware：七篇论文联读中的机制复核

日期2026-10-29。读本地2606.16903v1正文§I–VII（PDF pp.1–12）及相关参考文献；在线arXiv摘要复核题名、完整作者顺序、2026-06-15预印本日期。正式venue未核实。未重跑原系统。

## 当前研究主线

带层级特性的多属性向量混合过滤：外部层级范围与类别、范围、多值标签等组合谓词共同决定eligible，再按原向量距离top-k。不是HNSW内部层级、纯目录lookup、多向量聚合或向量join。

## 原文实际贡献链

1. §I/II：应用中目录是有结构操作的命名空间，而不只是静态字符串。DSQ解析递归/非递归范围，DSM表达MOVE/冲突MERGE。
2. §III：PE-Online将成本放到查询时路径展开；PE-Offline将成员物化到祖先，但仍维护路径键空间。
3. §IV：TrieHI以稳定节点身份表达子树，inclusive IDs维护`Inc(v)=Local(v)∪⋃child Inc(child)`；结构重挂不必逐个重写后代目录键。
4. §V：在同PG/IVF执行器下分别测scope解析、维护、空间及应用集成。贡献不是发明Trie，也不是单独最小化nonrecursive查询，而是把目录操作与物理设计取舍放进统一执行层。

## 可核实的证据与限制

- §IV pp.6–7明确不改变向量排序或ANN布局。递归读inclusive；非递归减子inclusive。当前语义中每对象一个逻辑父目录；多路径绑定不能原样使用差集。
- TableII的O(t)是节点访问/aggregate操作数，不是bitmap成员成本无关的CPU时间。MOVE仍更新新旧独有祖先；MERGE仍受名称冲突规模控制。
- §V-A p.9：共用entry catalog从DSM latency和目录模块构建开销比较中排除。不能把这些数字当完整系统成本。
- TableIV：WIKI递归directory-only均值，PE-Online365000.80μs、PE-Offline235.23μs、TrieHI273.10μs。TrieHI不在每项上胜过PE-Offline。
- TableV：WIKI PG总空间baseline7925MB、PE-Online8143MB、PE-Offline8213MB、TrieHI8326MB。稳定身份有空间价格，不是全面支配。
- §V-E Fig11 pp.11：作者观察路径越深，PG合格节点密度和连通性受影响，可能增加导航工作；IVF趋势不同。是具体设置观察，不是任意filtered HNSW退化定理。
- §V-F TablesVI/VII：OpenViking的QA、token和时延变化属于整条系统pipeline，未把所有收益隔离归因于TrieHI。
- ARXIV-Dir的subject/temporal是两个独立namespace；原实验不能当作本项目任意层级＋多属性交集的联合索引结果。
- §IV-A将wildcard/richer path predicates留future work；§VII明确future work包含“cost-aware planning for mixed semantic and hierarchical constraints”。相关研究不是作者完全未意识到的空白。

## 对本项目的启发（研究者推导）

最重要的剩余问题不是再实现一个Trie，而是“即使scope已经便宜得到，层级＋多属性筛选后的有效几何/导航如何索引化”。Hierarchy只给出部分包含偏序；再加入属性合取/析取后，候选集合不再是单一层状族。需要分开：资格表示、条件几何界、导航候选覆盖，以及更新后的维护。

可采用其稳定节点身份作为scope前端，不把它当新贡献；新索引必须证明层级共享与跨属性组合提供了额外能力，并用保持eligible基数的组合/相关性对照区分单纯选择率效应。

来源：[原文](https://arxiv.org/pdf/2606.16903v1)，本地[按页全文](paper.md)。与VikingMem是否为明确引用/继承关系，应从原文定位后入关系索引，不能仅因作者重叠推断。
