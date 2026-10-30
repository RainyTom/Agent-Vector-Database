# 01｜先固定查询语义，再讨论索引

状态：首轮原理学习；数学例子有[可运行探针](2026-10-30-exps/experiments/semantic_probes.py)，不是数据库性能实测。知识记录日期2026-10-30。

## 1. 三层对象：表示、排序、资格

输入向量不是天然的语义真值。embedding模型、版本、维度、归一化、chunk粒度和语料分布共同决定表示；索引只对给定表示及距离做近似查找。模型表达错误、候选未召回、reranker误排、生成器错误应分开诊断。

固定数据版本v、查询q、资格谓词F、k，定义：

$$E_v(F)=\{i:Live_v(i)\land Scope_v(i,F)\land Extra_v(i,F)\}.$$

精确GT是E中的前`min(k,|E|)`个对象，按`(distance(q,x_i), external_id)`升序。这里用外部ID打破并列是**本仓库评测合同**，不是所有产品共同规定。上层排序分数越大越好时需反转比较方向。SQL的NULL、缺失字段、NaN、零向量必须明确处理，不默认等于普通值；项目自定义tag和−1哨兵也不自动等于SQL语义。

exact资格与approximate排序可以同时成立：返回对象均合法，不表示真实近邻都被召回；反之，向量排序准确也不能抵消权限或scope错误。

## 2. 名字相似，实际不是同一个问题

| 检索问题 | 输出 | 不能混用的结论 |
|---|---|---|
| 单向量ANN | 一个q的近似top-k对象 | 不是vector join |
| filtered ANN | 满足硬谓词F的近似top-k | 不是先全局top-k再随意删减 |
| sparse+dense hybrid retrieval | 多路相关性融合后的对象 | BM25加权分数不是布尔资格 |
| multi-vector / vector-set search | 对象含多个向量，按MaxSim/Hausdorff等聚合排序 | 单向量索引支持多列，不代表支持该聚合 |
| geometric vector join | 两集合或self集合间的threshold pairs、per-query kNN或global top-k pairs | 逐query的k个结果不等于全局k对 |
| semantic/LLM join | 由语义判定器定义连接关系 | 不等于几何距离阈值 |

阅读论文先写输入、输出、exact/approximate、距离、过滤语义和计数方式。现有分类与本地原文从[论文库](../../papers/README.md)、[vector join](../../papers/vector-join/README.md)、[filtered ANN](../../papers/filtered-ann/README.md)、[multi-vector](../../papers/multi-vector-search/README.md)进入，不复制一套论文记录。

## 3. L2、squared-L2、IP、cosine

[Faiss官方MetricType说明](https://github.com/facebookresearch/faiss/wiki/MetricType-and-distances)的`METRIC_L2`返回平方L2，不做平方根：

$$D_2(q,x)=\sum_j(q_j-x_j)^2,\qquad d_2(q,x)=\sqrt{D_2(q,x)}.$$

二者排序一致；阈值、球界、残差界却必须统一单位。例如`D2=9`与欧氏阈值`r=2`比较应使用`9 > 4`，而不是把不同单位直接比较。历史剪枝审计见[交接阅读清单](../nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/upstream-reading-list.md)。

最大inner product排序为最大`q·x`。只缩放q的正比例大小不改变MIPS顺序，缩放不同库向量会改变顺序；因此MIPS不是自动等于cosine。

对非零向量归一化后：

$$\hat q=q/\|q\|,\quad\hat x=x/\|x\|,\quad \|\hat q-\hat x\|^2=2-2\hat q\cdot\hat x.$$

这时cosine、normalized IP与squared-L2给出同样的精确顺序。**归一化改变了原始MIPS问题**，不能因为索引方便就默默做。零向量不能按这个式子归一化；应遵守具体后端的拒绝/跳过/特殊处理合同。

产品API名字也要核对：[pgvector v0.8.0](https://github.com/pgvector/pgvector/blob/v0.8.0/README.md#querying)中`<->`是L2距离、`<#>`是negative inner product、`<=>`是cosine distance。不能把Faiss L2输出或hnswlib L2输出的单位套到SQL操作符。

## 4. 降维、旋转、量化分别改变了什么

- 完整正交旋转保持L2距离；保留全部PCA坐标、只做同一平移/正交旋转时几何保持。
- 截断正交投影只保证投影平方距离不大于原空间距离，不保证最近邻排序不变，也不保证有足够紧的剪枝下界。某些方向丢失后，再精巧地重组保留坐标也恢复不了缺失信息。
- whitening改变度量；不是“更好PCA但原问题不变”。Faiss文档将其解释为Mahalanobis与变换空间L2的联系。
- PQ/SQ/二值编码改变距离估计；原向量rerank只能修正**已经进入候选集**的排序，不能找回未进入的对象。
- learned embedding与压缩索引是两个训练环节；两者都需要检查训练/查询分布是否一致，不能只记录最终维数。

本项目WIKI低维联合界负结果的正确用法是暴露“此表示信息不够”，而非推出“所有条件几何索引无效”。现有结果与实现入口见[层级方案交付](../nested-attr-vector-search/2026-10-30-exps/closure-server-plan.md)。

## 5. 稀疏、二值和多向量不是免费换壳

倒排适合稀疏表示，因为非零坐标或token能组织候选；稠密向量硬转稀疏后要支付编码、候选漏召、原空间验证和更新成本。哈希碰撞不是原距离下界；OR-sketch丢失同对象关系时，多个对象可能分别满足各片段但没有单对象满足联合条件。

对于一个document多个token向量，`sum_i max_j q_i·d_j`与最大单对距离、均值向量距离、Hausdorff完全不同。路由、去重及top-k合并需按对象聚合。具体作品继续沿用[BioVSS/MSG主题索引](../../papers/multi-vector-search/README.md)，不因“都用了多个向量”合并算法保证。

## 自测与最小动作

1. 在未归一化例子`q=(1,0), a=(10,10), b=(1,0)`中，IP与cosine分别选谁？解释为何不能直接替换。
2. 为什么投影距离0不能证明原始距离0？给出丢失维度的两对象例子。
3. 从一个实验配置找出：模型/向量版本、距离单位、归一化、k、tie、空结果、外部ID定义。
4. 运行[语义探针](2026-10-30-exps/experiments/semantic_probes.py)，对照JSON中的metric和candidate-loss例子。它只是数学语义演示，不是ANN实现或浮点通用认证。

## 来源与未完成

- Faiss `MetricType and distances`：METRIC_L2、METRIC_INNER_PRODUCT、cosine、Mahalanobis；本轮已读，活wiki未冻结。
- pgvector README固定tag v0.8.0：Querying/Distances、Indexing、HNSW；本轮已读，不代表已部署Postgres。
- 论文分类、现有诊断通过仓库已有索引关联；本轮未新增论文、未把技术文档当精读论文。
- 待核：不同系统的零向量/NULL/NaN、量化和tie的具体实现；不要由公式替产品API兜底。
