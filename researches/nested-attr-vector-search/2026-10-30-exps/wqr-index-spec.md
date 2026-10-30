# WQR：共同见证限定的代理路由索引——冻结实施规格

日期：2026-10-30。WQR = Witness-Qualified Routing，仅为本项目方案简称，不暗示已发表算法。本文件由本地研究端完成设计；下游只实施、测量、报告，不再选择机制。本文取代“让服务器先诊断再自行制定新结构”的安排。

**设计结论：采用一个几何块—支持域代理图索引。完整层级/多属性资格先在对象slot上精确求值，再投影为代理支持域的存在性，直接控制代理HNSW的结果准入。** 不同谓词可激活同一非空块内不同代理；不合格代理仍可导航。目录用可cut/link的Euler序列，几何结构不随目录/属性改变。

这是完成算法定义的研究方案，不是已证明领先的算法。已运行真实hnswlib有限机制探针和独立语义验证；尚未运行WIKI/SIFT端到端实验。旧索引迁移可以构成贡献，本方案的具体增量定位在**资格到路由域的映射及其动态维护契约**，不是要求Trie/bitmap/HNSW全新。

## 0. 版本勘误与直接前序

1. **Curator不是必须先全局枚举qualified IDs。** v3 §4.2，本地`papers/filtered-ann/2601.01291-curator/paper.md:673–684`，已明确可仅在预计搜索的子树求值谓词/建临时索引。本库更早记录也已注明；此前草案强调外部qualified-ID输入却漏掉这一优化，不能据此制造差异。
2. [Unleashing Graph Partitioning for Large-Scale Nearest Neighbor Search](https://www.vldb.org/pvldb/vol18/p1649-gottesbueren.pdf)，PVLDB18(6):1649–1662,2025，§3–3.2、§4.3，已给出**每shard多代表→统一HNSW路由→shard内HNSW**。这是直接骨架前序，不把多代表或二级图当新发明。
3. [SIEVE v2](https://arxiv.org/html/2507.11907v2) §2.2/§6已有bitmap资格、hnswlib结果过滤与先bitmap后serving。普通对象过滤与本方案代理支持域过滤不是同一个函数。
4. Curator已把完整资格投影到几何子域；F-IVM/关系代数已有依赖键共享和选择/投影；动态Euler树也经典。本轮已核来源中，未见下面的完整接口：**固定多代理图＋唯一支持域＋按需完整资格的存在性投影＋不准入代理仍导航＋动态目录cut/link不重写对象索引**。这支持具体组合差异，不能证明领域首次。
5. Faiss官方filtered IVF文档说明在list scanner中检查IDSelector；该文档没有说明将支持域非空性用于coarse representative结果准入。这里只用于界定前序，不采用FAISS构建新检索索引。

证据：[支持域修订审查](wqr-prior-review.md)、[初稿对抗审查](wqr-adversarial-review.md)。后者针对旧的整块gate及数据对象portal，最终规则以本文的zone gate与独立proxy为准。新检索还出现Learning Cluster Representatives、Cracking Vector Search Indexes、lakehouse per-file ANN等线索，未核全文，不以未读材料认证新颖性。

## 1. 问题与接口边界

对象有稳定ID、单一直接owner node、int64 category/numeric、uint64 tags、alive、immutable FP32 vector。目录可在内部节点直接绑定对象。查询为：

`scope(anchor,recursive) AND extra(category, numeric closed range, tagmode, combine)`，返回原始FP32 squared-L2 top-k。

完全对齐`dynamic_triad.cpp:94–110,713–764`的V/T/M/B/R/D/I与Q；category=-1禁用，numeric两个端点均-1禁用，否则严格按现有闭区间语义，不能擅自解释为单侧无界。目录始终AND；普通字段AND/OR；禁用字段不参与OR；没有启用字段时extra=true。

本次实施不增加任意布尔AST、embedding替换、冲突MERGE或并发MVCC。它们并非现有replay已支持的接口，不虚称完成。若后续加入embedding更新，执行“旧物理向量删除＋新物理向量插入”并更新稳定ID映射，不能只unmark旧向量。MERGE需先确定命名冲突语义后分解为M/R等事件，不能声称一次cut/link即可解决。

所有在线索引与搜索C++；距离统一`raw_l2_space.h`。hnswlib revision固定`ca426729609b3221563047ceb269cc1213e0d376`。原NodeANN基线不修改。

## 2. 从问题推导出的核心操作

### 2.1 为什么边际摘要不够

令E为完整合格对象集，Z_p为代理p负责的对象支持域。正确的代理资格是：

`Allowed(p,q) ⇔ Z_p ∩ E(q) ≠ ∅`。

若Π把对象集合映射到其所属代理集合，则：

- `Π(A∪B)=Π(A)∪Π(B)`；
- 一般`Π(A∩B) ⊆ Π(A)∩Π(B)`，可能严格包含。

因此不能先把每个属性单独OR聚合到代理，再对这些边际标记做AND；不同对象会分别作见证。**必须先完成同对象合取，再做存在性投影。** 这是关系代数中的经典性质；本方案把它作为代理图准入的正确性条件，不认领该数学事实。

### 2.2 为什么不仅过滤空块

每块有r个代表及各自不交的Z_p。即使块内有合法对象，某个代表的支持域也可能全不合法。整块非空就激活所有代表，会让一个接近q、但只代表非法对象区域的代理占用路由预算。

WQR计算`active_zones[b]`，使**同一块的路由表示随查询资格变化，但路由向量和图边无需重建**。它并不计算新的条件质心，不枚举合格向量求几何摘要，也不引入新的距离度量。

## 3. 存储结构——全部确定

### 3.1 逻辑目录

使用order-statistics implicit treap存每目录的`enter(u), exit(u)`两个token。稳定token句柄带parent pointer、subtree_size、64-bit随机priority（seed100，priority平局按token ID）。rank为当前位置，不持久保存会过期的DFS整数。

- 递归资格：`rank(enter(a)) ≤ rank(enter(owner)) < rank(exit(a))`。
- 非递归资格：`owner==a`。
- 目录记录parent、直接对象posting及对象在posting中的位置；删除用swap-and-pop修正反向位置。
- 构建初始Euler顺序用显式栈，避免原树深度导致调用栈风险。

标准随机treap假设下rank/split/merge期望O(log D)，不是对任意优先级排列的最坏保证。D为目录数。节点rank/祖先缓存仅限单次query，不跨任何更新使用。

### 3.2 几何块

初建目标块大小`B0=1024`。对象唯一属于一个块，一个slot和一个zone；禁止复制对象到多个块以隐蔽提高召回。

每块：

- `slot_to_id`、alive bitmap、对象ID到(block,slot)的全局反向映射；
- 一份局部hnswlib HNSW，external label为稳定对象ID；
- category 64个bitplanes；numeric 64个bitplanes；tags 64个bitplanes；
- numeric用`uint64(value) XOR (1ULL<<63)`保持signed int64顺序；
- `owner→vector<slot>`稀疏posting，每对象在该list中的反向位置，**不为每个owner分配整块dense bitmap**；
- r个zone bitmaps、每slot的zone编号；
- query-local ExactMask缓存及active_zones位串，按query epoch使用；
- block generation，避免split/回收后的旧portal映射指向复用对象。

bitplanes按块容量分配，192 bits=24 bytes/allocated slot，不把它误写成总索引成本。稀疏owner posting总slot条目为N；没有每祖先×每属性组合的资格副本。

### 3.3 支持域代理

初建/块split时对块内live对象做farthest-first，`r_b=min(4,n_b)`：第一个为最小对象ID；后续选到已选集合最小squared-L2最大的对象，平局按对象ID。维护每对象最近代表距离，使选点为O(r_b n_b d)，不重复计算所有已选距离。

**代表选好后，复制其向量为独立routing proxy。proxy label不是源对象ID，不具有源对象的alive/owner/category/tag语义。** 所有proxy组成一张全局hnswlib HNSW。源对象删除不删除proxy；只要块存在，固定proxy可继续导航或代表其他对象。

每对象分配给最近proxy，平局按proxy序号。所得Z_p不交且覆盖块的所有live对象。支持域是离散成员集合，不能把它当作全局空间的精确Voronoi分区；新对象只在其选定块的proxy中分配zone。

局部图和proxy图均`M=16, efConstruction=200, seed=100`。vector payload实际会被hnswlib复制，见§8；不得宣称进程中raw只有一份。

## 4. 构建算法

### 4.1 确定性平衡几何分块

对当前对象集合S：若|S|≤B0生成块；否则：

1. a为最小对象ID；b为距a最远对象；c为距b最远对象，平局按ID。
2. 对x计算`key(x)=rawL2(x,b)−rawL2(x,c)`。
3. 按(key,ID)排序，取中位位置分成两半，再递归。

该分块与任何属性/目录无关，不训练、不调用FAISS。初建两侧大小平衡，不声称几何最优。全部向量距离走公共kernel；排序、初建和选点成本记账。不得在对照之间重新生成属性。

### 4.2 建索引顺序

建Euler目录 → 几何分块 → block slots/bitplanes/owner posting → local HNSW → proxies/zone masks → proxy HNSW。块、对象、proxy ID的插入顺序固定，便于重复。

## 5. ExactMask：完整过滤的固定实现

令V为live mask、S为当前目录mask。普通字段mask列表为F。类别等值用64个bitplane的逐位相等；numeric用MSB→LSB的`less/equal`电路做两个闭边界比较，不逐对象解释数值。

```text
Compare(planes, encoded_bound):
    eq = V; lt = 0
    for bit = 63 .. 0:
        if bound[bit] == 1:
            lt |= eq & ~planes[bit]
            eq &= planes[bit]
        else:
            eq &= ~planes[bit]
    return (lt, eq)
Range(lo, hi) = (V & ~LT(lo)) & (LT(hi) | EQ(hi))
```

标签bitplane T_t表示包含tag t的slots：

| mode | mask |
|---|---|
| 1，Q⊆ITEM | 对Q中的t取AND T_t；Q为空返回V |
| 2，Q∩ITEM非空 | 对Q中的t取OR T_t；Q为空返回0 |
| 4，ITEM⊆Q | V AND NOT(对Q以外的t取OR T_t) |
| 3，ITEM=Q | mode1 AND mode4 |

所有补集限制在有效live slots。tagmode0禁用。

目录mask：遍历该块稀疏owner posting；owner首次在query出现时查当前Euler rank并缓存scope真假；符合scope的posting slots置位。非递归直接ID比较。没有全scope向量扫描。

`Extra = V`（F空）；否则按combine对F做AND或OR。

`E_b = V AND S AND Extra`。

一次计算E_b后，对该块全部r个zone计算`active[p]=any(E_b & Zone[p])`；后续proxy callback只读active位。若E_b为空，所有proxy都禁止进入结果集，但图导航仍可经过这些proxy。

## 6. 搜索算法——一次路由，不由服务器另定策略

输入q,k和正整数块probe上限L。P=`min(4L, 当前proxy总数)`。空图直接返回空。

```text
Query(q, scope, fields, k, L):
    create query-local mask/owner caches
    set portal ef = max(configured_portal_ef, P)
    candidates = portal_graph.searchKnn(q, P, PortalAllowed)
    sort candidates by (raw squared-L2, proxy_label)
    blocks = first L DISTINCT block IDs in that order
    answer = empty bounded top-k heap
    for b in blocks:
        E = cached ExactMask(b)
        kb = min(k, popcount(E))
        if kb == 0: continue
        local_graph[b].setEf(max(configured_local_ef, kb))
        hits = local_graph[b].searchKnn(q, kb, LocalAllowed(E))
        merge hits into answer; final order by (distance, objectID)
    return answer plus actual-work counters
```

`PortalAllowed(p)`检查proxy元数据对应当前live block/generation，随后返回`active_zones[b][p.zone]`。不检查源对象资格，不在callback中按“曾见过块”去重。

`LocalAllowed(id)`用当前id→slot映射检查E，不能把internal HNSW node ID当对象ID。

不合格proxy/对象可按hnswlib原行为参与导航，callback仅决定结果准入；不修改库以截断导航。保持同一个query快照。

### 固定预算与停止

- 最多L个不同块各执行一次local search；没有隐式全scope扫描、GT停止、自动重试或新增recall模型。
- `ef`是beam参数，不是距离次数上限。稀疏/空结果可能遍历整个可达proxy图或局部图。
- 返回少于L块或少于k对象时原样报告，不冒称证明了全局无更多合法对象；只有独立GT能判定是否召回不足。
- 若恰返回P个不同proxy且每块最多4个，则至少ceil(P/4)个不同块。P≥4(L−1)+1足以保证至少L块，**前提是实际返回足量proxy**，不是HNSW召回定理。
- portal distance不作为未探测块的安全下界，不能与当前第k距离比较后剪枝。

## 7. 更新与生命周期——全套现有事件

所有更新串行同步提交，查询只见更新前或完成后的状态。workspace cache按query隔离；不宣称并发事务支持。

| 事件 | 确定操作 | 几何维护 |
|---|---|---|
| V | 修改同slot的category/numeric变化bitplanes，更新canonical属性 | 无local/proxy图或zone变化 |
| T | 对old XOR new的tag位更新同slot | 无几何变化 |
| M | 源/目标目录直接posting与块内owner posting swap-and-pop维护；更新owner | block/slot/zone与两类图均不变 |
| B | 先冻结源subtree当前全部live ID，再对每个ID执行M到指定目标；不能边遍历边改变冻结集合 | 不改几何图，但O(源目录token数＋移动对象数)及posting代价全计费 |
| R | 验证非root、目标不在源subtree；cut `[enter(src),exit(src)]`，在**cut后重新求**目标exit的rank，insert到exit之前，更新src.parent | 对象owner、posting、bitplanes、zone与两类图均不变 |
| D | live bit清零、属性位清理、owner/direct posting删除、zone成员清理、local markDelete；触发下述回收则同步做 | proxy不是对象；删除源对象不删除proxy。空块才移除该块所有proxy |
| I | 激活reserve或恢复immutable-vector ID；见下一段定位/slot规则 | 必要local插入/恢复、proxy插入/块split及回收均计费 |

### I与容量

- 被删ID的旧(block,slot)若仍保留且local图包含同一immutable vector，恢复旧slot并unmark；更新全部属性/owner/zone状态。
- 若旧slot已被压实/复用或旧块退役，则视作物理新插入。`id→current location`是唯一真值，旧元数据不能恢复到错误块。
- 新对象通过proxy graph做**不带资格过滤**的最近8个proxy搜索，从返回者中取最近proxy所属块；再按该块最近proxy分配zone。空索引创建新块及该对象proxy。插入时的ANN路由代价计入I。
- 新对象占用空slot；若复用slot，先清理旧id位置/tombstone映射，local HNSW external label仍用新对象ID。内存槽复用不等于用新vector unmark旧label。**不启用hnswlib随机replace_deleted**；元数据slot与HNSW internal node分开管理。若该external label在选定图中仍有同一immutable vector墓碑，可unmark并绑定新slot；否则addPoint。
- physical graph node数已满但logical live尚未满时，先重建live图回收墓碑再插入；不能只看free metadata slots就调用越容量addPoint。新容量与构建成本计费。
- 当前块已有2B0个live对象时，在写入旧块数组之前，将新对象与旧live IDs组成临时2B0+1集合，按§4平衡方法拆成两个块；重建局部图、proxy/zone/bitplanes并将旧proxy标删，在事件结束提交新状态。不先越界写旧slot。构建时双份峰值计费。

### 确定性回收，不增加研究调参主线

- local dead/(live+dead)>0.2时重建当前块local图，压实slots并同步重建bitplanes、owner postings与zone masks。**保留原proxy向量及各对象zone归属**，不因源对象删除强制重选代表。
- local容量不足按2倍增长，最大到2B0；小块若live<capacity/4，在同一维护事件缩到`max(16,2*live)`。其图重建/拷贝均记账。
- 空块删除其proxy并退役；proxy图dead/(live+dead)>0.2时全图重建，移除墓碑和旧映射；容量不足按2倍增长。proxy可重新连续编号，但必须与metadata原子切换。
- 不做主动块merge。删至很小的块会增加路由/局部图开销，这是明确退化，不隐去；空块必须回收，metadata注册表不得随历史更新无限增长。注册表空洞>20%时同步压实映射及query workspace索引，成本计入维护。
- 插入漂移可能令固定proxy覆盖变差；本版只在新块/split时选代表，不偷偷加入质量触发重聚类。该退化是实验失败候选，不由服务器擅自改算法补救。

## 8. 正确性、可证明性质与成本

### 8.1 硬资格与覆盖不变量

1. 每个live对象唯一属于(block,slot,zone)，`union zones=live slots`且zone两两不交。
2. ExactMask逐slot等于canonical完整谓词；所有输出来自LocalAllowed，故不存在合法性放宽。
3. 对每个合法对象x，其唯一zone的proxy必满足Allowed。**资格投影不会删除x的唯一代理**；有限近似路由仍可能不访问它。
4. zone gate允许的proxy集合是block gate的子集；不是召回/时延单调优于block gate的定理。
5. R只改变目录Euler序列与src.parent，不触及对象owner/几何状态；M只改变归属索引；V/T只改同slot属性。此不变量不依赖目录深度复制成员列表。
6. proxy向量为独立索引数据；源对象删除不能消灭仍非空zone的路由入口。空zone proxy可以导航，但不能返回。

### 8.2 几何近似边界

在实数Euclidean模型，若每zone内对象距proxy≤ρ、完整资格投影精确、**所有活跃proxy按精确距离排序**，选最近活跃proxy p并精确搜索其块，则返回最近合法距离≤d*+2ρ：最优x*的proxy距离≤d*+ρ，而p的非空zone存在合法y且dist(q,y)≤dist(q,p)+ρ。

该简单覆盖界不是新定理；ρ在WIKI可能很大，且实际portal/local HNSW不满足两个精确检索前提。**不把该界写成实际近似比或FP32安全停止条件。** 实际存在中心合法、四周proxy很远的反例；见§10。

### 8.3 查询工作量

记C为块数，R_p≤4C为proxy数，d为向量维数，U为实际求mask的块集合，J≤L为局部搜索块数，W_b=ceil(capacity_b/64)，o_b为块内owner数，s_b为构造scope mask实际读取的slot条目数，A为query首次遇到的不同owner数。

位切片和scope成本：

`Tmask = O((A+1) log D + Σ[b∈U] ((192+t_b+r_b)W_b + o_b + s_b))`。

其中192对应64-bit category及两次64-bit numeric比较的数量级，常数逻辑操作另计；t_b≤64。mask缓存每块一次，但U可能等于C，绝不写U≤L。scope lookup使用稀疏posting，s_b≤n_b。

总查询：公共距离计算`d*(Cportal+ΣClocal)`＋真实邻接访问/heap操作＋Tmask＋返回proxy排序`O(P log P)`＋top-k合并`O(J k log(k+1))`。HNSW没有在这里被假定为最坏O(log N)。

### 8.4 更新成本

V≤128位更新、T≤64位更新（按变化位循环）；M在哈希字典＋swap-and-pop假设下期望O(1)posting修改，不涉及向量距离；R为常数次rank/split/merge，标准随机treap下期望O(log D)，不随子树对象数修改posting。

B不是R，需冻结和逐对象归属修改。I/D的local/portal ANN操作、rebuild/split、容量迁移使用实测成本函数，不伪造O(1)或对数界。proxy固定策略使单纯D不必重算farthest-first，但回收可能全局重建proxy图。

### 8.5 空间口径

原始canonical向量`4d*N_raw`；hnswlib local向量存储按实际总capacity计`4d*S_local`；proxy catalog与proxy HNSW各有向量副本；另加两类图边/上层/label tables、192个bitplanes、zone masks、owner/direct posting、Euler tokens、free slots/墓碑/registries、workspace和重建双份峰值。

仅属性bitplanes为`24*S_slots` bytes；每个对象不复制到多个local图，但**canonical与HNSW内部payload确有副本**。NodeANN的小直接节点可能没有图副本，本方案必须承认这一额外空间，不能只报新增bitmaps很小。

## 9. 下游实施文件与冻结验收

新增`src/wqr_index.h`及`src/wqr_research.cpp`，复用现有读取/trace语义与RawL2Space；不改旧dynamic_triad行为和正式数据。构建入口单独生成wqr二进制。接口固定为：

```cpp
build(canonical_items, parent, raw_vectors);
query(Query, k, probe_blocks, ef_portal, ef_local) -> RankedIdsAndCounters;
apply_V(id, category, numeric); apply_T(id, tags);
apply_M(id, dst); apply_B(src, dst); apply_R(src, dst);
apply_D(id); apply_I(id, owner, category, numeric, tags);
```

版本号按现有事件提交；任何字段/维护未实现必须报错，不能no-op、假fallback或把失败的事件从tape删除。C++完整实现应使用相同tape/GT协议，不能把本文小探针当生产实现拷入后宣称完成。

第一轮固定B0=1024,r=4,M16,efC200；local ef=1024。L按`1,2,4,...,C`（去重并包含C）扫；portal ef仅`max(64,4L)`与`max(256,4L)`两档。参数是检验质量—成本曲线，不由服务器改变机制。先原WIKI/SIFT小集，不扩百万。

四臂使用同一分块/图/向量/属性：

1. NodeANN现有基线（独立原设计，不强制其使用几何块）。
2. **GeometryRoute**：proxy全部准入，局部仍完整资格过滤。
3. **BlockGate**：E_b非空就准入该块全部proxy。
4. **WQR**：E_b AND Zone_p非空才准入p。

主因果对照是3→4，不用1→4的所有差异冒充单一机制收益。另做**Eager-WQR**控制：相同ExactMask预先对所有块计算，其他完全相同，以分离按需求值收益；这不是新算法分支。第一轮不实现Memento/FlexFlood/RL/新量化/第二套图剪枝。

质量沿现协议：raw FP32 squared-L2、k10、run级macro/pair均≥.999、invalid/duplicate/state=0。空GT按旧协议统计，但额外单列非空query质量，不能靠空query稀释。记录route_coverage=GT中位于最终选块的比例、条件local recall（分母为选块中的GT；0时NA），以及完整最终recall。

**离线路由归因**：对同一snapshot，将全部active proxies精确排序，模拟同L选块；此过程只诊断，不进入在线计时或停止。若精确proxy路由也覆盖不足，是代表/块组织问题；精确路由好而HNSW路由差，是portal ANN损失；选块覆盖好而最终差，是local ANN损失。

必要计数：portal/local真实距离次数、邻接访问、callback次数、U、owner rank查询/缓存命中、bitset word操作、active zone数、实际J、构建/更新/回收字节与时间、常驻/峰值内存、query全生命周期p50/p95/p99。所有未被最终选中但触发mask的块也计费。

## 10. 已运行证据与明确失败判据

### 本地语义与真实HNSW机制运行

- `wqr_semantics.py`：120960次bitplane mask与独立scalar/parent-walk结果比较，0不一致；覆盖initial,V,T,M,B,R,D,I恢复、I reserve；另做135次合法Euler cut/link，每次核49对祖先关系；R重写对象owner数0。只是有限实例，不是形式化全称证明。
- `wqr_hnsw_probe.cpp`：真实hnswlib双层图＋公共FP32 L2，检验zone gate、重复块、proxy生命周期和空query。不是WIKI/SIFT性能benchmark；probe只用布尔资格fixture，完整四种tag等由上面的独立语义程序验证。
- 机制正例：块A的proxy位于0/10/20/30，但只有30合法；块B合法向量从1开始。q=0,L=1，BlockGate返回squared-L2=900，WQR返回1。它验证资格改变了非空块的代表准入，不是速度提升数字。
- 反例：块A有四个距离q为10的端点及合法中心0，块B在距离1附近。WQR的L=1仍返回distance²=1而GT=0；L=2恢复GT。**固定proxy不能保证最近合法块。**
- 源对象删除后保留proxy，仍能路由并返回其支持域中的合法中心；不将代理当作被删除数据对象。
- 不扩量时取2个proxy只得到1块；取8个proxy得到2块。空资格query仍做8次portal距离并准备2个块mask，说明ef/返回块数不是计算量上限。

本机`codes/directory-aware-vector-attr-search/deps/hnswlib`为空；使用已有`codes/vector-join-reproduction/build/_deps/hnswlib-src`，核HEAD为指定revision，目标headers相对HEAD无修改。编译成功；保留第三方header的unused/type-limit warnings，不压制或冒称零warning。命令与原始诊断见[复现记录](experiments/wqr-reproduction.md)，结果在[data/wqr-semantics.json](data/wqr-semantics.json)、[data/wqr-hnsw-probe.json](data/wqr-hnsw-probe.json)。

### 服务器失败判据，不再让其改方案

1. 资格/状态不一致：实现错误，先修，不记算法性能失败。
2. 精确proxy排序在L远小于C时仍不能接近质量门，且L≈C才通过：**当前代表路由压缩失败**，停止扩大规模，回传coverage曲线；不自动更换索引。
3. WQR相对BlockGate仅在整块为空时受益，而同样非空块中的失活zone没有作用：不支持“条件代表选择”这个核心贡献；即使整体胜NodeANN也不能归因于它。
4. 门槛内mask/owner-rank及portal访问成本吃掉节省的local ANN，或更新/峰值内存抵消：当前结构全成本失败，保留数据，不改写为“整个过滤方向失败”。
5. 只在人为极端fixture有效、真实两数据和独立query无稳定作用：只能支持机制存在性，不足论文。

**足以继续论文主张的证据组合**：相对直接多代表路由与BlockGate，在同质量/资源下显示条件代表选择减少无用块探测；层级变化/属性churn下证明几何维护触达量不增长，并计入Euler/资格代价；消融区分资格投影、按需求值和普通分块。复用基础索引不妨碍贡献，但这些机制差异及实验必须真实。
