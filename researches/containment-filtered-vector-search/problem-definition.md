# 前缀属性约束向量检索的问题定义

更新日期：2026-10-19（Asia/Shanghai）。本文件是当前单路径前缀问题的规范定义；早期无序包含/多路径overlap模型保留为背景，不替代本定义。创新性判断见[创新性复核](novelty-analysis.md)。

## Introduction

代码库、企业文档和分类知识库通常同时具有向量表示与路径式属性。例如，代码片段位于某个项目模块，文档位于某个业务目录，商品位于某条分类路径。用户既希望按向量相似性检索，也希望将结果限制在指定路径及其后代范围内。对路径 $(A,B,C)$，查询前缀 $(A,B)$ 表示搜索整个AB分支，而不是仅检查对象是否同时带有无序标签A与B。

此类需求已由 Directory-Aware 的 recursive DSQ 直接研究；通用过滤ANN也可通过完整前缀标签或范围编码表达。本文档不将其视为新提出的查询语义。需要研究的是：在精确满足前缀约束的前提下，如何以受限索引空间高效完成近似向量top-k检索，并判断前缀集合的嵌套与互斥结构是否带来超出现有编码和过滤执行器的增量。[Directory-Aware §II–IV](https://arxiv.org/pdf/2606.16903v1)。

## Problem Definition

**定义1（数据对象与属性路径）。** 令 $\Sigma$ 为属性片段全集，$\Sigma^*$ 为其有限序列集合。数据集为

$$
\mathcal D=\{o_i=(\mathrm{id}_i,\mathbf x_i,s_i)\}_{i=1}^{n},
\qquad \mathbf x_i\in\mathbb R^d,\quad s_i\in\Sigma^*.
$$

假设 $n\ge1$。对象ID唯一且具有固定全序。每个对象绑定一条路径；不同对象可具有相同路径或向量，路径可终止于内部节点，也可为空。片段按属性节点解释，不将字符级前缀、同名末端节点或跨路径拼接当作路径相等。

固定向量不相似度函数 $\delta:\mathbb R^d\times\mathbb R^d\to\mathbb R_{\ge0}$，距离越小越近。本定义不额外假设三角不等式；若采用cosine或其他度量，其归一化和数值计算约定在实验中固定，不能用属性混合导航分数替代最终向量排序。

**定义2（前缀关系）。** 对路径 $p=(b_1,\ldots,b_\ell)$ 与 $s=(a_1,\ldots,a_m)$，定义

$$
p\preceq s
\quad\Longleftrightarrow\quad
\ell\le m\ \land\ \forall j\in\{1,\ldots,\ell\},\ b_j=a_j.
$$

允许相等及空前缀 $\epsilon$。例如AB匹配AB和ABC，不匹配BAC、ACB或XAB；/a/与/ab/是不同片段路径，不能直接按裸字符串开头混淆。

**定义3（前缀可行集合）。** 给定查询前缀p，可行对象集合为

$$
\mathcal D_p=\{o_i\in\mathcal D:p\preceq s_i\},
\qquad n_p=|\mathcal D_p|,\qquad \sigma_p=n_p/n.
$$

$\sigma_p$ 为合格比例（pass ratio，假设 $n>0$），不是ANN召回率。$\mathcal D_\epsilon=\mathcal D$；未出现在任何对象路径中的前缀具有空可行集合。确定 $\mathcal D_p$ 的物理表示称为范围解析（scope resolution），可返回bitmap或集合视图，不要求逐个复制全部ID。

**定义4（精确前缀约束top-k查询）。** 查询为 $Q=(\mathbf q,p,k)$，其中 $\mathbf q\in\mathbb R^d$、$p\in\Sigma^*$、$k\in\mathbb N_{>0}$。令 $k_p=\min(k,n_p)$，按 $(\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i)$ 排序后的前 $k_p$ 个对象构成精确答案

$$
R^*(Q)=\operatorname{TopK}_{o_i\in\mathcal D_p}
\bigl((\delta(\mathbf q,\mathbf x_i),\mathrm{id}_i);k_p\bigr).
$$

当 $n_p<k$ 时，规范答案是全部合格对象；当 $n_p=0$ 时为空。结果按对象ID去重，排序仅由原向量距离与ID并列规则决定。本问题是单查询对象top-k，不是全局top-k pairs或vector join。

**定义5（近似检索与质量）。** 近似版本允许遗漏真实近邻，但不允许放宽前缀条件或返回重复对象。目标返回 $k_p$ 个合格对象；有限预算实现若返回更少，应记录为欠填（underfill），不能通过缩小评测分母规避。

对于返回集合 $R(Q)\subseteq\mathcal D_p$、$|R(Q)|\le k_p$，当 $k_p>0$ 时定义

$$
\operatorname{Recall}@k(Q)=\frac{|R(Q)\cap R^*(Q)|}{k_p},
\qquad
\operatorname{Fill}(Q)=\frac{|R(Q)|}{k_p}.
$$

空真值查询不纳入上述均值，单列空结果正确率及延迟；若其返回不合格对象，应计过滤错误。$0<n_p<k$ 时仍以 $n_p$ 为分母，欠填会反映在召回/填满率中；完成规范输出可能需要精确扫描回退，回退成本必须计入。硬过滤正确性、填满率与召回分别报告。

**定义6（预算约束下的索引检索目标）。** 令 $\mathcal W$ 为评测查询分布或固定工作负载，$\mathcal W_+$ 为其中非空真值查询，$\mathcal I$ 为索引。给定总内存预算B与平均召回目标 $\rho$，一个基础优化目标为

$$
\min_{\mathcal I,\mathcal A}
\mathbb E_{Q\sim\mathcal W}[T_{\mathrm{e2e}}(\mathcal A,\mathcal I,Q)]
$$

$$
\text{subject to}\quad
\operatorname{Mem}(\mathcal I)\le B,
\quad
\mathbb E_{Q\sim\mathcal W_+}[\operatorname{Recall}@k(Q)]\ge\rho,
\quad R(Q)\subseteq\mathcal D_p.
$$

总内存包括向量、图/分区、属性路径、bitmap/posting、路由、边证书和缓存；查询临时空间与峰值另报并受相同资源约束。端到端时间包括前缀解析、合法集合/入口准备、近邻导航、验证和排序。构建、离线profile与更新成本单列。若 $\mathcal W_+$ 为空，召回约束不适用，只报告空查询行为。平均召回目标不是逐查询概率保证，同时报告召回分位数、欠填、P95及过滤违规；若业务要求最差查询保证，应另行定义并验证，不能由此式推出。

## 前缀结构的性质与已有归约

**命题1（可行集合的层状性）。** 若 $p\preceq r$，则 $\mathcal D_r\subseteq\mathcal D_p$；若p与r互不为前缀，则 $\mathcal D_p\cap\mathcal D_r=\varnothing$。因此单路径前缀可行集合构成laminar family。

证明：同一对象路径的两个前缀必然可比较；较长前缀成立时，所有较短前缀都成立。该性质来自定义，不是新的ANN召回定理。

**命题2（完整前缀单标签归约）。** 令 $F(s)=\{\operatorname{id}(r):r\preceq s\}$，其中完整路径具有规范唯一标识，空前缀可单独按全局查询处理。则

$$
p\preceq s\quad\Longleftrightarrow\quad \operatorname{id}(p)\in F(s).
$$

因此普通单标签过滤ANN已经能准确表达本谓词。逻辑成员数随路径深度增加，但这不等于物理字节或向量必须复制；压缩bitmap与共享索引必须作为强对照。

**命题3（静态区间归约）。** 将路径组织为Trie，DFS时先枚举节点直接绑定对象，再枚举各子树对象；同节点对象按ID确定顺序。令 $r_i$ 为各对象的连续序号，则每个非空前缀范围对应一个半开区间 $[l_p,u_p)$：

$$
p\preceq s_i\quad\Longleftrightarrow\quad l_p\le r_i<u_p.
$$

内部节点对象与相同路径的多个对象仍保持该性质。未出现前缀对应空范围，空前缀对应全区间。于是单路径静态问题是受限区间族上的range-filtered ANN，可直接比较DFS+iRangeGraph等方法。属性顺序与向量几何顺序一般不同，不能据此断言几何分区也连续。动态MOVE可能破坏当前DFS序号，不在静态归约中免费解决。

上述结构与归约使问题可被已有方案执行，也约束创新主张；不会因为名称改为prefix search就产生新算子。

## Naive Solutions

**方案1：范围解析后精确扫描。** 先用Trie/bitmap取得 $\mathcal D_p$，再计算全部 $n_p$ 个对象的距离并选top-k。距离计算量为 $n_p$ 次，是精确真值和低合格数时的重要实际基线；scope读取、内存访问和top-k维护另计。

**方案2：全局ANN后过滤。** 先取全局前L候选，再检查前缀并截取k。固定L可能欠填或漏掉范围内近邻；提高L或多轮扩大候选增加成本。不能将其作为现代过滤ANN的唯一代表。

**方案3：每个前缀独立索引。** 为每个非空 $\mathcal D_p$ 独立建ANN，直接选择p索引搜索。每对象出现在多个祖先索引中，成员/图结构与构建工作会增长；即使底层向量共享，额外邻接与成员也需计算。该方案是完全物化对照，不等于ELI等选择性方案的完整能力。

## 必须比较的已有执行方案

TrieHI或PE-Offline生成scope，再搭配精确扫描、过滤HNSW、ACORN、PathSeer，是强基线组合。还需比较完整前缀单标签编码的适用方法、DFS-range归约，以及共享几何视图的Curator。它们不是本问题的Naive Solutions。

先固定ANN执行器比较scope表示，再固定相同scope比较导航结构。属性判定能跳过结果接纳，不代表可以不访问该对象的出边：不合格对象可能是合格区域的桥。不同策略能否省距离、跨过中间节点或构造专用连接，属于物理执行设计，不能混入谓词定义。

基础研究先采用静态单路径模型。多路径存在性匹配会破坏对象集合层状性；目录维护、通配符或更多布尔谓词应在真实需求成立后单独扩展，不通过堆叠条件制造新颖性。
