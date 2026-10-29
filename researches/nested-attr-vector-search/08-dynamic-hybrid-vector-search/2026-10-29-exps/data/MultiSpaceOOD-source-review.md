## Summary

1. **MSG 已取得并精读正式出版全文（19页）**。其 multi-vector 是**固定字段/模态的一一对应向量距离加权聚合**，不是 ColBERT late interaction、MaxSim 或 Hausdorff。其“统一图”实质是**共享对象顶点与向量存储、保留各字段组合的邻接列表**，不是证明所有组合具有同一套通用边。
2. **CDMG 已取得并精读作者技术报告全文（16页，2025-05-08 Git 历史版本）**，并核对2026正式出版书目。正式 ACM 正文各入口均返回403；**不能把旧报告的定理编号、实验表和五作者署名冒充2026六作者29页正式版**。旧报告与正式摘要之间已有明确措辞差异，下面严格标明版本。
3. 两篇均不是当前“层级外部属性 AND 多属性硬过滤”的现成解法。可借鉴的是**多条件导航关系的物理共享**和**按工作负载修正导航拓扑**；不能把属性转成软距离、把 OOD 当过滤，或据此继承精确 top-k 保证。
4. 未执行作者代码，未修改仓库，未运行构建/测试。

---

## 一、可解析书目与原文记录

```json
[
  {
    "key": "MSG",
    "title": "MSG: a unified multi-space graph index for efficient high-dimensional multi-vector search",
    "authors": ["Mengzhao Wang", "Xiangyu Ke", "Yifan Zhu", "Yunjun Gao", "Yaofeng Tu"],
    "venue": "Science China Information Sciences",
    "year": 2026,
    "volume": "69",
    "issue": "9",
    "article": "192106",
    "pages": "192106:1-192106:19",
    "doi": "10.1007/s11432-025-4793-9",
    "status": "Published research paper",
    "received": "2025-08-28",
    "revised": "2025-11-30",
    "accepted": "2026-02-05",
    "published_online": "2026-08-14",
    "issue_date": "2026-09",
    "landing_url": "https://link.springer.com/article/10.1007/s11432-025-4793-9",
    "pdf_url": "https://files.sciengine.com/pdfs/1674-733X/7A6ED766DFBC4011B8009139AFCB45A0.pdf",
    "code_url": "https://github.com/ZJU-DAILY/MSG",
    "read_version": "Publisher full text, 19 pages",
    "arxiv_id": null,
    "evidence_artifact": "artifact://74"
  },
  {
    "key": "CDMG",
    "title": "Efficient and Robust Out-Of-Distribution Vector Similarity Search with Cross-Distribution Monotonic Graph",
    "authors": ["Qiang Yue", "Mengzhao Wang", "Xiaoliang Xu", "Cheng Long", "Yuxiang Wang", "Jiahui Wang"],
    "venue": "Proceedings of the ACM on Management of Data",
    "year": 2026,
    "volume": "4",
    "issue": "1",
    "pages": "1-29",
    "doi": "10.1145/3786643",
    "status": "Published journal article; SIGMOD 2026",
    "published_print_crossref": "2026-04-02",
    "published_online_crossref": "2026-04-07",
    "landing_url": "https://dl.acm.org/doi/10.1145/3786643",
    "publisher_pdf_url": "https://dl.acm.org/doi/pdf/10.1145/3786643",
    "author_fulltext_url": "https://raw.githubusercontent.com/Lsyhprum/CDMG/9e6f3c5d14078efe1e1fbb9fa74227015645cba4/technical_report.pdf",
    "author_fulltext_revision": "Git commit 9e6f3c5d14078efe1e1fbb9fa74227015645cba4, 2025-05-08",
    "author_fulltext_authors": ["Qiang Yue", "Mengzhao Wang", "Xiaoliang Xu", "Cheng Long", "Jiahui Wang"],
    "author_fulltext_pages": 16,
    "code_url": "https://github.com/Lsyhprum/CDMG",
    "read_version": "Complete historical author technical report; formal-version body inaccessible",
    "arxiv_id": null,
    "metadata_url": "https://api.crossref.org/works/10.1145/3786643",
    "evidence_artifact": "artifact://73",
    "formal_body_gap": "ACM abstract/fullHtml/pdf/epdf/download variants returned HTTP 403; historical report is not asserted equivalent to the 2026 version"
  }
]
```

**版本核查要点：**

- MSG DOI 中的 `025` **不是出版年份**；正式PDF首页明确2026年接收、上线和卷期。
- 作者主页把 MSG 接收新闻写为2026年1月，正式PDF写 **Accepted 5 February 2026**；正式书目采用PDF，新闻只能作辅助线索。
- CDMG 作者主页记录2025年11月接收 SIGMOD 2026，故“2025接收 / 2026正式发表”不矛盾；历史报告更早，且少了正式版第五作者 Yuxiang Wang。
- CDMG README 的 `technical_report.pdf` 链接当前为404。通过[该文件历史](https://api.github.com/repos/Lsyhprum/CDMG/commits?path=technical_report.pdf&per_page=5)找到2025-07-31删除提交及其父提交，恢复上述可下载PDF。
- CDMG 正式[Crossref元数据](https://api.crossref.org/works/10.1145/3786643)摘要写 **“lower search time complexity”**，旧报告/README写 **“optimal search time complexity”**。不可将旧报告的“optimal”无条件写成正式版结论。
- 未确认两篇 arXiv 编号；这里 `null` 仅表示未确认，**不表示不存在预印本**。

---

# 二、MSG：正式全文阅读报告

原文：[正式PDF](https://files.sciengine.com/pdfs/1674-733X/7A6ED766DFBC4011B8009139AFCB45A0.pdf)。下述页码为 `192106:x` 的末尾页码。

## 1. 问题与 multi-vector 的确切含义

**§2.1，pp.3–4，Definition 2、Eq.(1)–(2)：**

每个对象有固定的 \(m\) 个字段/模态向量，查询激活其中一个非空子集 \(I\)，默认目标为

\[
d_w(o,q)=\sum_{i\in I} w_i\|o_i-q_i\|_2^2.
\]

查询动态性分为：

- **字段组合变化**：\(2^m-1\) 个非空组合；
- **逐查询权重变化**：同一查询向量、不同权重可以改变结果与路径。

这里比较的是**对应字段向量对**，不是 query token 对 document token 的跨集合最大值匹配。

特别有力的原文定位：**§8，p.17**明确将本文 MVS 与三类问题分开：
1. dense+sparse hybrid search；
2. ColBERT-style retrieval；
3. structured-attribute filtered search。

所以这篇的“多向量”**不能作为把当前任务改写成 MaxSim 或多模态加权检索的依据**。

## 2. 新颖机制的实质：共享什么，仍然物化什么

**§3.1–3.3，pp.5–6；Algorithm 1–3，pp.6–7：**

- 一个对象对应一个共享顶点；
- 每个对象的 \(m\) 个向量只存一份；
- HNSW式层级；
- **每层仍附带 \(2^m-1\) 套组合专属邻接列表**；
- 插入对象时，在组合空间获得候选，按 HNSW heuristic 选邻居；
- 在线查询按字段组合选择对应列表，仅沿该组合的连接遍历；
- 遍历中的邻居优先级按本次查询权重实时计算。

因此，“统一图”不是消除了组合拓扑，而是把全组合多图变成**共享数据载荷的多视图邻接结构**。

### 边共享与压缩的准确边界

**§4.3，pp.9–10，Fig.6–7、Algorithm 4：**

将同一顶点/层的所有组合邻居汇集成 `(neighbor ID, combination sign)` 序列，按 ID 排序，再做：

- Seq-ID：相邻差分，解码依赖前缀；
- Mini-ID：相对最小ID，支持直接恢复；
- Med-ID：相对中位ID，固定宽度编码，按排名恢复正负号。

查询检查 combination sign，仅解码本组合的邻居。

**不能直接宣称“同一物理边只存一次并附任意组合集合”**：正式算法描述的是 ID/sign 对，未给出将重复ID的所有组合合并成一个共享支持集合的机制。它首先是**向量共享、布局统一与邻居编码共享**。

## 3. 算法优化：哪些是机制，哪些主要是工程

| 内容 | 原文定位 | 判断 |
|---|---|---|
| 组合专属拓扑、共享顶点和向量 | §3，pp.5–6 | 核心表示/索引机制 |
| 单字段距离缓存，组合距离复用 | §4.2.1，p.7 | 正确的计算复用，主要是工程/算法执行优化 |
| 组合候选近似为各单字段候选的并集 | §4.2.1，Eq.(3)，p.7 | 实质性构建近似，不能混同为纯缓存 |
| 完整正交PCA变换＋部分平方距离提前退出 | §4.2.2，pp.7–8 | 距离下界执行优化，不是新检索语义 |
| 预存范数，用内积重建 squared-L2 | §4.2.3，p.8 | 标准代数/工程优化 |
| 顶点连续布局、delta ID编码 | §4.3，pp.9–10 | 存储与缓存优化 |
| 在线按高权重字段优先算部分距离 | §5.2，p.11 | 查询相关的执行次序优化 |

## 4. 理论究竟保证什么、不保证什么

**Lemma 1–2，p.4**：组合数与单向量参加组合次数的计数结论。

**Lemma 3，p.8**：缓存、正交变换下部分距离、范数内积展开不改变距离比较，属于**实数算术下距离执行等价性**。

**Lemma 4，p.9**：整数编码可逆，属于存储无损。

**Lemma 5，p.12**：查询加速不改变用于排名的精确分数，证明略。

**§4.4，pp.10–11**：

\[
T_{\rm MSG}
=O\!\left((\theta(n)m+2^{m-1}r)c_1\alpha_1D_mn\right),
\]

\[
S_{\rm MSG}
=O\!\left(((2^m-1)r\beta+D_m)n\right).
\]

**指数项仍存在**；消除的是向量重复以及组合空间重复遍历的一部分，不是得到任意 \(m\) 的线性空间索引。

### 必须保留的审慎意见

- **没有任意动态权重下精确 top-k 或严格召回保证。** 一种字段组合的固定图，在线更换权重，并不等于为连续权重空间中的每个度量建立了完美拓扑。
- **“lossless acceleration”不能覆盖候选并集近似。** p.7明确写 `approximate its candidate set`；p.8 Lemma 3证明的是距离缓存等，不是证明组合空间的近邻必在单字段候选并集中。
- 原文 p.4声称支持其他 monotonic aggregate、理由涉及相对排名；**分量单调并不意味着不同聚合函数维持相同排名**，只能说搜索程序可替换评分函数，不能由此继承原拓扑质量。
- 正交变换与内积展开在实数域等价，**不构成原始 FP32 逐维平方和的逐位等价保证**。当前任务若坚持原始FP32 squared-L2，应让转换值只作保守界或导航，最终按既定原始分数排序。

## 5. 实验核查与关键表

**§6.1，p.12，Table 2：**

- Recipe：1.3M、2向量、总2048维；
- MIT-States：2.1M、6向量、3456维；
- CelebA：1M、4向量、2304维；
- FashionIQ：1M、2向量、1024维；
- MS-COCO：1M、3向量、1536维；
- Shopping：1M、2向量、1024维；
- MIT-States+扩展：16M、2向量、1152维。

数据规模通过生成式增强扩充；query从base随机抽取；GT为暴力检索。主性能默认 **全部字段、固定权重**，动态权重和字段子集属于后续robustness实验。48线程建图、单线程查询。

**Table 3，p.13：**

| 数据集 | VBase Recall@10 / ms | MSG Recall@10 / ms |
|---|---:|---:|
| Recipe | .98 / 40.0 | .99 / 3.0 |
| MIT-States | .97 / 23.1 | .99 / 6.7 |
| CelebA | .99 / 159.2 | .99 / 6.5 |
| FashionIQ | .98 / 61.4 | .99 / 4.7 |
| MS-COCO | .92 / 91.8 | .93 / 11.6 |
| Shopping | .94 / 48.5 | .95 / 11.1 |

- **Fig.10–13，pp.14–15**：权重比例、逐查询权重、查询字段数、k变化。
- **Table 5，p.15**：m从1到6时邻居存储和构建时间。MSG邻居MB为198、460、990、2055、4146、8261，清楚显示组合存储并未消失。
- **Fig.15 / §6.5，p.15**：16M时“96.5% latency reduction”对照是VBase；不是普遍对任何ANN/过滤算法的96.5%收益。
- **Fig.16–18，pp.15–16**：构建加速、ID压缩、查询距离计算消融。
- **§7.2，pp.16–17**：明确承认 \(m>10\) 的组合爆炸；选择性物化高频组合列为未来工作。

实验不能证明层级、多谓词 AND、动态属性维护收益，也不应把图近似返回的高recall写成精确检索保证。

## 6. 对当前主线的可迁移性与重合风险

**可复用：**

- 将多个条件视图需要的几何邻接存储在**一个对象/向量载荷之上**；
- 用视图标记选择导航边，而不是复制所有向量；
- 把“导航视图”与“结果资格”分开：属性始终硬判定，距离始终原始FP32 squared-L2。

**不成立：**

- 把祖先/后代或标签、数值条件编码成加权语义向量，并允许距离弥补谓词不满足；
- 直接预建所有谓词组合：范围边界、标签组合和层级锚点不只是少量模态子集；
- 假设分别构建 subtree图与类别图后简单合并/求交就保留 AND 下的导航连通性。

**直接重合风险高的表述：**

> “一个统一图共享向量，按查询条件选择多套邻接，压缩这些邻接。”

这已经与MSG核心架构高度接近。当前若保留层级优势，真正需要新增的是：**边适用条件的可组合表示、AND下的导航/剪枝性质、祖先复用规则及其动态失效语义**，不能只换 combination sign 为目录标签。

### 一个可证伪灵感

**[INFERENCE，研究假设]**：为一条边保存可压缩的“层级适用区域＋属性支持条件”，对相同几何边的多个合法视图做因式分解；查询仍执行精确硬过滤与原始L2排名。

在**相同总字节、相同召回**下，与NodeANN、逐视图独立图、无因式分解ID/sign列表比较。若层级共享降低的边存储被标签/范围支持状态和更新失效开销抵消，或AND查询频繁切断必要路径，则假设被否定。

### 必要反例

1. **组合候选并集遗漏：** 查询两字段均为0；对象A=(0,10)、B=(10,0)、C=(6,6)。单字段top-1分别A、B，候选并集不含C；但平方距离和C=72，小于A/B=100。
2. **单调聚合不保排名：** 分量距离A=(0,10)、B=(6,6)。sum偏好A，max偏好B；二者均分量单调。
3. **字段组合一致、极端权重不同：** 固定同一组合，令权重从均衡变到近单字段，观测固定邻接的召回/访问数退化，不能只测温和比例。

---

# 三、CDMG：作者全文阅读报告

原文：[完整历史作者PDF](https://raw.githubusercontent.com/Lsyhprum/CDMG/9e6f3c5d14078efe1e1fbb9fa74227015645cba4/technical_report.pdf)。

**以下所有章节、页码、定理和实验值均指该16页报告。正式版正文差异未核实。**

## 1. OOD的定义与问题诊断

**§2.1，p.3：**

数据库 \(X\) 的向量独立同分布于 \(P_{\rm id}\)，查询独立同分布于另一个 \(P_{\rm ood}\neq P_{\rm id}\)。这仍是同一向量空间中的**单query ANN**，不是多向量集合检索。

**§1–2.2，pp.1–5：** 认为传统数据库几何图在OOD时有两类失配：

- navigability：查询远离数据库，既有边难以持续收敛；
- clusterability：同一OOD query的多个近邻在数据库自身几何中不够聚集。

**Table 2，p.4**比较query最近邻距离与数据库点间近邻距离，说明不能只把 \(\tau\)-MNG relaxation参数设大：太小不能满足接近假设，太大使图趋近KNNG。

**关键：这不是过滤定义。** 没有属性合法集、层级、AND谓词、零违规要求。过滤即使query与数据库完全ID，也能因合法点删去而破坏导航。

## 2. 跨分布单调性的确切含义

**§3.1，pp.5–6，Definition 2–3：**

每个数据库顶点关联一个query代表 \(q_i\)。路径分两段：

1. 数据库距离阶段：
   \[
   \delta(q,x_i)-\tau>\delta(q,x_{i+1}).
   \]
2. 关联query距离阶段：
   \[
   \delta(q,q_j)-\tau>\delta(q,q_{j+1}).
   \]

图顶点仍在数据库中，但第二阶段“单调”衡量的是**这些顶点关联的query代表到测试query的距离**。

令 \(n^*\) 为训练/代表集合 \(Q\) 中最接近测试query的query，\(n_1\) 为 \(n^*\) 的数据库最近邻。Definition 3要求：

\[
\delta(q_{\rm ood},n^*)\le\tau
\]

时，从任意入口存在到 **\(n_1\)** 的跨分布单调路径。

**重要边界：**

- 这不是每一跳的**原始query–数据库距离始终下降**。
- 定义的终点是最近query代表的数据库NN，**不自动等于测试query自己的真实数据库NN**。
- “任意OOD、任意训练覆盖、任意过滤后子图”的保证均不在定义内。

## 3. Query是否进入图？构建怎样使用训练query？

**§3.2，p.6，Algorithm 2–3：**

### 初始化
…

…

…
**给父任务的最终限制声明：MSG可标“正式全文精读”；CDMG应标“历史作者全文精读＋正式书目核验，正式版正文差异待核”，不能标“2026正式29页全文已精读”。**

[Showing lines 1-300 of 496. Use :301 to continue]