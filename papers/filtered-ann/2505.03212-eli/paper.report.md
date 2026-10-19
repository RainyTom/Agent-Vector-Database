# Elastic Index Selection for Label-Hybrid AKNN Search

核查日期：2026-10-18（Asia/Shanghai）。B 类可迁移借鉴，原问题为 label-filtered AKNN，不是 vector join。

## 书目与版本

作者顺序：Mingyu Yang、Wenxuan Xia、Wentao Li、Raymond Chi-Wing Wong、Wei Wang。

正式稿：[PVLDB 19(4):617–630](https://www.vldb.org/pvldb/vol19/p617-yang.pdf)，论文首页引用格式为 **2025**，DOI [10.14778/3785297.3785304](https://doi.org/10.14778/3785297.3785304)。Crossref 列 published-print=2025-12-01、published-online=2026-03-18；不得只取 online 年覆盖刊年。会议 VLDB 2026。预印本 v1=2025-05-06，所读 [2505.03212v2](https://arxiv.org/pdf/2505.03212v2)=2025-12-11，共 18 页；正式稿 14 页，附录证据以 v2 为准，不将预印本附录当作正式稿已有。

本地 [paper.pdf](paper.pdf)/[paper.md](paper.md) 为 v2；[published-2025.pdf](published-2025.pdf)/[文本](published-2025.md) 单独保留正式稿。文本为 PyMuPDF 提取，公式表格需回看 PDF。

## 问题与方法

数据为向量与标签集合，查询为向量、查询标签集合与 k。核心可行条件是 containment：查询标签集合包含于对象标签集合。输出为可行集合内近似 top-k 对象；不是全局 top-k pairs 或 threshold join。

为查询标签集合选择性物化子索引，使用集合包含关系选择可覆盖查询的索引；elastic factor 为可行对象数量与所选覆盖索引对象数量之比。索引选择不依赖特定 ANN 库，实验子索引使用 HNSW。核心有两种目标：给定 elastic-factor 门槛最小化空间，或给定空间预算提高最低 elastic factor；使用贪心与门槛搜索。

## Overlap 扩展与证据

v2 附录 A.2（PDF p.16）明确：overlap 条件为对象和查询标签集合交集非空，将查询拆成与查询标签数相同的单标签 containment 子查询，再合并结果。A.3 Exp-7、Fig.14（PDF p.16）在 SIFT 合成 Zipf 标签及 PAPER 真标签上测试。作者报告 ELI 与 ACORN-γ 接近、相对 ACORN/UNG 为 2–10 倍；仅适用于该 overlap 实验，不把全文其他 containment 加速比搬到 overlap。

作者明确 future work：当前索引选择优化工作负载不纳入 overlap，拟扩展 greedy method 支持 overlap 及更复杂条件。该表述不能被扩张为整个领域未优化 overlap，LSSG 等已有直接工作。

本项目推导：拆分子查询的局部结果必须按 ID 去重与统一距离顺序合并。若各路为精确、不同对象的局部 top-k，则合并足以覆盖全局 top-k；重叠本身不意味着不足 k，需要区分 ANN 漏报、局部候选截断、记录重复与实际去重开销。

## 理论与限制

v2 §3.1 Lemma 3.2、PDF p.5；附录 A.1、p.15：期望成本 O(C+k/c)，C 为初始邻居搜索成本。原文使用后续邻居摊销常数枚举、标签与向量位置独立以及匹配对象均匀分布等假设。全局 elastic factor 本身不能保证距离排序中每段候选也具有相同匹配概率；该界不是任意图或相关标签分布的最坏情况保证，也不是 Recall@k 证书。

§3.3 Theorem 3.4 给 EIS 决策问题 NP-complete；不能将其当作本项目结构化 overlap 子类的难度证明。§4 贪心不是最优索引集合保证。原文空间成本主要以索引成员数近似，并将已有 top index 成本置零（§3.2），本项目需另报含全局索引的实际总字节。

## 实验与项目启发

§5 Table 4：SIFT、MSMARCO、LAION、OpenAI-1536、PAPER、TripClick、Arxiv、DEEP 100M；包含真实、元数据生成与合成标签。基线含 UNG、ACORN-1/γ、brute force。记录 QPS–Recall@10/20、indexing time/size、elastic factor、更新等；未独立复现。原文 L2 示例与实际数据预处理须逐数据对齐。

迁移点：将标签过滤和索引物化选择迁移到 join/层级搜索组件，而非将 ANN 当作 join。针对当前问题，ELI-flat overlap 是强基线，祖先展开 singleton ELI 也是必须考虑的强归约基线。不能仅与逐叶子固定 k 搜索比较。候选区别应体现保留紧凑谓词、分支间不同对象搜索共享及实际内存—延迟收益。

待补查：正式稿与 v2 的完整逐项差异、官方代码实际 overlap 去重接口、硬件与所有数据预处理、独立复现和边界查询处理。官方入口由论文首页给出：https://github.com/mingyu-hkustgz/LabelANN ，本轮未运行。
