# 联合证据：结构、性质、前序和机制门

接续7756064，第2轮；不是新索引命名或实施承诺。事前[信息诊断协议](joint-information-protocol.md)，实测及失败见[joint-certificate-report.md](joint-certificate-report.md)。

## 1. 本轮纠正的关键推理

“原空间最小距离oracle强、64维独立轴界弱”同时混入了**投影丢失**和**不同对象见证松弛**，不能直接归因为后者。即使同对象联合界很强，也必须与**不过滤的同种联合界**比较；旧whole-box不是足够的归因对照。

这不是提高实现参数的实验：r/g/F改变的是证书保留的几何信息和对象绑定，不报告微基准胜负。

## 2. 两族草案；两者均未取得独立新颖性

### A. 分组同对象tuple/区域证书

定义S=B∩F，U为正交变换；r个轴分成互不相交、嵌套合并的组G。

\[
 L_{r,g}(q,S)=\sum_G\min_{x\in S}\|U_G(q-x)\|_2^2.
\]

- g1是独立gap；g=r是**精确投影最小距离**，不是原空间距离。
- 每组的min来自同一个对象，不同组仍可取不同对象。
- min(a+b)≥min(a)+min(b)，故固定r按嵌套方式合并组只会加强；不超过min原始距离。
- S={(0,1),(1,0)},q=0：g1=0，g2=1。该反例本轮数值执行。
- 分组顺序也影响中间g，故实测保留contiguous/permuted，不选择最佳顺序作主结果。

**可能的共享组织（经典对照，而非新设计）：**每个ITEM存共同ID、block、各子空间cell code/tuple及raw向量引用；每个cell/tuple存posting与包围区域。先构造同快照F，再测试F∩block∩cell IDs非空，按区域下界展开，已取得k个合法结果时用tau停止；必要时进入局部HNSW或验证raw距离。若候选生成截断而无剩余覆盖，则是近似，不能称安全完备。

独立cell非空不证明同一对象存在；完整tuple posting或共同ID交集才保留跨组绑定。未观察过的code tuple不是“可忽略的对象”，但空tuple可在准确成员结构下排除。

**直接前序：**

| 前序 | 实际核读版本/定位 | 与草案重合 | 不可偷换的边界 |
|---|---|---|---|
| Weber/Schek/Blott, VLDB1998 VA-file | 原文§4.1 pp202–203的mirror转录；[正式目录](https://sigmod.org/publications/discs/1999.1/disc/vp_a_quantitative_analy.htm)、[转录](https://studyres.com/doc/8797762/a-quantitative-analysis-and-performance-study-for-similarity) | 每对象多坐标cell code，矩形上下界，按界精炼，插删时固定边界 | 全部code扫描仍在；转录公式有损，原PDF仍未归档 |
| Ferhatosmanoglu/Tuncel/Agrawal/El Abbadi, VA+ | [UCSB2000-10](https://cs.ucsb.edu/research/tech-reports/2000-10)的[原始PS](https://cs.ucsb.edu/sites/default/files/documents/2000-10.ps)，§3.1–3.4，报告p5–10 | KLT/PCA去相关、不均匀bit分配、优化标量区间 | 17页技术报告不等于CIKM8页版；不是任意联合点集精确表示 |
| Babenko/Lempitsky, IMI | [TPAMI2015](https://doi.org/10.1109/TPAMI.2014.2361319)，§2–4，PDF3–6；本地全文 | 子空间codebooks、tuple posting、空cell、best-first product枚举 | centroid排序和T截断不是raw NN安全证书；journal明确扩展CVPR2012，不强合并别名 |
| Ferhatosmanoglu/Stanoi/Agrawal/El Abbadi, CNN | [SSTD2001](https://doi.org/10.1007/3-540-47724-1_14)，§4.2 pp264–268、§5 | 在约束与MBR交集上计算更强mindist，过滤后重新处理witness上界 | 多边形几何约束不是任意属性Boolean bitmap；optimality仅其树/page模型 |

因此，“联合code＋posting＋F＋安全refine”不够成为新机制。需要能指出减少了哪一类**普通tuple枚举或eligible扫描**，而不是只与独立box比较。

### B. 关系/径向余量证书

以固定中心c及正交U表示：z(x)=U_r(x−c)，ρ(x)=||U_tail(x−c)||。每ITEM存(z,ρ,ID)，共同pivot/中心/基变换不按F复制。

\[
 T_r(q,S)=\min_{x\in S}\bigl(\|z(q)-z(x)\|^2+(\rho(q)-\rho(x))^2\bigr).
\]

反三角不等式给T_r≤min原L2²。r0为径向，r=d退化完整距离；整个min要求同一对象。

**这正落在既有simplex/Hilbert contraction框架内。** Connor/Vadicamo/Rabitti的[2017 SISAP正式条目](https://doi.org/10.1007/978-3-319-68474-1_7)及[已读arXiv v1](https://arxiv.org/pdf/1707.08370v1)§4.2给apex投影坐标＋残余高度的同式下界，§6已将surrogate表扫描或重新建树；§5还讨论PCA引导。换成原始坐标的PCA子空间，不产生独立表示新意。

**不能修复的解析反例：**所有pivot位于H，q=z+u、x=z−u、u⊥H，则pivot距离和上述tuple完全相同，surrogate距离0而原距离2||u||。本轮smoke验证了径向界的收缩与full维退化；此反射构造为解析证据，不伪称已跑真实pivot实验。保留ID不等于保留余空间方向。

附近前序还有[Hetland Ptolemaic indexing](https://doi.org/10.20382/jocg.v6i1a7)的两pivot乘积界、[Hilbert exclusion](https://doi.org/10.1145/3001583)的bisector界与[Omni](https://doi.org/10.1109/ICDE.2001.914877)的pivot-distance索引。Omni本轮只有摘要/书目核验，正文提取失败，不补猜章节；不把这些全部归档为已精读全文。

## 3. 真正未支付的联合操作

对任一合法L(q,x)，需要的是：

\[
 \operatorname{Empty}\{x\in B\cap F:L(q,x)\le\tau\}.
\]

或者条件最小值。若直接枚举B∩F取min，就已经做了待避免的工作；若把它交给普通低维NN/tree/tuple posting，它有已有前序和维数退化。任意Boolean F并不自动变成共同几何顺序中的一个区间。

不为每F建图/摘要只是约束，不是解决上述操作。过滤后可收紧的几何界在2001 CNN已经出现，不能把这一抽象本身写成首次。动态任意F/层级合同较宽，但新需求也不自动等于新结构。

## 4. 成本：所有常见项都要保留

用N为ITEM数，D为原维数（不与原任务平均层级深度d混淆），M为目录节点数，d为平均层级深度，f为目标后代节点数，C为最终eligible数，U为单位时间更新数，k为结果数，h为几何块/cell/tuple访问量。

### A族

- 存储：向量Θ(ND)、目录Θ(M+N)，各组code Θ(Nr/g)，posting总长度每组N，共Θ(Nr/g)，若存完整tuple且只建一套posting可Θ(N)但不能免费做所有投影group访问；完整稠密cell目录可能指数增长，稀疏目录需付实际占用tuple数。区域摘要另需其边界维数，不能只数code bytes。
- 属性/层级解析T_F取决于posting/bitmap和范围表示；在线展开可能遍历f，祖先物化可能每ITEM含d条成员；不能统称PE-OFFLINE每次更新O(t)。查询前处理T_U(q)通常稠密投影Θ(rD)，或选轴Θ(r)。
- 查询：T_F+T_U(q)+cell/tuple枚举与h次非空测试+访问局部ANN/验证+合并。若s个局部结果流各给k，普通merge至少处理这些返回结果；预算并非免费。
- **当前信息oracle**：对同一query计算全部D维变换差和子组和，然后在每组对C个对象取min；各组维和为r，坐标工作Θ(Cr)，min输入数C(r/g)。不是次线性取得证书。
- 标量/标签改动：更新相应posting/版本；几何code不变但缓存F/certificate须维护。单ITEM跨目录涉及源/目标成员与可能局部HNSW增删；子树重挂需算d/f/C及物化祖先结构变化，不写成整个O(1)。
- 新向量需投影/量化、code/posting、目标图插入；删除需当前live集合/tombstone/压实；codebook重训可能全体重编码。单位时间维护成本为U乘各更新类型实际成本加后台维护，不是U×常数。

### B族

- 表为Θ(N(r+1))几何值，另有共享基、中心/pivot参数及F。使用m个pivot需每新对象Θ(mD)距离及apex构造；不能把任意实现都报线性apex成本。
- 查询需query编码、tuple访问/树遍历、F过滤/非空、必要raw核验；若扫描表，最坏Θ(C(r+1))或Θ(N(r+1))，没有普遍次线性保证。
- 属性改动不改tuple；同样要维护posting、目录和缓存版本。pivot/子空间更换可要求全表变换；插入需要扩大安全区域后才能可见。删除后保留superset可安全但松，漏掉新对象的subset不安全。

### 并发和近似性合同

两族若实施都须query读同一已发布版本的live-ID/F/几何覆盖，提交前完成插入覆盖与属性posting；缓存不能跨版本沿用过紧旧界。可用MVCC或读写epoch，但本轮**未实现/测量**该协议，也不作为贡献。

安全结果界只可跳过结果贡献，不能直接删除HNSW navigation frontier；桥点仍可能需要访问。全局预算截断/局部ANN本身会有Recall风险，须同状态同F GT确认。以上证明为实数；本轮FP64 guard只证明样本未违反共同FP32内核，不是普遍认证。

## 5. 失败判据与决定

能排除的候选/tuple必须在**同种不过滤几何界**不能排除的集合中核算；对g1的提升只是联合几何收益，不全属F。r64联合oracle若仍为0，该具体正交距离表示在相同阈值下已到头，不能继续在其普通cell近似上期望更强下界。

目前两族的共享组织都属于已有方法或直接组合，本轮没有满足“新增操作/覆盖且避免其取得工作”的独立结构。即使SIFT联合oracle强，也不因此过门；不接入HNSW、不跑新ANN矩阵、不把维护路线复活。对其他编码、数据/阈值/更大维数不作不可能性结论。

## 6. 不能用局部ANN的近似min冒充联合下界

若某组真正min是1，但ANN漏掉该点而返回100，它给的是组min的上界，不是下界；当tau=10时直接代入L会误剪。这个解析反例不需要改变硬谓词就成立。故“每组建HNSW快速找min”不能实现这里声称安全的证书，除非同时证明未访问区域的下界；预算截断则必须承认近似性并测Recall。HNSW可提供合法结果来形成tau，不自动认证未访问对象的最近距离。原任务允许ANN，本轮安全信息诊断是其中一条路线，不把其失败推广为所有近似表示无效。
