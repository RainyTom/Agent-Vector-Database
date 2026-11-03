# EMA: Approximate Nearest Neighbor Search with General Attribute Filtering and Dynamic Updates

2026-11-03核查。Mocheng Li、Baotong Lu、James Cheng、Chenhao Ma；arXiv:2606.00734v1，2026-05-30。正文用PVLDB模板，但标注Submitted to PVLDB Research Track；**接收状态、卷期与DOI未核实**，不按正式发表引用。唯一全文由alphaXiv alternate Markdown取得，来源/SHA见`metadata-2026-11-03.json`。

## 问题与方法

B类可迁移filtered ANN，不是vector join。输入向量＋数值/类别混合属性＋查询谓词，输出每query近似top-k；支持range、label、多谓词合取与Boolean组合，并声称支持动态更新。

已核§1、§3 pp.3–6、§4 pp.6–8、§5 pp.9–13相关正文：

- **Marker**：附在**边**上的紧凑摘要，从被支配邻居的属性按位**或**聚合，查询时按谓词相容性决定是否遍历该边，从而动态导出谓词相关子图。码本离散化带来受控假阳性，Alg.3在底层剪枝时写入。
- **构建**：沿用HNSW插入；Alg.3在剪枝中编码/合并Marker，复杂度§4.1给出O(M·efc·n log n·(M+m))，与HNSW渐近同阶；空间O(nM·sm)。
- **动态支持§3.5**：插入沿用HNSW；删除先做惰性标记，累积到阈值后做**patch**（记录遍历中遇到的失效边，用被删节点的最近有效邻居替换）；**属性-only修改**明确写为“updates the FT of reverse edges within one-hop neighbors by merging the new attribute information via a lightweight OR operation”；属性与向量同时修改走delete-and-insert。
- **§5.4实测**：删除1.3 s/1M、属性-only 2.5 s/1M、插入90.8 s/1M；patch 45.2 s/次≈全量重建371.1 s的12%（SIFT5M）；20%变更触发首次patch、其后每10%再触发、累计50%全量重建。QPS在95% recall@10下测。

## 与本项目的关系

**“支持属性更新”本身已有前序，且被明确作为贡献与实验指标发布。** 属性频繁更新不能单独作为我们的新意，也不能只说“别人假设静态”——EMA正文Table 1与§1把静态假设列为现有FANN缺陷并正面处理。

结构差异值得记录：EMA的Marker按位或只增不减，因此属性改值/删除后旧位不会在该操作中被回收，只能靠后续patch/重建复位；这是其阈值维护的结构性原因。本项目推导（非作者原话）：引用计数式的祖先聚合可精确撤销，避免单调假阳性累积。

但**不能由此宣称我们更优**：EMA单项更新成本是微秒级，本轮成本模型显示我们的定向更新中位约3.15×10^5次距离计算，量级相差极大；EMA的代价出现在周期性patch/重建，需按churn曲线比较，而不是比较单次操作。

## 未核与边界

未读附录A（证明）全文、未核Figure 9逐点数值与全部超参扫、未运行作者实现；正式版本差异、`s`与`m`的具体取值组合只按§5.1记录（s=256、M=40、efc=300）。Marker假阳性定理依赖码本分辨率与i.i.d.假设，不能迁移为我们DAG范围的保证。
