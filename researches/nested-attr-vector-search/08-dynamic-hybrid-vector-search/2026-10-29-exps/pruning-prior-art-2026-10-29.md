## 结论

两条收窄方向都值得继续，但**“稀疏倒排＋残差范数保证不漏”已有非常直接的前序 L2AP**；必须把贡献压到“任意原始 dense FP32 squared-L2 的表示转换、零碰撞覆盖与过滤后的成本”，不能把 Cauchy–Schwarz 残差界当新意。

**“查询时求 \(B\cap F\) 的条件化几何界”与本次核读机制仍有明确区别**：Seismic 是整块摘要，Curator 是过滤子树/缓冲区与启发式路由，二者都不是条件极值证书。但这只是机制区分，**不是新颖性结论**。最值得继续查的是“共享投影值排序＋任意 eligible 集合上的 successor/predecessor/extrema 查询”的既有数据结构，而非再查普通 ANN 标签索引。

以下四篇为核心核读。页码指所链接 PDF 的页序；HTML 文献以章节定位。

## 核心原始论文及机制证据

### 1. MUVERA：有理论的 multivector→single-vector 转换，不是当前任务的 dense→sparse 过滤索引

**完整作者顺序：** Laxman Dhulipala, Majid Hadian, Rajesh Jayaram, Jason Lee, Vahab Mirrokni.  
**版本：** *MUVERA: Multi-Vector Retrieval via Fixed Dimensional Encodings*, NeurIPS 2024，核读的是[会议版 PDF](https://proceedings.neurips.cc/paper_files/paper/2024/file/b71cfefae46909178603b5bc6c11d3ae-Paper-Conference.pdf)，不是后来修订的 arXiv v2。

- **§1.1–1.2，p.3：**目标是 \(\sum_{q\in Q}\max_{p\in P}\langle q,p\rangle\)，即 Chamfer/MaxSim；FDE 后做 MIPS，再按原 Chamfer 重排。
- **§2，pp.4–5，式(2)：**共享空间分区；query 的块内向量求和，document 的块内向量求均值；document 空块用附近已有 token 填充；可加随机投影、独立重复。
- **§2.1，pp.5–6，定理2.1–2.2：**单位向量、随机化条件下的加性近似保证。不是原始 squared-L2 的确定性过滤界。
- **附录A，p.15，Lemma A.1/A.3：**query FDE 确实可稀疏；但 document 空块填充削弱其稀疏性。**无内部降维投影时**存在“不高估 Chamfer”的单边性质；投影后变为带误差的概率保证。
- **§3.2，p.8：**实际候选索引是 DiskANN＋PQ，不是 WAND。

**否证边界：**“MUVERA 已把 dense 检索变成可直接用 WAND 的稀疏 posting 检索”不成立。其无投影单边界方向也不能直接用于最大相似度安全淘汰——低估分数不足以证明对象不可能优秀。不能为借用它而默认改变当前 single-vector L2 任务。

### 2. Seismic：几何分组 posting＋整块向量摘要已经存在；实用版本是近似剪枝

**完整作者顺序：** Sebastian Bruch, Franco Maria Nardini, Cosimo Rulli, Rossano Venturini.  
**版本：** *Efficient Inverted Indexes for Approximate Retrieval over Learned Sparse Representations*, SIGIR 2024，11页，DOI [10.1145/3626772.3657769](https://doi.org/10.1145/3626772.3657769)；核读 [arXiv:2404.18812v1](https://arxiv.org/pdf/2404.18812v1)，2024-04-29。

- **§5.1–5.2，p.4：**每维 posting 先按权重保留前 \(\lambda\) 项，再按向量几何相似性分块，不是按 docID 固定切块。
- **§5.3，pp.4–5，式(2)：**
  \[
  \phi(B)_i=\max_{x\in B}x_i .
  \]
  在其非负 sparse 表示条件下，完整摘要产生块内 inner product 上界。**“几何 cohesive block＋向量化 Block-Max/WAND”不能再算新机制。**
- **§5.3–5.5，pp.5–6，算法1–2：**实际用 \(\alpha\)-mass 截断摘要、标量量化、query cut、posting 静态截断及 heap_factor。论文明确承认压缩可能失去摘要保守性；算法2按 \(r<\mathrm{Heap.min}/\mathrm{heap\_factor}\) 跳块。因此不能把实用 Seismic 当 exact-safe WAND。
- **§6，p.6：**一般化为 spilling clustering、sketching、compression、routing；其抽象覆盖面比“稀疏词倒排”更广。

**与父任务的差别：**它没有计算 \(\phi(B\cap F)\)，也没给出任意 dense→sparse 的原 L2 误差证书。非负坐标 max 上界不能直接移植到有正负值的 dense/PQ 残差空间。

### 3. L2AP：稀疏索引＋未索引残差范数＋完整候选覆盖，是最直接的安全性前序

**完整作者顺序：** David C. Anastasiu, George Karypis.  
**版本：** *L2AP: Fast Cosine Similarity Search With Prefix L-2 Norm Bounds*, ICDE 2014；核读[作者原始论文 PDF](https://davidanastasiu.net/pdf/papers/2014-AnastasiuK-ICDE-l2ap.pdf)。本次未独立核验正式 proceedings 页码/DOI。

- **§II，p.2：**单位长度向量，cosine 化为 dot product；只索引部分坐标，其余为未索引 prefix。
- **§IV-A/B，p.3：**索引长度不是任意稀疏化：必须保证每个达阈值真邻居至少有一个索引公共特征。这是**零 posting 碰撞可否安全淘汰**的关键覆盖条件。
- **§V-A，p.5，式(3)–(4)：**
  \[
  \langle x',y\rangle\leq\|x'\|\|y\|,\qquad
  \langle x'_j,y'_j\rangle\leq\|x'_j\|\|y'_j\|.
  \]
- **§V-B–D，pp.5–6，算法3–4：**prefix norm 决定索引覆盖、何时停止引入新候选；部分分数加剩余范数乘积决定候选淘汰；存储 prefix norm/pscore 支持安全验证。

**明确否证：**“保留大坐标倒排，存残差 L2 norm，部分积＋Cauchy–Schwarz 上界剪枝”本身已经成立且已知，不是新方案。

**不能直接等同的地方：**该实现是正权重处理路径（伪码迭代 \(x_j>0\)）、归一化 cosine、固定阈值 all-pairs join；不是任意带符号 dense、不同范数、动态属性、单查询 filtered top-k。Cauchy–Schwarz 本身不要求非负，但其余值界、累加与覆盖策略不能未经修改照搬。原始 L2 数据不能靠归一化偷换距离排序。

### 4. Curator：共享几何树＋谓词子集索引已知，但没有条件几何证书

**完整作者顺序：** Yicheng Jin, Yongji Wu, Wenjun Hu, Bruce M. Maggs, Jun Yang, Xiao Zhang, Danyang Zhuo.  
**版本：** *Curator: Efficient Vector Search with Low-Selectivity Filters*, [arXiv:2601.01291v2](https://arxiv.org/html/2601.01291v2)，页面标注2026-01-07；本次未核实正式发表 venue，未核对 PDF 页码。

- **§3.1–3.2：**共享 hierarchical k-means 树；per-label buffer 调整粒度；Bloom 只判断对应标签子树是否存在。
- **§4.1，算法1：**路由分数用 centroid distance 减去 \(\alpha\) 倍**平均**cluster radius；文中称 heuristic。遇 buffer 不再改善结果可停止，非安全 branch-and-bound。
- **§4.2，算法2：**任意谓词先由外部关系引擎给出排序 eligible IDs/bitmap；用树路径编码 ID 的连续性、二分分割构建临时谓词子树。临时节点只保留基树指针、eligible 范围与子节点关系。

**对方向1的直接约束：**“共享几何分区＋bitmap＋临时谓词索引”已知；不能只换成投影块就宣称创新。它仍未求 \(B\cap F\) 的投影真实极值，也未由这些极值产生原 L2 安全下界。其临时树共享的是分区拓扑，不是条件几何极值。

## 导航与安全性：必须独立写清

补充核对了 **FINGER 会议版**：[原文](https://cdn.amazon.science/74/56/d9c1550a4afab9020fb063336769/finger-fast-inference-for-graph-based-approximate-nearest-neighbor-search.pdf)，**Patrick H. Chen, Wei-Cheng Chang, Jyun-Yu Jiang, Hsiang-Fu Yu, Inderjit S. Dhillon, Cho-Jui Hsieh**，WWW 2023，DOI [10.1145/3543507.3583318](https://doi.org/10.1145/3543507.3583318)。

**重要版本差异：**arXiv v1 是五位作者、浮点低秩＋distribution matching；会议版增加 Jyun-Yu Jiang，§3.2–3.3、pp.4–6 改为 residual-angle 的 learned-basis 符号哈希估计。不要混写作者与机制。

- 会议版算法2，p.5：近似距离没过门槛的邻居不入 candidate queue；过门槛才算 exact distance。**保留图的边，不等于保留原搜索导航路径。**
- FINGER 是逐邻居近似筛选，不是邻居组安全证书。
- **[INFERENCE]** 即使新下界能证明一个邻居不可能进入结果 top-k，也不能证明“不经过它仍可到达其他真近邻”。若要相对既有 beam search 不新增导航损失，应对齐其 **frontier/beam 门槛**，而非直接用 filtered result 第 \(k\) 距离删导航节点；或另给绕行/覆盖保证。

## 对两条候选的最终判定

| 候选 | 已知/不成立部分 | 真正仍需查证的问题 |
|---|---|---|
| 条件化几何证书 | 普通投影下界、整块 box、posting AND、共享树/谓词临时子树都不足以成为新意 | 能否不扫描 \(B\cap F\)、不每谓词物化，就共享维护条件 extrema/最近边界 witness；其查询与更新成本是否优于直接 eligible 距离扫描 |
| 稀疏码＋残差证书倒排 | partial score＋residual norm、阈值控制索引覆盖是 L2AP 直接前序；近似候选重排不能补回未召回对象 | 任意 dense L2 的转换是否真正产生短 posting；无碰撞对象的上/下界如何被**完整覆盖并廉价处理**；动态 top-k 阈值、带符号分量、不同范数与属性更新如何共同成立 |
| 邻居分组证书 | “边保留”“只跳 distance calculation”不足以证明无导航损失 | 证书究竟排除结果资格、frontier 入队资格，还是整个可达区域？三种证明不能混用 |

**[INFERENCE] 条件界正确性最低要求：**对正交归一投影 \(U\)，若精确获得 \(B\cap F\) 在投影空间的包围盒，则 query 到该盒的 squared distance 是原始 L2 安全下界；可能比整块界更强，也可能完全不变。一个普通 eligible witness 只给上界，**不能代替“该区间/极值之前不存在更近 eligible”的证明**。独立非正交投影的平方差也不能直接相加。

**未核实边界：**原始 VA-file 论文已定位为 Roger Weber, Hans-Jörg Schek, Stephen Blott，VLDB 1998，pp.194–205，[原文](https://www.vldb.org/conf/1998/p194.pdf)，但两个 PDF 来源均提取失败、PDF 查询超时，因此未把其正文作为已核验证据。RoarGraph、RaBitQ、PLAID 未深入核读；不能据本轮结果排除它们或投影排序/bitmap 数据结构中的更直接前序。未改仓库、未运行任何检查。