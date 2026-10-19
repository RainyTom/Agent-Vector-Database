# Directory-Aware Query and Maintenance in Vector Databases

核查日期：2026-10-19；版本 arXiv:2606.16903v1，2026-06-15，15 页。Mengzhao Wang、Zheng Gong、Jingpei Hu、Jiajie Fu、Maojia Sheng、Junwen Chen、Yifan Zhu。正式 venue/出版 DOI 未核实。B 类，可迁移借鉴；单查询 filtered ANN，不是 vector join。

## 问题与方法

DSQ 将目录条件转为向量排序的合法 entry-ID 范围；recursive DSQ 包含目录本地与后代对象，固定单路径命名空间时与本项目前缀范围一致。还研究 non-recursive、派生排除与 DSM MOVE/MERGE。

PE-Online 存直接目录posting，在线枚举后代并集合；PE-Offline 物化祖先posting；TrieHI 保留 segment Trie、parent/children 和逐节点 inclusive-ID 集合，以稳定节点身份支持路径查询与结构重连。scope 层独立于 ANN，不修改 PG/IVF 布局或排序，成员实现 Roaring bitmaps。

## 保证与复杂度边界

Inc(v)=Local(v)∪子节点Inc并集是精确 scope 不变量，非 ANN 召回保证。Table II 的 O(t)、O(t+r) 是节点访问/集合更新次数，实际bitmap与catalog成本不能忽略。TrieHI 仍沿祖先复制成员；非递归查询做子集合并差；MERGE冲突规模最坏接近源子树。一般逐查询近邻质量定理未提供，原文主要是结构/操作分析与实验；本文未独立复现。

## 实验与局限

§V Viking C++、PG/IVF、48核Xeon Silver4310、125GB RAM。WIKI-Dir 1.94M/1024维bge-m3，类别DAG取每对象一规范路径，363467目录/均深11.95/456查询；ARXIV-Dir 2.76M/1024维mxb-ai-embed-large-v1，subject/temporal两独立命名空间，1000查询。具体距离/归一化/执行器参数待核，不猜测。

指标nDCG/Recall、端到端/directory-only延迟、MOVE/MERGE、构建/大小；比较PE-Online/Offline/TrieHI及无目录向量基线。Table IV不支持TrieHI所有查询均更快；Table V的TrieHI空间最大。共同catalog在目录模块/DSM比较中排除，应另报真实系统总账。OpenViking QA是系统结果，不是新ANN保证。实验原版本与当前仓库对应、数据下载/许可和零/不足k待核。

补核§V-A：固定Viking PG/IVF用于隔离目录表示开销，不代表完整覆盖先进filtered ANN后端；§VI/参考文献列ACORN、Filtered-DiskANN、UNG，不能说作者不知道。原文未逐一说明不实验这些方案的理由，也未证明TrieHI不能或无需与它们组合。结论限已测目录方案/后端，不外推为端到端领先所有过滤检索方法。

## 项目启发与证据

“前缀过滤+Trie+动态目录维护”已有直接前序。F1–F8需收缩到实际ANN或表示增量；F9候选为前缀合法深度参与候选/入口/边导航，相同scope和总内存下与既有过滤ANN比较，新颖性未充分排查。

原文定位：§II PDF pp.2–3，§III pp.4–6，§IV pp.6–8，§V pp.8–12，§VII pp.12–13。作者future work为更丰富path predicates及混合语义/层级条件的cost-aware planning，与本项目F9分开。见[PDF](paper.pdf)、[全文](paper.md)、[逐篇分析](../../../researches/containment-filtered-vector-search/walkthroughs/09-directory-aware.md)。
