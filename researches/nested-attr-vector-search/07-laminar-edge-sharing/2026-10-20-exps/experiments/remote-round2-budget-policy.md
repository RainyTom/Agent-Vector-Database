# 下一轮交接：验证同空间优势，改进高召回搜索预算

执行回传后的最新入口：[ee90400复核与收束](les-closeout-ee90400.md)。B1已实施；本轮不继续参数搜索或启动A1，下文保留原设计要求。

日期：2026-10-20。状态：**待远端实施**。承接 E0、S1 静态分析和 strict blocks 回传；本轮研究侧只做文件审计与离线统计，未运行新检索实验。

## 1. 本轮决策

- 撤回“LES 被严格支配”的笼统结论；保留“当前汇总表显示更小空间、中等召回工作点”的观察。**同空间优势的配对重复证据尚未完整入库**，不是已确立的跨负载结论。
- E0 保留；全量有序列表 exact interning 的 S1 关闭。唯一率0.985只否决这种表示，不证明块共享、邻居字典、少数 scope 覆盖都不可行；本轮不继续投入这些分支。
- 优先做 **B1：按作用域特征校准 ef 预算表**，用小型策略表替代当前两个固定基数阈值，目标为约53.3 MB下改善r98。同样机制给PSG-M12/M16使用，不能只调LES。
- A1 anchors 仍为条件分支。尚无逐查询导航计数证明入口不足，不直接启动联合构图或把其他论文名称当作实现规格。

## 2. 回传审计：先补这些证据

已检查 `run_strict_blocks.py`、`analyze_scope_adjacency.py`、LES/PSG查询源码及两份strict CSV。

| 项目 | 当前证据 | 下一轮要求 |
|---|---|---|
| M12 7-block | 原始CSV仅42行，6配置×7轮，没有M12；summary有M12数值 | 找回已有逐轮日志/命令；没有则重测，不能将summary行视作配对重复 |
| 约0.963点 | 只有summary一行及note | LES ef14、M12 ef40纳入同一交错block |
| 计时边界 | LES累加query时间；PSG用repeat wall time | 双方输出同轮query sum与batch wall；主表统一batch wall，分位数另用完整单查询耗时 |
| 配对比值 | 历史文字使用中位数的比值 | 同时报告每block比值的中位数；两者不能混称 |
| 环境隔离 | runner继承环境，fixed未显式移除GRAPH_ADAPTIVE | 先清理控制变量，再按配置设置；保存实际环境 |
| 文件留存 | ids/timings按config命名，被后续block覆盖 | 按run_id/block/config分别存；每轮验证recall与违规数 |
| warmup/固定CPU | runner未显式预热或绑核，外部执行设置未入库 | 记录实际绑核命令、CPU和一次不入统计的预热；不推断此前一定未绑核 |
| S1字节 | 82.99 MB未计lv、upper_off、global→local lookup等 | 标为已统计组件的小计/部署下界，不称完整部署大小；否决决定不变 |

S1中 `refs=1,034,631`，仅补回每成员1B lv和4B upper_off就再需5,173,155B，尚未算source lookup等。没有原始索引在本地重跑，不重写历史82.99数值，追加会计口径即可。

离线重算见 `../data/strict_evidence_audit_20261020.json`：LES/固定M16 r95比值的中位数为0.8423，r98为0.7208；中位数之比分别0.8306/0.6912。仍受计时边界影响，**这些不是修正后的性能结论**。

M12 ef40 recall0.9638/QPS32,707，而ef48 recall0.9717/QPS34,506；高ef反而更快不必然错误，但值得同block复核。另补ef28/32/36/40/44/48，以免r95取了不必要的高recall点。LES中等召回优势不能只靠更宽松的选点。

## 3. B1改进：把搜索预算给边际召回收益高的查询类别

### 3.1 假设与目标

当前规则只按可行集大小把ef减半/乘2/3。可行集相近的scope可能图结构与查询难度不同；统一增加ef到r98会浪费部分工作。拟用 **在calibration上测得的时间–命中数曲线** 选择各类别的ef，不改变图、不在test上判断是否漏召回。

这属于本地研究假设，不声称是BOA/PathSeer复现或已具备新颖性。若现有规则已足够，B1可能没有收益。

### 3.2 数据划分与特征

1. 保留原1000条为历史诊断集。另生成互不重叠的 calibration-fit、calibration-validation、test 查询，每组目标至少1000条；使用现有生成器和相同真实查询向量来源，记录query id、scope、seed与输入哈希，禁止同一(query vector id, scope)跨split复用。若数据不足，明确实际数量，不复制成“独立”样本。
2. 不改变scope分布来制造优势，主实验沿用既定workload分布；单列不同scope频率的压力测试。生成各split完整过滤GT。
3. 第一版只用廉价的查询前特征：scope基数桶 `0,1–9,10–499,500–9999,>=10000` 与深度。稀疏类别在fit阶段按确定规则合并，建议少于50条回退到父级类别；阈值是预先约定的工程选择，不在test调整。
4. 暂不逐scope拟合、不上学习模型，避免1365个scope与小样本导致过拟合。没有见过的类别回退到基数桶；仍未知则用validation选定的固定ef。只做单scope Subtree，antichain另留待办。

### 3.3 校准预算表

每方法独立扫 `ef={10,12,14,16,20,24,28,32,40,48,64,96,128}`，ef不低于k。旧fixed和旧adaptive都保留控制组；B1使用表中的**最终ef**，不能再次乘旧adaptive系数。

对类别b与ef e，保存fit上总命中数 `H[b,e]`、每查询平均时间 `T[b,e]`、查询权重 `p[b]`，以交错重复减轻时间噪声。每查询有固定GT分母，按协议主recall定义统一聚合。目标是：

```text
minimize sum_b p[b] * T[b, ef_b]
subject to aggregate predicted recall >= target
one ef_b from the measured grid for every category b
```

先移除同类别下时间不低且命中数不高的选项；**不假设ef增大时测得recall/时间严格单调**。初版使用离散动态规划：状态为累加整数hits，保留最小加权耗时和选择路径，做支配剪枝；小型类别表可用精确枚举检查DP。若使用不同采样权重，应显式处理，不能用整数hits近似后不披露。

分别生成r95/r98表。预先限定候选训练目标为 `{r, r+0.002, r+0.005}`（不超过1），在validation上选择实际达标且最快的候选，然后冻结表及版本。validation均未达标则回退固定ef控制并报告失败；test未达标只报未达标，不能再调表。有限validation不能保证test达标。

为避免平均recall掩盖困难类别，报告各桶recall和最低桶表现；另给“每个样本充足桶都达到目标”的约束版作为消融，不混作主目标。

### 3.4 实现接口

沿用 `run_strict_blocks.py` 扩展参数与配置输入，不再新增一套sweep runner；import放顶层。需要的参数：fixtures/query/GT/bitmap/index路径、split、seed、run_id、blocks、配置文件、输出目录。删除对 `/tmp/les_var` 和 `/tmp/psg_sh100` 的默认依赖或要求显式确认路径和索引manifest。

两引擎增加建议接口 `--ef-policy <file>`（**尚不存在，须先实现**）：

- 离线生成小型JSON/二进制表，字段含schema version、method/index hash、metric、k、桶定义、ef、fallback及训练split hash。
- 启动时加载和验证；缺失/不兼容直接失败，不能静默转固定ef。
- 查询热路径只做桶查找，记录实际ef；策略文件、运行时表内存和在线查找成本全部计入空间/时间。
- `--ef-policy` 与旧adaptive互斥；固定模式显式清空GRAPH_ADAPTIVE等环境变量。
- 相同ef下policy开关输出逐位一致；所有类别命中、fallback、边界499/500/9999/10000都要覆盖。测试聚焦配置错误与路由语义，避免只复述实现。

## 4. 对照矩阵、留存与进入A1的条件

先冻结策略，再对LES-v3(E0)、共享PSG-M12/M16分别比较fixed、旧adaptive、B1。原顺序为主，统一计时、同一CPU、单线程；至少15个随机交错block，block内配置相同。bootstrap按整个block重采样，报告配对比值的95%CI和IQR；每轮ids、timings、命令、环境、recall保存独立文件。

主要问题：B1是否在完整空间仍约53.3MB时提高LES的r98，并相对**同样调参后的**PSG-M12保持竞争力。统计实际空间差，不把53.3与53.6称为严格相等；所有性能结论只在实际达到门槛时成立。报告参数选择成本，不计入在线搜索但不能隐去。

保留逐查询 `expanded_nodes/scanned_edges/visited_skips/upper_distance_evals/l0_distance_evals` 的独立诊断运行；若远端尚未实现，应明确“未完成”，不要用QPS反推瓶颈。

进入A1的条件：B1无可重复收益或高recall差距仍显著，且诊断提示入口/导航问题。届时按旧改进文档§5实施r=0/2/4多样入口，保留原入口并计入距离预算。否则结束本轮，先将已验证的recall–space–time边界扩展到其他原生L2负载，不急于重构图。

## 5. 本轮必须回传

1. M12及0.963点的原始证据补齐或重测记录，统一计时后的fixed控制表。
2. fit/validation/test manifest、GT生成方式、策略表、选点记录与回退情况。
3. 每block及每桶结果、配对CI、错误与负结果；不能只回传最佳策略。
4. 更新论文，区分“旧表观察”“独立test结果”“未测方向”。S1仅关闭full-list interning，删除“只剩两个方向”的穷尽式表述。
5. 同步代码/小型数据/报告到Git，回传commit。大索引留远端并附路径和哈希。
