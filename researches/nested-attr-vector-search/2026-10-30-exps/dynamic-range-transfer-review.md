## Summary

**身份无误；迁移优先级为 Memento > Grafite。** 两者解决的是整数有序域上的**近似范围非空判断**，不是向量索引，也不提供 squared-L2 距离剪枝。

- **Memento**最值得迁移的是：**保留真实低位后缀＋对高位分区做指纹化＋可变长连续编码**，使范围过滤与插入、删除、扩容兼容；不能简化为“动态 Bloom”。
- **Grafite**最值得迁移的是：**局部性保留的随机宇宙缩减＋Elias–Fano 精确查询哈希值集合**，以明确空间预算约束相关查询下的 FPR；它是**静态结构**，动态化属于作者明确列出的开放方向。
- 对本项目，适合作**块级必要条件否定过滤器**。收益上限受“真实空块比例”和“边际非空但合取为空”的比例限制；即使每个过滤器 FPR 为零，也可能完全无法排除后一类空块。
- 建议候选是**稳定目录 ID 间接层＋几何块内动态属性 range filters**。首先尝试几何叶块，而不是以过滤器阴性直接切断 ANN 导航路径。

全程只读；未修改仓库、未下载到工作树、未运行作者代码或任何检查。

---

## 1. 可归档书目、版本及正文证据

| 项目 | Memento | Grafite |
|---|---|---|
| 完整题名 | **Memento Filter: A Fast, Dynamic, and Robust Range Filter** | **Grafite: Taming Adversarial Queries with Optimal Range Filters** |
| 作者顺序 | **Navid Eslami；Niv Dayan** | **Marco Costa；Paolo Ferragina；Giorgio Vinciguerra** |
| 正式出版 | *Proceedings of the ACM on Management of Data* **2(6)**，Article **244**，1–27，**2024**；SIGMOD 2025 对应论文 | *Proceedings of the ACM on Management of Data* **2(1)**，1–23，**2024**；SIGMOD 2024 |
| DOI | [10.1145/3698820](https://doi.org/10.1145/3698820) | [10.1145/3639258](https://doi.org/10.1145/3639258) |
| arXiv／本报告分析版本 | **2408.05625v3，2024-10-27**；初版 2024-08-10 | **2311.15380v2，2024-03-19**；初版 2023-11-26 |
| 可下载原文 | [固定 v3 PDF](https://arxiv.org/pdf/2408.05625v3)；[HTML](https://arxiv.org/html/2408.05625v3) | [固定 v2 PDF](https://arxiv.org/pdf/2311.15380v2)；[HTML](https://arxiv.org/html/2311.15380v2) |
| 原文 tool artifact | **`artifact://126`**：v3 完整 HTML 提取 | **`artifact://122`**：v2 完整 HTML 提取 |
| 正式书目核验 | **`artifact://124`**：[Crossref](https://api.crossref.org/works/10.1145/3698820) | **`artifact://125`**：[Crossref](https://api.crossref.org/works/10.1145/3639258) |

**版本纠正很重要：**最先定位的 Memento v1 仍有 DOI 占位符，且 Theorem 6.1 的 cluster-length 界与 v3 不同；不要把 `artifact://120` 的旧界并入 v3 报告。v3 首页已给正式 Article 244 元信息。Grafite 所读预印本 PDF 为双栏紧凑排版，以下页码均指**所链接版本的 PDF 页码**，不冒充正式 23 页版本页码。

### 正文定位

- **Memento v3**
  - §4，**pp.8–10**：分区、真实后缀、指纹及 keepsake-box 三种编码。
  - §4，**pp.11–14**：插入／删除、范围查询、长范围、bulk load、并发及变长键限制。
  - §5，**pp.14–15**：扩缩容、fluid fingerprints、最长匹配删除、chain、rejuvenation。
  - §6，**pp.16–17**：FPR、期望运行成本、空间。
  - §7，**pp.17–23**：独立过滤器及 WiredTiger 实验；§8 p.23 future work。
- **Grafite v2**
  - §2，**pp.3–4**：前序与动态更新尚未解决的说明。
  - §3，**pp.4–6**：哈希式 (1)、Lemma 3.1、Elias–Fano、Theorem 3.4、Corollary 3.5。
  - §6，**pp.8–13**：实验；§7，**p.13**：动态 Elias–Fano、字符串等 future work。

以上页码还通过 `answer_pdf_queries` 返回的 `<paper id="…"><page num="…">` 原文核验；该工具本次未返回单独 artifact 编号。

---

## 2. 两篇真实机制与保证

### 2.1 Memento：动态性来自“可搬迁的后缀记录”，不是仅把前缀塞进 Bloom

**输入／输出。** 固定长度整数键集合，闭区间 \([a,b]\)，主要保证针对长度不超过 \(R\) 的查询；输出 empty／possibly nonempty，无假阴性，允许假阳性。

**作者机制，§4 pp.8–13：**

1. 令 \(r=\lceil\log_2R\rceil\)，每个键拆为高位前缀 \(p(k)\) 与 **真实的 \(r\) 位低位后缀** \(m(k)\)。
2. 高位前缀对应宽度 \(2^r\) 的宇宙分区。因此长度 \(\le R\) 的查询最多涉及两个分区。
3. 哈希前缀，产生 RSQF 的 canonical slot 和 fingerprint；**不是对完整键做破坏顺序的普通哈希**。
4. 同 slot、同 fingerprint 的后缀保存在一个连续 **keepsake box**。哈希碰撞可能使不同真实分区进入同一 box。
5. 后缀排序保存。一个成员直接存；两个成员各带指纹；多成员用零指纹 escape 标记、把最小／最大后缀放在前两个 slot，剩余后缀紧密编码并携带变长长度。
6. 单分区查询做后缀 predecessor 检查；跨两个分区只需相应最大／最小后缀。较长查询可以检查更多分区，**代价是更高 FPR 和查询成本**。

**更新。**

- 插入／删除修改后缀记录，RSQF Robin Hood 布局可能产生 slot 平移；不是最坏情况恒定时间。
- 保留碰撞记录的重数很关键。调用者必须依据真实对象状态执行删除，不能把过滤器“可能存在”当成允许删除未知键的证明。
- §5 扩容扫描旧过滤器，将 fingerprint 一位转成地址位，slot 数翻倍；缩容反向操作。因此**一次扩容不是 \(O(1)\)**，要迁移整个结构。
- fluid fingerprint 携带年龄；新插入用完整指纹；删除选择**最长匹配指纹**，查询检查全部匹配的 fluid fingerprints。
- 单层最多牺牲 \(f-1\) 次指纹位；无限增长依靠次级 filter chain，查询和删除要检查链中各过滤器。
- rejuvenation 需要重新取得真实键，不能仅凭过滤器假阳性直接“刷新”。

**理论，§6 pp.16–17。** 以 \(\ell\) 表示非空分区平均键数、\(\alpha\) 为论文定义的负载参数、\(f\) 为指纹位数：

\[
\epsilon_{\text{point}}\le \frac{\alpha}{\ell}2^{-f},\qquad
\epsilon_{\text{range}}\le \frac{\alpha}{\ell}2^{1-f}.
\]

发生 \(E\) 次扩容的版本给出：

\[
\epsilon_{\text{expandable}}\le
(E+2)\frac{\alpha}{\ell}2^{-f}.
\]

**不能说扩容后固定参数 FPR 完全不变**：其保证显式依赖 \(E\)。基本版空间式为

\[
\frac{1}{\alpha}\left(3.125+\log_2\frac{R}{\epsilon}\right)
\quad\text{bits/key},
\]

expandable 版多一位年龄开销，常数为 4.125；若按整个生命周期指定最终 \(\epsilon\)，还须按 \(E\)-相关界选指纹预算，不能只套基本版式子。

期望插入／删除 \(O(\ell)\)，查询 \(O(\log\ell)\)；“约一次／两次随机 cache miss”不是总内存访问次数或严格最坏延迟保证。偏斜并发对同 box 的争用仍是作者 future work。

### 2.2 Grafite：把范围哈希到小宇宙，再精确查哈希集合

**输入／输出。** 静态整数集合 \(S\subseteq[0,u)\)，范围非空判断，同样无假阴性；没有对象返回或满足合取的见证对象。

**作者机制，§3 pp.4–6：**

设 \(r=nL/\epsilon\)，从 pairwise-independent 家族选 \(q\)，构造

\[
h(x)=\bigl(q(\lfloor x/r\rfloor)+x\bigr)\bmod r.
\]

- 同一原宇宙分区内，映射相当于循环平移，保留范围局部性；不同分区采用随机偏移。
- 区间跨原分区边界要拆分，模运算环绕也要正确处理，不能简单只比较两个哈希端点。
- 对 \(h(S)\)**排序、去重**，以 Elias–Fano 保存：低位密集数组＋高位 bitvector／select。
- 在哈希区间上做 predecessor，精确判断是否有哈希值；假阳性来自宇宙缩减碰撞，而非 Elias–Fano 本身。
- 这里“高低位编码”是**哈希值的压缩表示**，与 Memento 保留原键真实后缀不同。

Lemma 3.1 对长度 \(\lambda\le L\)：

\[
P(\mathrm{FP})\le n\lambda/r=\lambda\epsilon/L.
\]

Theorem 3.4：

\[
n\log_2(L/\epsilon)+2n+o(n)\text{ bits},\quad
T_q=O(\log(L/\epsilon)),
\]

并有 \(L\le u\epsilon/n\) 等条件；空间最优指匹配 approximate-range-emptiness 下界的主项／加性线性项尺度，**不是任意实际数据上压过精确压缩集合**。

Corollary 3.5 固定 \(B=O(1)\) bits/key：

\[
P(\mathrm{FP})\le\min\{1,\lambda/2^{B-2}\},\qquad T_q=O(B)=O(1).
\]

**限制。**

- 查询越长，预算固定时保证越弱；\(\lambda\ge2^{B-2}\) 时上界退化到 1。
- 这是对随机哈希选择的概率保证；不应升级为已证明抵抗观察过滤结果后自适应寻找碰撞的密码学攻击。
- **静态**。构造需要排序及 Elias–Fano 构建；插入、删除、扩容不是论文已实现保证。p.13 明确把“dynamic Elias–Fano 帮助支持插入”列为开放方向。
- Bucketing 是另一个**无分布无关 FPR 保证**的启发式方案，不应把其性质混为 Grafite。

---

## 3. 作者实验实际说明了什么

| | Memento v3 §7 | Grafite v2 §6 |
|---|---|---|
| 数据 | 各 200M 的 Uniform、Normal、Books、OSM | 主要为各 200M 的 Uniform、Books、OSM |
| 查询 | 10M；点、\(2^5\)、\(2^{10}\) 长度；独立／靠近真实键的 correlated／真实数据工作负载 | 同样 10M，单线程；点、\(2^5\)、\(2^{10}\)；专门生成空范围测 FPR |
| 基线 | SuRF、Rosetta、REncoder、Proteus、SNARF、Oasis+、Grafite；增长实验为 Rosetta、REncoder、SNARF | Bucketing、SuRF、Rosetta、SNARF、Proteus、REncoder 多配置 |
| 指标 | FPR、查询／构造／插入成本；增长过程；WiredTiger 查询和插入成本 | FPR、空／非空查询 ns、构造 ns/key、空间；约 8–28 bits/key |
| 动态证据 | Uniform 从 1/64 数据增长至全集，每次翻倍后测 10M 查询；20 bits/key | 无动态实证 |
| 系统证据 | WiredTiger：100M 子集，512B KV，总内存预算相同；15 bits/key filter 从 buffer pool 分配。50% 空查询时约 **1.9×** 查询提升，插入约 **2.5%** 开销 | 查询微基准不包含后续磁盘／网络资源访问成本 |

**局限：**Memento 的上述增长实验不能证明高删除率、频繁属性换值、目录 MOVE、块拆分／合并的完整成本；两篇都没有给本项目外部层级＋多属性合取＋FP32 L2 top-k 实验。Grafite “up to 92×” 等是特定过滤器微基准比较，不可转换为向量检索加速预测。

---

## 4. 可迁移性、反例与安全边界

### 排序

1. **Memento：优先。** 适合不断变化的几何叶块中，问“该属性范围是否可能出现”；主要价值是相关端点下可控 FPR 和删除／增长能力。
2. **Grafite：第二。** 适合冻结、低更新或可批量重建的块；可作静态空间／FPR 强对照。若加 delta filter，那是**我们的新组合**，不是作者已提供动态性。

### 必须区分的三种“误差”

设块为 \(G\)，目录条件 \(D\)、类别 \(C\)、范围 \(A\)、多值标签 \(T\)。

**安全必要条件：**

\[
\exists x\in G:D(x)\land C(x)\land A(x)\land T(x)
\Rightarrow
\bigwedge_j\exists x\in G:P_j(x).
\]

所以任意边际过滤器回答 empty，便能否定块存在合格结果；全部回答 positive，**不能证明合取非空**。

**反例：**块有两个对象：

- \(x\)：在目标 subtree、红色、价格 100；
- \(y\)：不在目标 subtree、蓝色、价格 10。

查询目标 subtree AND 蓝色 AND 价格 \([0,20]\)。每个边际都真实非空，但没有合格对象。**这不是 range-filter 哈希假阳性，而是丢失对象共现信息的结构性放行。** 多值标签 “含 a 且含 b” 可以由两个对象分别见证，也有同样问题。

**多层概率：**祖先／子块集合嵌套、查询相关、哈希种子可能共享，故不能写 \(p_{\text{path}}=\prod_i p_i\)。应测联合放行和条件放行率；未证明独立时用适用的 union bound 或直接报告实测，且边际真实阳性时单过滤器 FPR 界根本不约束合取放行率。

**ANN recall：**

- 对覆盖对象集合的几何叶块，empty 可安全跳过该块的结果扫描。
- 对邻接块，若它只覆盖一跳邻居，则只能否定这些邻居是合格结果，**不能证明经过它们无法到达远处合格点**。以此截断导航可能损失 ANN recall，哪怕过滤器零假阴性。
- 原始 FP32 squared-L2 距离、top-k 及停止条件仍由搜索器处理；range filters 不产生向量距离界。

---

## 5. 一项具体迁移候选 `[INFERENCE]`

### 稳定目录间接层的“块级动态范围否定目录”

**存什么**

以现有几何**叶块** \(G\) 为单位：

1. 原始对象 ID／属性及精确验证数据仍在现有系统。
2. 每个数值属性存 Memento \(F_{G,j}\)，插入每个对象对应的整数有序编码；重复值保留重数，或另有精确引用计数。
3. 类别／标签用点成员过滤或精确小集合；不强行为无序标签赋予有意义的范围距离。
4. 存块内对象的**稳定直接目录 ID** 集合／计数，不存祖先闭包，不把当前 DFS 序写进每个属性键。
5. 外部目录树独立维护 parent／child 关系。subtree 查询编译为当前后代稳定目录 ID 的集合，按稳定 ID 数值序压成**精确、不跨空隙**的区间；对目录 range filter 查询这些区间。区间多时可用精确集合／bitmap 对照，不宣称 subtree 总能变成一个区间。

**查询**

- 目录条件：所有后代 ID 区间均 empty 才能拒绝块。
- 属性条件：任一必要原子谓词被安全否定则拒绝块；OR 组要所有分支都否定，标签 ALL／ANY 按原语义组合。
- 剩余块交给相同向量搜索与逐对象精确合取验证。
- 不利用阴性切断未被该块集合覆盖的导航节点。

**更新**

- 属性变更：按真实旧值删除一次、新值插入一次；标签集合差量更新。
- 对象迁块／删除：更新源、目标叶块相应记录。
- **subtree MOVE：仅更新目录拓扑／查询编译视图；稳定直接目录 ID、对象属性键和几何块过滤器不重编码。**
- 代价没有消失：稳定 ID 不保证后代连续，MOVE 后查询可能产生更多区间；必须计入目录展开、排序／压缩或缓存失效成本。目录查询视图与属性摘要要使用一致版本，未就绪不能返回“确定空”。
- 几何块拆并时重建受影响块摘要，成本归入更新。

**相对现有方法**

| 对照 | 可检验差异／主要重合风险 |
|---|---|
| 精确 bitmap | 不先生成所有 qualified IDs，按访问块做近似范围否定；但不提供对象级合取见证。稠密属性和高共现歧义时 bitmap 很可能更强。 |
| 普通 Bloom | 无需枚举范围每个值或所有前缀；保留真实后缀处理相邻端点，支持记录级删除。类别／点谓词部分则大量重合。 |
| Curator | 不先拿完整 qualified IDs 构造 virtual-label tree；试图直接在线测试数值范围和块必要条件。但**共享几何树、Bloom 摘要、删除后摘要重算、虚拟复杂谓词索引／缓存已是其前序**。 |
| EMA | 从属性分桶 OR 改为原值后缀＋指纹、可删除范围摘要；先限定叶块对象集合，而非直接依赖边支配邻域。**区域属性摘要用于过滤**高度重合，且两者都受共现歧义限制。 |

Curator／EMA 对照依据本地已核审读，位置如下；本次未重新完成两篇的独立全篇核验。**“把 Bloom 换成 Memento”本身不足以认证新颖性**；可研究点应是稳定目录间接层下的联合工作量、摘要粒度和更新／查询成本边界。

---

## 6. 最小判别实验 `[INFERENCE，未执行]`

固定向量、对象属性、查询流、几何分块、FP32 squared-L2 与搜索预算；不要因对照不同重新生成属性。

### 对照组

1. 无摘要、原搜索器精确属性检查。
2. 精确块内有序值集合／Elias–Fano，以及 bitmap 合取。
3. 等内存 prefix/counting-Bloom 范围方案。
4. Memento 动态方案。
5. Grafite 冻结块：把重建时间、delta（若加）和峰值内存算全。
6. EMA 式 OR＋周期／patch 修复，Curator 式局部重算摘要对照。

### 最小工作负载轴

- 块大小至少小／大两档；属性与几何相关／独立两档。
- 短／长数值区间，随机端点／紧贴已有值的空区间。
- 增删换值流，标签 churn，连续扩容，热点偏斜。
- 浅／深 subtree、大片 subtree MOVE。
- 特设**边际都非空而同对象合取为空**数据，排除仅靠单属性 FPR 展示优势。

### 必报指标及分母

| 指标 | 定义／目的 |
|---|---|
| 空间 | 总常驻、峰值、bytes/object、bytes/block；包含各属性、重数、目录间接层、缓存、delta、重建双份内存 |
| 真实空块率 | exact 合取为空的被访问候选块／全部被访问候选块 |
| 空块命中率 | 被拒绝的真实空块／全部真实空块 |
| negative rejection | 全部被拒绝块／全部候选块；与上述指标分开 |
| 边际 FPR | 针对该边际确实为空的测试，positive 比例 |
| 共现歧义放行 | 边际都真实非空但合取为空的块比例，以及其成本 |
| 层间相关性 | 联合 positive、条件 positive，不能报告理论独立乘积 |
| 查询全成本 | subtree 编译、filter probes、精确验证、向量距离次数、块读取、导航／恢复、p50/p95/p99、QPS |
| 更新全成本 | 删除＋插入、反向定位、slot 平移、扩缩容、chain、块拆并、目录拓扑／缓存失效；均值和尾延迟、更新吞吐 |
| 正确性／质量 | 摘要阴性误拒对象数必须 0；最终非法结果 0；ANN recall 相对精确过滤 FP32 L2 GT 独立报告 |

**最小决策条件：**若精确空块很多、但主要是跨对象合取歧义，单维 range filters 再降 FPR 也可能无收益；若过滤器省下的块扫描／距离计算小于目录展开、过滤探测和维护成本，也应判为不值得，而不是继续只调指纹。

**MOVE 专项验收：**移动一个大 subtree，记录对象键重编码数应为 **0**；同时报告新增查询区间数、目录视图构造／失效开销。否则只是把全量失效藏到了排序键或查询预处理中。

---

## Files / Architecture / 代码可用性

### 作者代码：已读、未运行

- [Memento 仓库](https://github.com/n3slami/Memento_Filter/)：GPLv3。
  - `include/memento.h`，**`artifact://127`**：`qf_insert_single`、`qf_delete_single`、`qf_range_query`、`qf_resize` 等 API。
  - `src/memento.c`：仓库目录列出的 C 实现。
  - README 区分 `master` 动态版、`expandable` 扩容／B-tree 实验版、`header-only` 模板版；因此不能只取默认分支就声称复现 expandable 论文。
  - API 注释明确并发特性**未充分测试**。README 分支描述与当前头文件可见 resize API 不完全对应，实际能力应固定 commit 后再核实现，不能依据名字断言。
- [Grafite 仓库](https://github.com/marcocosta97/grafite)：GPLv3。
  - `include/grafite/grafite.hpp`，**`artifact://128:403-645`**：`filter(begin,end,eps,L)`／`filter(begin,end,bpk)`、`query(left,right)`；静态哈希＋Elias–Fano。
  - `lib/sux`／`lib/sdsl-lite`：Elias–Fano 后端；默认 SUX。
  - `bench/filters_benchmark/`、`bench/reproducibility.md`：基准与复现说明。

### 本地对照材料

- `researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/literature-mechanisms-review.md:20-29`：Curator 共享几何树、复杂谓词虚拟视图、删除递归重算。
- 同文件 `:68-76`：EMA 邻域 OR 摘要、属性更新／lazy 删除／patch、共现与召回限制。
- `researches/nested-attr-vector-search/2026-10-30-exps/index-mechanism-portfolio.md:27-34`：已有组件复用与贡献定位。

**架构落点：**作者结构应放在“外部目录／属性谓词 → 已访问几何块必要条件 → 原搜索器／精确验证”之间，而不是替代向量距离、对象合取验证或 ANN 可达性机制。