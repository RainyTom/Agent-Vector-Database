# 接收5fc1be3：少边没有少展开，先纠正查询预算再分解原因

## 1. 决定

接受服务器按7654f1e原样执行的static负结果，不要求重复证明：WIKI确认质量不足；SIFT本样本同质量速度差，另有独立qidx覆盖限制。无已观测状态错误；Dynamic与维护包络未测，不记0。

用户随后指出正常实验优先采用k附近小查询预算。研究端接受纠偏：**不再以大ef/.999选参作为唯一评价，不扩大N、不继续升预算；交付A/U/R/H在ef15/20的固定重跑。** 上轮结果不撤销，新实验也不是事后改门宣布通过。完整可执行提示词见[小ef服务器任务](closure-small-ef-server-prompt.md)。

本轮新增的是离线聚合推导、源码性质小探针和小预算调度；没有修改原NodeANN/closure核心或替服务器重跑真实数据。

## 2. 哪些事实可采信，哪些不能扩大

依据[完整固定报告](https://github.com/RainyTom/Agent-Vector-Database/blob/5fc1be3/researches/nested-attr-vector-search/2026-10-30-exps/closure-server-report.md)、其group-runs/matched-ef/failure-diagnostics和本地原实现：

- H的编译邻接确有差异，实际search调用同一compile，不是离线另画图。
- 同ef1024校准质量均通过时，H/R平均expanded几乎相同；ef64的小些预算也已有该现象，不能只归因“1024太大”。
- “展开数量相同”不证明展开对象集合、顺序或首次发现边相同。现有CSV没有这些轨迹，不追加此类强因果。
- WIKI漏召回和SIFT样本内效率失败不同；SIFT16个非空确认qidx不足覆盖是另一项限制。
- 新臂calibration通过而confirmation未过，不是语义实现错误。U/R也漏，不能把WIKI唯一归因H闭包。
- 880次recursive采样核对通过仅支持所查邻接性质；direct不属于定理，连通/限制一致也不保证有限ef召回或快搜索。

## 3. 一个由源码确定的遍历下限

[search原代码L86–103](https://github.com/RainyTom/Agent-Vector-Database/blob/7654f1e/codes/directory-aware-vector-attr-search/src/closure_index.h#L86-L103)中，results只放普通属性合格对象；`results.size()<ef`时每个新对象都可进入frontier，距离早停只在`results.size()==ef`时启用。

若某query完整eligible数E小于ef，则results永远填不满。次序骨架覆盖当前scope的所有live对象，因此沿骨架最终全部可达；seen保证每对象只计算一次距离。于是固定该版本、有限合法距离与正确scope状态时：

**E<ef ⇒ expanded=distance_calls=scope_card。**

空GT且非空scope也满足；如果scope本身为空，三者都是0。这是当前规则的设计后果，不是服务器实现偏离规格。邻接再稀疏，只要保留这条连通骨架，便不能改变这一类query的expanded总数。

本地已运行原Index的[48对象C++探针](experiments/closure_budget_probe.cpp)：E4、ef8、k3、池8，U/G/R/H/H0分别保留384/343/368/350/190条编译边，全部expanded=distance=48且结果与原维精确GT一致；E0也全部展开48。见[实际输出](data/closure-budget-probe.json)。只是新性质的有限执行，不是重跑已报告服务器失败，也不证明真实query中该类所占比例。

不能将该推导推广成全部归因：WIKI唯一H确认漏召回query的E为8511，大于ef1024；它不属于此分支。需要逐query CSV对E<ef和E≥ef分层，不能从均值猜占比。

`analyze_closure_termination.py`已实现原CSV只读统计，校验原SHA，并保留空/非空、强制遍历比例及H/R分组。原raw未在本机，所以本轮未填造真实分层数字。

## 4. 编译成本并不只是LCA：U也昂贵

从服务器group-runs按三次确认各自非空均值重算：

| 数据 | U编译占e2e | H编译占e2e | H每展开源读取的池候选 |
|---|---:|---:|---:|
| WIKI | 80.26% | 85.36% | 64 |
| SIFT | 88.15% | 90.89% | 64 |

这些是原运行的嵌套计时占比，不是CPU硬件profile或可直接减掉的成本预测。WIKI H质量未过，不能据此报告其同质量加速比。

源码compile在所有模式都为每个live池候选调用动态treap rank，U虽不做几何见证也要执行scope编译；H再按候选爬当前parent、做enter/exit rank及闭包mask。当前实现的代价至少与展开源数×64次候选检查相关，动态rank的树高代价也在其中。局部mask复用并没有把每个候选的当前LCA查找消掉。

**更重要的是，边被剪掉前已经支付了该候选的元数据检查。** 只有真正避免以后展开或距离/队列工作，才可能补偿这些费用；“保留边少”本身不是已省去这些编译工作。

H0比H更少边但更慢，也不能简单解释为属性检查慢：H0允许更多blockers进入hierarchy分支，原计数中hierarchy_steps更高，虽少了Extra资格检查，LCA/rank工作反而更多。哪些调用占多少真实CPU仍待profile，不能用该计数当完整因果证明。

即使仅做账面`e2e−compile`，SIFT H仍约3.863ms，而A约0.355ms；这**不是“删掉编译后的新算法性能预测”**，只说明现有数据不支持把所有差距归给LCA。禁止用扣除计时宣称已赢基线。

## 5. 几何见证没有证明减少查询区域

当前见证判断比较的是d(x,w)、d(w,y)、d(x,y)，不是d(q,y)与当前top-k阈值。它约束候选邻接保留，不给出可以排除一批query近邻的距离下界，也不保证w的池里实际存在w→y边。

同一个对象可能由其他候选或骨架到达；删掉一条边不等于删掉一个搜索状态。反过来，删捷径可能需要更多步才能到达同一好邻域。是否被骨架主导、被其他邻接重复发现、或始终在同一距离阈值内，现有计数不能完全分开。

WIKI最近GT对象8122未返回，若固定GT/tie/资格及共同距离核合同成立，则它没有被offer并进入距离求值：一旦这个合法全局最近对象被offer，必能进入合法结果堆，且不会被最终top-k挤掉。这把问题定位到候选访问/路径/停止之前，而不是最后序列化只返回九条或rank排序错误。记录实际是返回10条、其中9条GT命中。

根scope仍有次序骨架，不代表有限ef一定走到8122；offer接受阈值或frontier早停可以阻断通向更好对象所需的中间访问。未有完整轨迹时不唯一归因“池没有8122”或“某一H见证剪错”。

## 6. 查询预算纠偏：不是把efConstruction调小

当前建图M16、efConstruction200，取64候选的构建期HNSW查询ef200；这三项维持原样。用户本轮说的小预算对应在线搜索：k10、efSearch15/20。

- hnswlib中`ef`/`setEf()`就是查询efSearch；不能当作两个独立旋钮。
- H的ef作为整个scope的合法堆宽度；NodeANN在多个direct-owner局部图各自执行预算并合并，数字相同不代表总访问工作相同。因此先报同预算曲线，再看质量，而不是将same ef当公平性的充分条件。
- 小ef可能显著减少遍历；仍可能在E<ef的query全scope展开，也可能降低Recall。两者按实际CSV报告，不提前宣布修复。
- 原ef64下H/R已几乎相同的证据保留；15/20未测，不能外推已经失败或已经胜出。
- 新脚本保留全部小预算质量点，不使用`.999`选择参数，也不默认换成`.95`门来宣布通过。任何同预算耗时比都附实际macro/pair。

## 7. 本轮本地执行与最小后续

实际完成：固定聚合读取/比例重算；原C++48对象性质探针；离线CSV工具真实CLI、SHA/覆盖边界检查；新小预算驱动用真实NodeANN和closure在构造输入上完成24次A/U/R/H×15/20×3执行。详见[离线工具证据](data/closure-5fc1be3-local-check.json)、[小预算流水线证据](data/closure-small-ef-local-check.json)、[聚合派生数据](data/closure-5fc1be3-attribution.json)。构造输入不提供新真实数据效率结论。

后续仅两件事：先读旧CSV分层解释已有结果；再按[明确提示词](closure-small-ef-server-prompt.md)重跑两个小预算点。暂不改核心、解耦导航堆/合法堆、扩池/加入口、重启二跳/WQR或跑真实Dynamic。改停止语义是新算法实验，不是免费“修bug”，需另有假设与同质量控制；正常小ef评测本身也不是新论文贡献。
