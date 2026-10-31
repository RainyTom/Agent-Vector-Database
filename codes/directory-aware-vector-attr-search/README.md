# Directory-Aware Vector + Attribute Search — 论文复现

复现对象：**Directory-Aware Query and Maintenance in Vector Databases**
(arXiv:2606.16903v1, Wang et al.)。本地全文与核查记录见
[paper.report.md](../../papers/prefix-vector-search/2606.16903-directory-aware/paper.report.md)。

与论文的差异（复现声明）：

| 项目 | 论文 | 本复现 |
|---|---|---|
| 数据集 | WIKI-Dir 1.94M×1024d / ARXIV-Dir 2.76M×1024d | 已完成完整 WIKI 1,941,679×1024d 两类基线；历史 SIFT/子采样结果保留 |
| 层级属性 | 真实 Wikipedia 类别树 / arXiv subject+temporal | 初始合成模拟（见下）；另有真实目录工作负载 |
| ANN 执行器 | Viking PG + IVF | 全局 FAISS HNSW M16（PG 近似替代，非同一实现）+ IndexIVFFlat nlist=1024；另有每节点局部 ANN 基线 |
| ID 集合 | Roaring bitmaps | CRoaring v4.3.1(与论文一致) |
| 硬件 | 48-core Xeon Silver 4310 | 本机 WSL2(单线程计时) |

合成层级属性(`scripts/gen_workload.py`,seed=20261026):

- **wiki-like**：深随机树，19,000 目录、最大深度 14、均深 13.4（论文 WIKI-Dir
  363k 目录、均深 11.95);90% 对象绑定在深度≥8 的目录，保证浅层锚点挂大子树
  （论文 Fig. 10 的关键结构性质）。
- **arxiv-like**：两个独立命名空间——subject 树 1+14+42+168（均深≈2.19,
  同论文 168 目录）;temporal /yyyy/mm 树 1+20+240（均深 1.70，论文 1.92)。
- **workload**:wiki 500 查询（锚点深度混合按论文 Fig. 10:1/2/4/9 层
  计数比 83/39/72/114),arxiv 1000 查询（subject/temporal 各半），每个查询
  带 recursive / non-recursive 目录约束（各 50%)。GT = 可行集暴力 top-10
  （同论文 ARXIV-Dir 协议）。
- **DSM**:wiki 命名空间上 1000 MOVE + 1000 MERGE（同论文数量）。

## 两类基线与维护语义

**A：全局 ANN + scope ID 过滤。** 一个 HNSW 或 IVF 索引覆盖全体向量，三种目录表示（`src/repro.cpp`）分别解析同一 scope 的候选 ID 集，再交给全局 ANN：

- **PE-Online**：目录身份是完整路径字符串，仅存直接父路径的 Local posting；recursive 枚举后代路径键并 union，non-recursive 直接查 Local。MOVE 必须枚举移动子树的键并重映射，不能视作原生树的一次重挂。
- **PE-Offline**：扁平路径键上物化祖先 Inclusive posting；recursive 一次查找，non-recursive = Inc(v) − ∪ Inc(child)。MOVE 同样重映射子树键，并维护新旧祖先的成员集合。
- **TrieHI**：Trie 节点 + Inclusive 集合，查询语义同 PE-Offline；MOVE 重挂子树而无需重写所有后代路径键，但仍维护新旧祖先聚合集。

`IDSelector` / `sel` 限制结果资格；这不等于属性谓词已下推到图遍历，也不能据此断言不合格节点必被跳过、不再扩展。未核对具体版本源码前不作该机制判断。

**B：节点树 + 每节点局部 ANN。** 使用 `--node-algorithm hnsw`：每个非空节点建局部索引，递归查询枚举候选节点，各搜 top-k 后合并；非递归只搜锚节点。该基线的 `--node-hnsw-threshold 0` 仍表示全部非空节点使用HNSW，大阈值/混合Flat仅作对照。`all3`与`dsq_scan`默认保持该基线，不因新增候选而替换旧复现实验。N<k、截断节点和局部HNSW自身均可能造成近似损失。

DSM 语义：MOVE = 子树重挂；MERGE = u 的直接绑定条目迁移到 w、u 的子目录作为节点单元转挂到 w、u 删除（论文 §V-C “transferred as node units”）。PE-* 额外承担子树路径键重映射；PE-Offline 与 TrieHI 还维护新旧祖先聚合集。参照[论文相应说明（第 5 页）](https://www.alphaxiv.org/abs/2606.16903?page=5)。

成本须区分结构与集合维护：$O(m_u)$ 表示移动子树键的重映射次数（不含字符串长度成本）；$O(t)$ 仅表示祖先集合操作次数，不含位图规模及差/并运算成本。当前公共 `Tree::relink` 还遍历后代更新 depth，不能把 TrieHI 或 NodeANN 整个 MOVE 实现称为 $O(1)$。NodeANN 的局部索引随节点保留、无需因重挂重建，不等于整次 MOVE 无后代成本。公共 catalog 的历史 DSM 计时排除项沿用原实验记录，不能等同于实现没有该开销。

完整 WIKI 输入为 `datasets/dbpedia_dir_2m_corpus_vectors.fvecs`（7,960,883,900 bytes，1,941,679×1024）。本轮已修复 fvecs 转换的 int 位模式数值转换错误，并确认旧 `data/real/test.f32` 受影响；未核查的历史实验不推断污染范围。NODE 查询已移除额外全局 scope 位图解析；旧457行约束改为456条有效查询，运行器拒绝越界 qidx。新结果与旧ANN质量/延迟不可直接比较，旧目录/DSM记录不因向量转换自动失效。

## 运行

```bash
# 1. 生成数据(需要 nested-attr work venv 的 h5py)
production/nested-attr-vector-search/work/venv/bin/python \
  scripts/gen_workload.py --out data/sift100k

# 2. 构建(CRoaring 已预构建在 deps/croaring/build;
#    FAISS 使用仓库 deps/PathSeer/faiss 的构建产物)
./build.sh

# 3. 运行全部实验(HNSW efSearch=2048、IVF nprobe=256,可用 --ef/--nprobe 调)
#    --csv 指向研究报告数据目录(代码与报告分离,见下)
REPRO=../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-triehi-reproduction
LD_LIBRARY_PATH=../../deps/PathSeer/faiss/build/faiss \
  ./build/repro --data data/sift100k --csv $REPRO/data/results --mode all

# 4. 绘图(PNG+SVG 写入 $REPRO/figures)
cd ../..
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/scripts/plot_repro.py
```

完整 WIKI 两类基线（**仓库根目录**运行；默认局部全 HNSW，Flat 仅对照）：

```bash
bash codes/directory-aware-vector-attr-search/build.sh
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-27-exps/experiments/run_wiki_full.py
production/nested-attr-vector-search/work/venv/bin/python \
  researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-27-exps/experiments/summarize_wiki_full.py
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/tests/test_fvecs_to_f32.py
```

输入准备、固定约束、GT/empty-scope口径和内存限制见[复现协议](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-27-exps/experiments/reproduction.md)，数字见[全量报告](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/report.md)。原CSV的 `recall10` 仍为历史 `hit/10`；标准macro/global recall由新增计数列汇总。NODE `bitmap_us=0`，局部搜索/合并计入独立 `search_us`。

历史选择率诊断给出过 ef 随选择率缩放的经验拟合，不是普适参数保证。完整 WIKI 本轮使用固定 ef=2048、nprobe=256，并单列 nprobe=1024 精确对照；极小 scope 下全局 ANN 召回仍低，不能称该默认值已覆盖 workload 或达到同召回最优。

## 实验报告(与代码分离)

本方向的协议、方案、文献笔记、报告、数据、图和实验命令统一放在
[researches/nested-attr-vector-search/01-directory-aware-acceleration/](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/README.md)。
每日记录按 `<YYYY-MM-DD>-exps/{report.md,data/,experiments/}` 保存；已有复现目录保留原名。
例如 [TrieHI 复现](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-triehi-reproduction/2026-10-26-triehi-repro-results.md)与[完整 WIKI / HLG 进展](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/report.md)。
历史静态复现曾要求用户执行；2026-10-29 动态研究按用户授权由助手运行。各实验需保留命令、资源约束、原始失败和判定标准，不混用不同协议。

自检：每个 DSM op 后可设 `DSM_AUDIT_EACH=1` 逐 op 重算 Inclusive 集合
audit;结构不变量(parent/children/path-key 一致）默认每 op 校验。

## 新索引研究的固定 autoresearch 基准

仓库根目录运行 `bash autoresearch.sh`：完整WIKI、独立冻结NumPy GT、局部全HNSW起点，串行3次重复；输出主指标 `METRIC recursive_e2e_ms=...` 与召回/构建/RSS等副指标。每次重复均须通过质量及资源门槛，否则非零退出。`result_ids` 为新增per-query列，输出与独立质量审计均在查询计时外。

[固定协议及新颖性边界](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/experiments/autoresearch-harness.md)；[已观测baseline](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/data/autoresearch_harness_baseline.json)。已完成12轮静态DSQ迭代，最佳保留候选及消融见[报告§9](../../researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-28-exps/report.md#9-autoresearch-静态dsq迭代)，不是已通过新颖性认证的论文方案。

### Scope-run 候选索引

单独`--executor node`的DSQ默认采用新候选；显式配置为`--node-algorithm scope-run --scope-mode 4 --scope-local-order geometry`。静态single-binding，DFS区间及每节点内部符号编组，tile8 AoSoA与global top-k cutoff反馈；每个绑定仅一个向量副本。

- `--scope-mode 0`：row布局穷尽L2扫描；mode1为四路交错对照，mode2为逐点partial-distance对照。
- mode3：转置tile8穷尽扫描；mode4：每64维进行tile级共享截止反馈。mode4的浮点容差不是形式化精确性保证，本轮独立GT召回为1。
- `--scope-local-order original`：关闭节点内部几何编组，保持顺序payload物化，用于关键消融。
- `--node-algorithm hnsw`：保留原局部HNSW/Flat路径；全局HNSW/IVF与三种目录表示未移除。
- 不支持多绑定/DAG及MOVE/MERGE；拒绝重复global ID。物理DFS布局不继承TrieHI的动态重挂优势，不能将静态查询收益写成维护收益。

## NodeANN hnswlib 研究 runner（2026-10-29 起）

独立于 `repro.cpp` 的新后端，固定 hnswlib revision
`ca426729609b3221563047ceb269cc1213e0d376`（`deps/hnswlib` git submodule）。
所有原始 FP32 squared-L2 距离统一经 `src/raw_l2_space.h` 的 `RawL2Space`，
即 `hnswlib::L2Space(dim).get_dist_func()`；查询路径单线程，`--threads`
仅并行构建。构建：`./build_nodeann.sh` → `build/nodeann_research`。

```
./build/nodeann_research --data DIR --dim 128 --mode nodeann \
    --scan-threshold 32 --ef 64 --threads 8 --csv PREFIX
```

- `--mode exact`：整 scope shared-distance 扫描（GT/B3）；`nodeann --scan-threshold 0` 为 B0；`T>0` 为 B1（|L(v)|≤T 精确扫描，否则每节点 HNSW，M=16、efConstruction=200、seed=100）。
- 输入：`base.f32`/`test.f32`（LE f32 无头）、`tree.txt`（首行 N，其后 `id parent`）、`bindings.txt`（每行一个节点 id，第 i 行 = global id i）、`workload.txt`（首行 `nq 0`，其后 `qidx rec anchor`）。
- 输出：`PREFIX.perquery.csv`（25 列，含 scope/scan/hnsw/merge 计数与分阶段延迟，`hit_count=-1` 由驱动核对）与 `PREFIX.summary.json`（构建墙钟/CPU、峰值 RSS、序列化字节、延迟分位数）。
- ties 统一 `(distance, global_id)`；空 scope 输出十个 `-1`。
- smoke：`scripts/gen_smoke.py <dir>` 生成 2000×32/7 节点小样本，等价性校验脚本见 2026-10-29-exps。

## 动态三臂 runner（2026-10-29）

研究问题是维护策略的查询—更新成本交叉，**不是已证明的新索引胜出**。完整定义见 [dynamic-triad-protocol.md](../../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/dynamic-triad-protocol.md)。

```bash
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_dynamic_triad.py -v
codes/directory-aware-vector-attr-search/build/dynamic_triad \
  --data DIR --trace TRACE --arm A --ef 256 --scan-threshold 512 \
  --compact-threshold 0.2 --out PREFIX
```

- A：直接节点局部 HNSW + 小节点扫描；B：路径直接 posting + 全局 HNSW；C：路径祖先 posting + 全局 HNSW。B/C 属性移动不改图。
- `src/dynamic_triad.cpp` 同时实现三臂、独立 current-state 精确 GT 与计时；Python 只生成数据/trace、调度和绘图。所有原始距离仍统一 hnswlib；无 FAISS index、无 ANN 失败后的全局扫描兜底。
- 事件 `Q/V/T/M/B/R/D/I` 对应查询/标量/标签/单 ITEM 移动/批量 ITEM 移动/子树重挂/删除/插入。标签支持四种集合语义；属性组 AND/OR，目录始终 AND。
- 单线程提交可见性：查询看见之前全部成功更新。`inline`在查询计时后计算GT，但仍可能不对称热身原始向量；正式性能测量推荐下面的两遍GT协议。两种模式均不让oracle决定跳过搜索或搜索k，均不证明并发吞吐/MVCC。
- 结果 `.queries.csv` / `.updates.csv` / `.summary.json` 保留目录/ANN/GT、操作类型、ANN距离与跳数、压实、写操作、质量错误。HNSW字节真实计量，metadata单独标注估算，RSS包含输入与GT等进程开销。
- 语义回归覆盖往返移动、恢复旧标签、局部扫描↔图转换、批量移动、子树重挂、空结果及四种标签语义。状态不一致报错，低召回保留失败，不伪装同质量更快。
- 新增互斥计量 `{tombstone_compaction,threshold_build,threshold_drop}_{us,count,ann_insertions}`：三个分项精确划分原 `compaction_*` 总量，分别表示墓碑重建、扫描→图、图→扫描，不改变原触发策略。summary另给实际初始/结束HNSW图数及图内live/dead数。

### 完整原始 WIKI 子树的拓扑配对

`scripts/generate_wiki_topology_triad.py` 保留原始完整子树所有向量/目录与绑定；native与等节点数balanced树共享同一向量、直接占用、属性和字面trace，只有parent边不同。默认root410/3626、456个原始query vectors、Q/U0.01/0.1/1；这是完整子树cohort，不是全量WIKI。

```bash
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/scripts/generate_wiki_topology_triad.py
```

输出manifest沿用 `scripts/run_dynamic_triad.py` 调度。生成器拒绝覆盖历史生成目录；Python的Fenwick结构只用于生成查询锚点，不参与被测索引/搜索/GT或计时。完整协议见 [wiki-topology-protocol.md](../../researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-29-exps/wiki-topology-protocol.md)。

### 两遍GT协议：不在测量查询之间重算向量GT

`run_dynamic_triad.py --gt-mode precomputed` 先由同一C++执行器按完整事件流逐查询生成精确GT，再结束oracle阶段，运行三臂测量并传入 `--gt-in ORACLE.queries.csv`。GT不是静态初始状态，也不是ANN输出；driver绑定数据/trace/binary/tape/stamp SHA。测量仍独立核对当前scope元数据，GT只在搜索/merge计时后读取，不影响执行预算。

```bash
python codes/directory-aware-vector-attr-search/scripts/run_dynamic_triad.py \
  --manifest MANIFEST --gt-mode precomputed --ef 1024 --jobs 2 --repeats 3 \
  --tracked-root NEW_RESULTS --work-root IGNORED_WORK
```

- `--gt-mode inline` 是明确保留的诊断/GT生成模式（driver默认值）；旧inline时间只作诊断，不能因“GT不计时”便声称无缓存偏置。
- `oracles.json` 单列真实GT生成时间/距离调用；测量summary与每query `gt_distance_calls` 必须为0。测量中的 `gt_us` 是元数据核对与tape读取，不是向量GT生成成本。此协议不消除所有元数据/文件缓存干扰。
- Oracle本身ANN召回不足不影响其独立精确GT生成；真正测量的低召回点仍失败，不做精确兜底。篡改tape或stamp拒绝复用，成功oracle stamp复用时保持不变。
- 当前研究保留旧运行binary时，另编译 `build/dynamic_triad_gt_tape` 并用 `--binary` 指定；测试可用 `DYNAMIC_TRIAD_BINARY` 覆盖路径，不覆盖正在测量的旧执行器。新checkout正常构建默认binary即可支持两种模式。

## 子树闭包可恢复邻接（2026-10-30）

`src/closure_index.h` / `closure_order.h` 为新研究候选，不替换NodeANN。固定64候选池、几何见证掩码、当前子树闭包选边、可恢复邻接与动态Euler序列；已实现V/T/M/B/R/D/I及同步墓碑压实。属性不合格点可导航但不可作为合法见证。递归子树限制性质不覆盖内部owner的direct范围，也不是有限ef召回保证。

```bash
bash build_closure.sh
bash build_dynamic_triad.sh
python -m unittest discover -s tests -p test_closure.py -v
python scripts/run_closure.py \
  --dataset "wiki=$WIKI_DATA" --trace "wiki=$WIKI_TRACE" \
  --dataset "sift=$SIFT_DATA" --trace "sift=$SIFT_TRACE" \
  --out ../../work/closure/static
```

六臂为真实NodeANN A、无剪枝U、普通几何G、DFS-between规则R、子树闭包H、禁用属性见证恢复H0。默认ef64/256/1024校准、按qidx分组确认、串行轮换三次；C++预计算GT，各臂计时消费同一tape。离线全scope/邻接审计与性能运行分开，防止全池指纹扫描污染查询缓存。静态模式冻结初始属性，不执行源trace中的更新。

只在`<out>/<name>/result.json`的`dynamic_admitted=true`时，按相同输入与二进制运行 `--stage dynamic --static-results <out>`；自动回放动态前缀并生成独立R/B/M与强制压实维护包络。`--smoke`不能授权正式动态阶段。结果包含全部质量失败点、原始CSV、SHA、构建/更新/总成本、结构估算与峰值RSS。输出目录须为新目录。

完整算法、预测、失败归因、计时边界和可复制服务器指令见[closure-server-plan.md](../../researches/nested-attr-vector-search/2026-10-30-exps/closure-server-plan.md)。本地3项真实程序回归通过，不代表已赢WIKI/SIFT或新颖性已获确认。`neighborhood_filter.h`原样来自502753c，仅复用数据/谓词合同；其静态Qualifier不用于新索引动态状态。

### 历史复现：固定efSearch15/20，已完成并停止当前原型扩展

服务器5fc1be3已完成上述大预算static，未放行Dynamic。用户要求正常小查询预算，新入口保持原核心/候选池/停止规则及建图efConstruction200，只重跑A/U/R/H×查询ef15/20×三次；k10，复用原始200条Q和初始快照：

**2026-10-31状态：** 服务器2f8c4e0已完成下述48次小预算运行；A及同池U在四个全部非空点的时间、macro/pair均优于H，停止当前原型扩展。完整[收尾](../../researches/nested-attr-vector-search/2026-10-31-exps/report.md)。下列命令只作历史复现，不是新的自动任务；Dynamic/真实维护未测。

```bash
python scripts/analyze_closure_termination.py \
  --raw ../../work/closure/static-7654f1e-run1 \
  --out ../../work/closure/termination-7654f1e-run1.json
python scripts/run_closure_small_ef.py \
  --previous-raw ../../work/closure/static-7654f1e-run1 \
  --out ../../work/closure/small-ef-15-20-run1 --cpu 2
```

生产模式重新构建原程序，校验原始11份源码blob、数据/trace SHA，独立GT后串行轮换。保留所有Recall—time点，不按`.999`筛选或补高ef，不自动调用Dynamic。两数据集共48次计时、2次GT；旧qidx分组另列，不冒称新的独立测试集。

源码可确定E<ef时合法堆填不满，加上次序骨架会遍历全scope；工具只读旧CSV量化真实占比。该现象不是全部query归因，尤其WIKI漏召回query的E8511>1024。完整[服务器提示词](../../researches/nested-attr-vector-search/2026-10-30-exps/closure-small-ef-server-prompt.md)及[归因](../../researches/nested-attr-vector-search/2026-10-30-exps/closure-5fc1be3-review.md)。代码仅新增提交`e0cd9f6`可单独cherry-pick，不要求合并知识库文档。

