# 联合证据第2轮：事前信息诊断协议

冻结于首次运行joint_information_probe.py之前。接续7756064，不覆盖首轮结果；本轮不是在线ANN原型，也不把经典联合分块改名为新结构。

## 问题与至多两族

1. 联合子空间/tuple：将独立坐标的不同见证收紧到同组同对象；对照VA/PQ/range nearest-neighbor。
2. 关系/径向信息：投影联合距离再加正交余空间范数差；对照metric pivots/Hilbert exclusion。这里只测最简单的原点径向lift，不实施pivot index。

两族先由原文核查；没有独特结构就不进入在线C++。Python只作离线oracle信息归因，不能以其时间与C++基线比较。

## 必须先排除的混淆

原首轮“raw完整块oracle强，而64维独立界弱”不能独自证明丢失的是联合信息：可能是降维损失。即使联合界强，也可能在不过滤B上已经强，不能计为F新增的信息。故必须同时固定B/q/F/tau，交叉比较r、同对象组大小g和B vs B∩F。

## 实数定义与反例

固定正交U，r个轴与嵌套分组partition P_g。S=B∩F非空时：

L_{r,g}(q,S)=Σ_{G∈P_g} min_{x∈S} ||U_G(q−x)||²。

对g1、2、4…r嵌套合并，L单调不减且不超过真实最小距离；g1是gap，g=r是精确投影空间最小值，仍不是原空间精确值。合并更强因为min(a+b)≥min(a)+min(b)。同组保留witness，不同组仍松弛。S={(0,1),(1,0)},q=0给g1=0、g2=1；旋转/分组会改变中间g结果，不能只挑最优分组。

径向lift：将剩余d−r坐标压成范数ρ(x)，T_r=min_{x∈S}[||U_r(q−x)||²+(ρ(x)−ρ(q))²]。反三角不等式给安全实数下界。r=0为径向证书，r=d退化完整变换距离。整个min必须同一对象；该lift为经典metric contraction，不认领新意。共同参考中心固定为全体样本均值，不随F复制。

## 固定数据与变量

完全复用run2数据、query、seed、kmeans(K32/96)、scope和合成属性。输入SHA与run2 provenance逐项核对；重复seed的分块/属性候选数对照旧queries.csv，不能静默改变场景。

- WIKI11869×1024，SIFT12000×128；各前8条旧query；k10。
- basis=variance-coordinate/PCA；r={16,64,256,d}去掉超d和重复；径向另有r0。
- g为1到r全部2次幂；两分组顺序为contiguous与seed固定permuted，g1/gr作为重复sanity，不双计主结论。
- scope root/broad/narrow；scope_only/eq7/eq7_and_hi6/eq7_or_hi6；相关/tuple打乱控制保持旧条件基数。
- 主阈值相同F的raw FP32精确第10距离。另保留共同scope半径控制，但不能作为filtered top-k在线tau。
- raw距离复用run2 C++矩阵，核对矩阵/输入SHA、形状及样本0/25 smoke来源；不声称本轮新做了190952次原距离调用。

## 记录与判别

每case记录eligible/空块数、raw-oracle排除、wholeB联合界、conditional联合界、相对g1新增排除、相对wholeB的F新增排除、guard违规、oracle取得的feature/reduction工作。空块不计几何增益。

1. 若r64、g64仍弱，而更大r明显恢复，主要瓶颈至少包含投影信息丢失，不能只责怪独立轴。
2. 若r64、g小便强，存在低阶联合信息；但还需扣除不过滤B的同组界，且取得方式/前序仍必须通过门。
3. 若径向lift显著接近raw oracle，有明确低维统计机会，但范数lift本身经典，不能宣布新表示。
4. 若有效证据需要接近原d同对象计算，查询时计算该oracle等价/接近原filtered NN，不能把它假定为便宜索引原语。

报告全部r/g/order/basis/control，不能只选最佳点。g1在相同r/basis/F应复现首轮gap；PCA/FP32沿用旧guard并逐块检验，观测安全不等于通用浮点证明。发现违规或配置不一致应保留失败，不调阈值掩盖。

## 门禁与完整成本

本轮即使信息阳性也不自动实施索引。在线候选必须指出相对VA/PQ/range/metric前序新增了什么操作或覆盖；其投影、编码/存储、F构造、cell/posting访问、补扫与更新/版本维护须计入。属性更新不改变向量不等于整个维护O(1)。不改HNSW frontier，不运行ANN大矩阵，不用大scope exact scan作方案。
