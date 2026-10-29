# 条件几何证书：机制门、原文差异与保留的科学问题

## 结论先行

当前两草案都**没有通过独立新颖性门**。A“共享投影排序＋F上的条件min/max/gap”是BSI/wavelet/range-query能力的直接应用；B“稀疏代理＋残差覆盖”未给出超越L2AP/Seismic的新覆盖机制。保留A作信息价值对照，暂停B独立实现。不是否证整个条件几何证书领域，也不回退维护优化。

本轮已实际做WIKI/SIFT静态信息诊断，见 [报告](report.md) 与 [事前协议](conditional-geometry-protocol.md)。下面的论文作者性能没有复现；几何证明按实数算术，当前FP64/FP32数值guard仅有样本验证，不宣称任意输入浮点认证。

## 1. 核心原文与最近前序

| 工作 | 实际阅读版本/定位 | 已有能力 | 当前不能据此认领的新意 |
|---|---|---|---|
| Denis Rinfret, Patrick O'Neil, Elizabeth O'Neil, **Bit-sliced index arithmetic** | SIGMOD2001,47–57；[DOI](https://doi.org/10.1145/375663.375669)、[作者全文](https://www.cs.umb.edu/~poneil/SIGBSTMH.pdf)；§2、§4 Algorithm4.1，PDF2–4/6–7 | 共同ID位空间的标量位切片、AND/COUNT、逐位Top-k选择 | 共享几何标量＋候选F求extrema不是新原语；Algorithm3.3逐行MIN不能误当集合aggregate min |
| Denis Rinfret, **Term Matching and Bit-Sliced Index Arithmetic** | UMass Boston博士论文，June2002；[全文](https://www.cs.umb.edu/~poneil/DenisRinfretThesis.pdf)，§6.1，PDF71/印刷61 | 明确允许Boolean条件结果替换potential answers | 不是只支持全集/单标签；不按每F复制摘要已有直接表达 |
| Travis Gagie, Gonzalo Navarro, Simon J. Puglisi, **New Algorithms on Wavelet Trees and Applications to Information Retrieval** | [arXiv1011.4532v1](https://arxiv.org/pdf/1011.4532v1)，§2/4.1–4.3，PDF4–6/9–13；正式TCS426–427(2012)25–41，[DOI](https://doi.org/10.1016/j.tcs.2011.12.002)书目核实，版本差异待核 | range quantile、next-value/occurrence、range count、多区间交集及alternation证书 | 目录区间上的min/max/successor和gap为空的证明属于直接实例；任意临时F bitmap不自动是一个区间 |
| Gonzalo Navarro, Yakov Nekrich, **Optimal Dynamic Sequence Representations** | [arXiv1206.6982v2](https://arxiv.org/pdf/1206.6982v2)，§2–3/6–7；对应SODA2013书目[DOI](https://doi.org/10.1137/1.9781611973105.62)，865–876；同题SIAM2014[条目](https://doi.org/10.1137/130908245)另核、未混用其版本 | access/rank/select查询最坏、符号插删摊还O(log n/log log n)，压缩空间 | 不是任意F、多投影、subtree cut/paste/MERGE或ANN整个操作的同复杂度 |
| Yicheng Jin et al., **Curator** | 已有本地v3全文；[v3](https://arxiv.org/pdf/2601.01291v3)，§4.1–4.2/5；正式[DOI](https://doi.org/10.1145/3786635) | 共享几何树、外部qualified IDs/bitmap、临时谓词视图、预计访问区域的延迟构造、增量更新 | “共享树＋bitmap＋lazy谓词子树”不新；它的启发式centroid−alpha×平均半径并非条件极值L2安全证书 |
| David C. Anastasiu, George Karypis, **L2AP** | ICDE2014,784–795，[DOI](https://doi.org/10.1109/ICDE.2014.6816700)、[原文](https://davidanastasiu.net/pdf/papers/2014-AnastasiuK-ICDE-l2ap.pdf)，§II/IV/V、Algorithms3–4 | 部分倒排覆盖、prefix L2残差界、候选引入和验证停止 | 部分坐标索引＋范数余量＋覆盖不是新意；单位cosine阈值self-join不能直接当raw L2 filtered top-k |
| Sebastian Bruch, Franco Maria Nardini, Cosimo Rulli, Rossano Venturini, **Seismic** | [arXiv2404.18812v1](https://arxiv.org/pdf/2404.18812v1)，§3/5/6；SIGIR2024，[DOI](https://doi.org/10.1145/3626772.3657769) | 几何posting组、max块摘要、压缩/query-cut/heap_factor近似搜索 | 整块摘要/WAND非空白；实用摘要压缩和posting截断不能称安全；signed query仅max不够 |

前四篇归自身问题到 `papers/data-indexing/`；L2AP是直接threshold self-join，归 `papers/vector-join/`；Seismic是单查询稀疏检索，归 `papers/sparse-vector-search/`，不误称join。六篇均保存reader全文及日期化文本SHA；非原PDF二进制，不把转换完整性当逐公式正确性保证。新增关系均为researcher_synthesis，不捏造引用/继承。

### 背景与仍有缺口

Agarwal/Erickson的 [range-searching综述](https://users.cs.duke.edu/~pankaj/publications/surveys/holyoke-survey.pdf)将count/report/emptiness/optimization统一，说明范围内投影极值本身经典；本轮未补齐原始range-tree与动态版本闭包，综述未新增正式总库记录。任意标签Boolean表达式也不等于固定低维rectangle。

VA-file1998原文扫描页仍未成功核读。2008作者回顾不足以替代原文；不填造章节或成本。范围/bitmap/压缩数据结构查重不声称全面完成。

## 2. A草案的结构、查询和更新合同

### 存储

固定几何块B，r维投影U；每块每维保存按投影排序的值与共同对象ID。属性与层级归属在独立结构，查询首先获得当前快照F。没有每个F或祖先复制几何摘要。

当前64维诊断的排序数组为12rN字节（FP64值+int32 ID），另计U/选择的轴、block map、属性成员和工作区；这不是BSI/wavelet压缩实现，也不是最优空间界。任意F映射到多个顺序的成本没有消失。

### 查询：两种条件信息，同一候选族

设S=B∩F。先在共同ID上组合所有谓词。

1. min/max：在每个排序从两端找首个eligible，已检查区间无eligible，得到条件box。
2. gap：在每个排序中binary-search q的位置，再两侧找首个eligible；完备前驱/后继给近q空区间和δ_j。
3. 若S为空可资格排除，但不计作新增几何剪枝。若已有k个合法结果且LB>tau，排除块的**结果贡献**。本轮tau来自离线GT，不是在线结果堆。

直接端点/双向membership探测最坏可达O(rb)，不是因为没枚举全部eligible就变次线性。一个eligible对象如果在各排序中都靠中间，取得其边界可能触碰几乎所有不合格对象。BSI/压缩bitmap/wavelet可能改变取得成本，但它们已有，并未在本轮实现或测速。

### 更新

属性变动不改几何值，但需维护属性结构/版本F；新分类在可见前必须进入正确成员集合。仅删除后保留superset摘要可以安全但更松；插入不能沿用过紧旧界。若缓存条件界，须失效/扩展后才可见，本轮没有该缓存协议。

新向量需投影、分块并维护r个排序；当前静态数组插入/块分裂不能写成O(logN)。子树R只说明向量几何不变，不能忽略范围表示和成员可见性。没有动态查询更新实测，也不声称O(1)维护。

## 3. 性质与直接反例

对所有x∈S，δ_j≤|(Ux)_j−(Uq)_j|，故sum δ_j²≤||U(x−q)||²≤||x−q||²。各维witness不同**不破坏下界**，只造成松弛；S={(0,1),(1,0)},q=0时逐维gap0而真实最近平方距离1，该新诊断smoke已执行。

若q在条件min/max外，最近eligible边界与box距离相同；若q在内部、但没有eligible靠近q，gap可以更强。一个近witness只给上界，必须证明中间没有遗漏eligible。非正交投影不得直接平方相加；诊断记录PCA Gram上界并收缩。

同对象反例：一对象red、另一对象large，两个边际非空不证明red AND large非空。若用共享ID bitmap AND则语义正确；若只以bin ID交集，则可能串错对象。

图导航边界：结果LB不能直接用于删除HNSW frontier节点。即使点不可能成为结果，也可能是通往近邻的桥；本轮完全不改HNSW邻居队列。

## 4. B草案为何暂停

BioVSS++的变化是BioHash/WTA→集合count/OR→激活倒排union→sketch筛选→Hausdorff精排；它不是当前single-vector raw L2，Q={q}时其集合距离取最远成员而非最近成员。基础BioVSS仍扫所有集合。相关定义/算法见本地PDF页7–29。

L2AP已经把“部分索引必须命中每个真阈值pair”与prefix残差界结合；Seismic已有posting几何分组与摘要。新的signed/different-norm/动态F要求是合同差异，不自动变成研究贡献。

[解析构造，未新跑数值实验] q=(1+eps,1),x=(1,1+eps)，identity top1-WTA无公共激活但原距离平方2eps²；任意截断候选精排补不回。q=(1,eps),x=(eps,1)各保留不同top1支持时，码点积0+残差乘积eps²小于真实点积2eps，简单双残差公式漏交叉项。若仅提出条件残差极值接口，它属于A而不是第二项独立贡献。

## 5. BioVSS/Seismic本轮补充审读边界

- BioVSS v2 Theorem4把1−similarity写作Hausdorff数值界；单位向量实际应区分H与H²/2尺度。排序单调关系尚在，不等于raw L2界。相关式在本地paper.md1058–1094行、PDF18。
- 其count/OR丢成员绑定，强分离/碰撞模型不自动覆盖++的Top-A/Top-T截断。Algorithm6的c/T及F/F2记号、重复编码是否去重仍需核实现，不当执行规格。
- Seismic逐维max在非负query下有上界；负query需min/max符号选择。B={−2,−1},q=−1时只存max得到1但真实最大IP=2（解析反例）。修正符号不是新表示。
- 以上是审读/推导，不是作者性能复现，也未验证其全部理论基础。

## 6. 门禁决定

**不实现在线C++候选索引，不接入HNSW，不跑大规模ANN网格。** 当前只写了统一距离的离线C++矩阵oracle以及Python信息/排序探测诊断；不是借辅助程序绕过阶段门。

有科学价值的剩余问题是：如何用一种超出经典独立坐标extrema/gap的共享联合证据，弥补“真实块距离可排除很多，但这些便宜界排除不了”的鸿沟，并使取得成本低于被省工作。必须先说明相对BSI/wavelet/range emptiness新增的操作或覆盖能力；本轮没有解决该问题，不把它包装成已设计好的下一索引。
