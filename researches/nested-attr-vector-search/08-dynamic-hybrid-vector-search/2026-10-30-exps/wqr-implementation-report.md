# WQR（Witness-Qualified Routing）实施与检验报告

日期：2026-10-30。实施对象：冻结规格 `de3180a` 的 wqr-index-spec.md / wqr-server-prompt.md / data/wqr-contract.json。实施基线：本仓库 `0837918`（第二轮联合信息诊断之后）。hnswlib 固定 `ca426729609b3221563047ceb269cc1213e0d376`（deps 子模块，headers 与 HEAD 无差异）。全部原始距离走公共 `RawL2Space`（构建期经 `CountedL2::cdist` 计数，图内经 `get_dist_func` 计数），无手写距离核。

## 结论（先给）

**WQR 正确实现后未达到论文门槛，失败归因是“代表打分压缩不足”（判据2），不是实现缺陷。**

1. 正确性：80 次网格运行 + 6 次 screening replay，invalid=0、duplicate=0、state_errors=0；E0 九组用例全过（独立 canonical 逐 slot 核对）。
2. 机制增量真实存在但不足：BlockGate→WQR 在同块同图下同 L 一致提升 route coverage（WIKI L=8：0.8259→0.8479；SIFT L=8：0.9760→0.9789），支持域资格投影不是无操作。
3. 质量门失败：run 级 macro/pair recall ≥0.999 只在 **L=C=16（探测全部块）** 达到。精确 proxy 参考（refCov）与在线覆盖完全一致（signed delta = +0.0000，所有配置），且 ef=64 与 ef=256 结果逐位相同——portal HNSW 在 64 个 proxy 上实际是精确的，**损失全部来自固定 proxy 代表本身**，不在 HNSW 近似，也不在 zone gate。
4. 同质量全成本：质量门处（L=16）WQR e2e 均值 6052μs vs NodeANN（dynamic_triad arm C，ef1024）p50 6326μs；每 query 距离调用 5634 vs 9440；但峰值内存 206MB vs 113MB（约 1.8×，来自16张局部图+proxy图+canonical+proxy payload 副本）。查询侧没有可复现的净收益，空间显著更贵。
5. 维护解耦成立（G2）：V/T/M/R 零距离调用；R 实测 0.7–0.9μs；B 35–102μs（posting 重写）；D 11μs；I 305μs（含路由+HNSW 插入）。目录/属性事件不触碰几何图。

**处置：按冻结判据停止扩规模，不更换算法，回传全部 coverage 曲线与成本数据。**

## 1. 实现与规格差异声明

新增文件：`src/wqr_index.h`（索引+全部事件）、`src/wqr_research.cpp`（replay runner，与 dynamic_triad 同数据/trace 协议）、`src/wqr_e0_tests.cpp`（E0 套件）、`run_wqr_e1.sh`（网格驱动）。

实现中已修复的阻断性问题（相对 8649e55 初稿）：

- ExactMask 分为 Live/Scope/Extra 独立构造后合取；Extra 按启用字段正确 AND/OR（初稿先 AND 再用错误中间结果修补 OR）。
- Euler 构建改为真实 DFS enter/exit 事件序（初稿先全 enter 后反序 exit，分叉树即错）；修复 DFS 迭代器在 emplace 后悬垂引用。
- LocalFilter 以外部对象 ID 经 `loc[id]` 映射 slot（初稿把 label 当 slot）。
- numeric 仅在 lo=hi=−1 时禁用；否则字面闭区间（含 −1 端点）；位序 LSB 索引 + MSB→LSB 比较电路，int64 经 XOR(1<<63) 保序。
- 距离计数：portal/local 按本次查询差值计数（pspace/各块 space 独立计数器），构建期 partition/proxy/zone 距离走 `build_space->cdist` 单独入账；修复 `RawL2Space::dist` 绕过计数与 `build_space` 赋值后 `param_` 悬垂（段错误根因）。
- B/D/I 生命周期完整：reserve 首次 I、空索引首 I、2B0 split（实测 splits=1 于 2051 对象边界）、墓碑压实、空块 proxy 回收、registry 压实、容量增长；无“容量不足报错”捷径。
- slot 空位标记统一 `NONE=SIZE_MAX`（消除 uint32_t/SIZE_MAX 不一致）。

已知未覆盖：embedding 替换、MERGE、MVCC 不在合同内，未实现（与规格一致）。

## 2. E0：正确性（全过）

`build/wqr_e0_tests`，9 组用例，全部以独立 canonical（标量谓词 + parent-walk）逐 slot/逐对象核对：

| 组 | 内容 | 结果 |
|---|---|---|
| 1 | 分叉树 Euler 全对祖先关系、R 后重核、内部节点直接归属 | pass |
| 2 | AND/OR、空 tag（mode1空Q=全过/mode2空Q=全不过）、字面 −1 端点、负 category、int64 min/max、70 对象跨 bitmap word | pass |
| 3 | D/I 恢复（slot 复用、zone 恢复为最近 proxy 而非强制 0）、交错更新 | pass |
| 4 | 空索引首个 I、reserve 首次 I | pass |
| 5 | 删除 proxy 源对象后其支持域对象仍可路由 | pass |
| 6a | 代表混叠正例：BlockGate=900 / WQR=1（与规格§10预期一致） | pass |
| 6b | 反例：圆周四端点+合法中心，L=1 丢失 GT、L=2 恢复 | pass |
| 7 | 2B0 容量边界 split（2051 对象，splits=1，全部 loc 一致） | pass |
| 8 | 非连续 live ID（奇数 id）、全空资格查询返回空 | pass |
| 9 | V/T/M/B/R/D/I 与 Q 交错，每步 canonical 核对 | pass |

## 3. E1：机制对照（真实数据）

数据：WIKI root-3626 native（11869×1024，151 目录节点，qu1 trace：5935 Q + 5935 M）；SIFT crossover sift-n12000-r1000（13000 含 1000 reserve，r0p2-qu1 trace：3000 Q + 3000 更新混合 V/T/M/D/I）。两数据集几何分块均得 **C=16 块、64 proxy**。属性快照直接来自数据集文件，未重新生成。GT 独立 canonical 两遍口径；GT 缓存带 eligible 数与逐 id 合法性校验。

冻结参数：B0=1024，r=4，M=16，efC=200，local ef=1024；L∈{1,2,4,8,16}；portal ef∈{max(64,4L), max(256,4L)}。

WIKI 主表（ef=256；ef=64 逐位相同，略）：

| L | 臂 | macro recall | route cov | exact-ref cov | 每query距离 | e2e均值 |
|---|---|---|---|---|---|---|
| 1 | G/B/W | .270/.459/.469 | .088/.324/.337 | 同左 | 427/682/681 | 450/1236/1327μs |
| 2 | G/B/W | .330/.559/.574 | .162/.449/.468 | 同左 | 779/1178/1178 | 911/1944/1945μs |
| 4 | G/B/W | .444/.692/.712 | .305/.615/.640 | 同左 | 1476/2074/2074 | 1763/2991/2960μs |
| 8 | G/B/W | .654/.861/.878 | .568/.826/.848 | 同左 | 2885/3615/3615 | 3532/4620/4537μs |
| 16=C | G/B/W/E | 1.000 全部 | 1.000 | 1.000 | 5634 | 6051μs（W） |

SIFT 同构（L=8：B .9820 / W .9842；L=16 全部 1.0000）。Eager-WQR 与 WQR 结果逐位一致；lazy 准备未减少 mask 工作（该 trace 中 portal 导航几乎触及全部块）。

NodeANN 基线（dynamic_triad arm C，ef=1024，同 trace 同 GT 协议）：WIKI macro .99995 / pair .99991，p50 6326μs，9440 距离调用/query；SIFT 基线同跑。

**因果读法**：GeometryRoute→BlockGate 的大幅差距是资格过滤本身；BlockGate→WQR 的 +1.0~+2.2pp 是支持域投影的真实增量。但 exact-ref=在线、ef64=ef256 共同证明：在 L<C 时剩下的损失是**固定 proxy 代表对合法对象分布的覆盖缺口**，无法靠增大 ef 或修正 HNSW 近似解决。质量门只在 L=C（等价于探测全部块）达到，此时路由压缩收益为零。

## 4. E2：动态完整成本

SIFT r0p2-qu1（arm W，L=16）：V p50 5.2μs、T 4.9μs、M 6.6μs、D 11.4μs、I 305.4μs（含 proxy 路由 + 局部图插入）；V/T/M 的 ann_insertions/deletions 全 0。WIKI qu1：M p50 5.9μs，0 图操作。screening R（5 次）：0.7–0.9μs、0 距离调用；B（3 次批量移动）：35.3/96.8/102.5μs、0 距离调用。**G2（属性/目录变化不动几何图）实测成立。**

空间（WIKI，L=16 各臂相同）：图 50.7MB + canonical 48.6MB + proxy 副本 0.26MB + bitplane/posting/zone 0.50MB，峰值 RSS 206MB；NodeANN 基线 index 52.8MB、RSS 113MB。**WQR 常驻约为基线 1.8×。** SIFT 同比（8.4MB 图 + 6.7MB canonical，RSS 77MB）。

窗口内未触发 local 压实/proxy 图重建（死比未超 0.2）；E0 case7 已验证 split/回收路径正确。I 的 305μs 是主要维护成本来源。

## 5. 失败判据对照

1. 资格/状态一致性：全绿 → 非实现错误。
2. **精确 proxy 参考仅在 L=C 过质量门 → 代表路由压缩失败（成立）。**
3. WQR 相对 BlockGate 在非空块内失活 zone 有一致增益（非仅空块效应）→ 核心机制增量存在但不充分。
4. 同质量下全成本：查询时间打平、距离调用略省、内存 1.8× → 全成本不划算。
5. 机制正例（6a）+ 真实数据一致增量 → 不只是人为 fixture 存在性，但达不到论文门槛。

**归因：proxy representation loss（非 portal/local HNSW 损失，非资格求值成本，非维护成本主导）。**

## 6. 数据与复现

- 聚合：`work/wqr-e1/e1_aggregate.json`（40+40 配置全量）；每 query CSV 在 `work/wqr-e1/*.{queries,updates,summary}.csv/txt`（未入库，体量约百 MB）。
- GT 缓存：`work/wqr-e1/wiki-qu1.gt`、`sift-r0p2-qu1.gt`（带 eligible 校验）。
- screening R/B/mixed replay：`work/wqr-e1/screen-*`。
- 构建：`g++ -O3 -march=native -std=c++17 -o build/wqr_research src/wqr_research.cpp -Ideps/hnswlib -Isrc -lpthread`（E0 测试同理换源文件）。
- E0：`./build/wqr_e0_tests` → `E0 ALL PASS; failures=0`。
- 网格：`bash run_wqr_e1.sh`（14 并行，GT 缓存复用）。

## 7. 对后续研究的约束

- “固定少量 proxy + 谓词投影”在此数据上要把 L 提到全块才过质量门；若继续该方向，问题应重新表述为**代表分配/数量自适应**（而这正是冻结规格明确排除的“临时换算法”，应由研究端决策）。
- G2 维护解耦证据可作为后续任何层级动态方案的正面组件复用。
- WQR 的全部负面证据（coverage 曲线、exact-ref、ef 不变性）已完整保存，供新颖性排查与失败复现。
