# 交接文档：语义时空 Episode 检索

更新：2026-10-25。分支 `autoresearch/researches-nested-attr-vector-search-agent-datas-20261020`，最新提交 `e668cda`。

本文是给下一位研究者/代理的入口，按“结论 → 问题 → 方法 → 数字 → 产物 → 复现 → 坑 → 下一步”组织。详细推导见链接文档，不复述。

## 0. 一页结论

**问题**：轨迹由坐标、时间戳、段向量集组成；查询是一段语义序列 + 相对时间偏移 + 绝对时间窗 + 空间范围 + 时长容差；要在每条轨迹中找最佳连续 episode，再全局 top-k。

**方法**：`Episode Catalog`（物化 48 h 内全部 span，6.21M 条）→ `Episode Span Tree`（对 `(start_ts, dur)` 建静态 k-d 树，节点带 bbox 并集）→ 64 维投影上的分段 DP 排序 → top-C 的 128 维精确验证。可选：PQ 加速排序；有界 slack `n ≤ m+G`。

**最好数字**（6 城、1.84M 签到、500 查询、1 年窗、50 km、potion 128 维）：

| 方法 | Recall@10 | mean ms | p95 ms |
|---|---:|---:|---:|
| 精确空间先行（全扫描） | 1.000 | 15.3 | 37.5 |
| 平坦桶 catalog | 0.989 | 10.5 | 23.6 |
| **EST（G=∞）** | 0.989 | **5.66** | **15.0** |
| **EST + G=2** | 0.989 | **4.68** | **11.4** |
| EST + G=0 | 0.984 | 3.57 | 9.0 |

EST 与全量扫描在 500/500 查询上**硬约束结果逐条一致**（可行 11,673.3、验证 293.5、Recall 0.9894）。

**可辩护**：多段 episode 的联合跨度访问路径（时间/时长/bbox/段数可精确索引），是检索式查重未发现的同形工作。
**不可辩护**：带宽引理（episode matching/SPRING 已有）、下界剪枝（成本≈验证）、单纯物化、ANN/PQ 叠加。
**最大风险**：审稿人把它归约为 “PVLDB 2020 / TRSSQ + 硬过滤 + 物化”。必须有选择性区间内的等召回对比与无误判论证。

## 1. 研究问题（v4）

规范定义：`researches/semantic-spatiotemporal-hybrid-search/problem-definition.md`。

- 轨迹 $T=(X,\tau,E,\beta)$：坐标、时间戳、段向量集、点→段映射；分段契约 C1–C4（连续、覆盖非空、查询无关、编码固定）。
- Episode：连续段区间 $[u,v]$，原始点 $X(E)=\bigcup X_k$，时间 $[t^s_u,t^e_v]$。
- 查询：$Q=(\mathbf q_{1..m};\tau_{1..m};W;R;\delta_\Delta;k)$，无坐标。
- 可行：窗包含、时长容差、$X(E)\subseteq R$、段数 $\ge m$。
- 对齐：步长仅 $(1,1)/(0,1)$，**禁止 $(1,0)$**；分数 $D/m$。
- 结果：每轨最佳 episode，全局 top-k，排除查询来源用户。

## 2. 当前方法

### 2.1 Episode Catalog

- 物化所有 $u\le v$、时长 $\le 48$ h 的 span：**6,213,067 条，298.2 MB，构建 0.77–0.83 s**。
- 字段：`traj,u,v,n,dur,ts,bbox[4],maxnorm`（48 B/条）。
- 旧版用四个哈希桶（时长、时间、空间×月、时长桶）；已被 EST 取代。

### 2.2 Episode Span Tree（EST）

- 对 span 的 $(t^s,\Delta)$ 建静态 k-d 树，叶大小 16 可配，**节点 16.8 MB，构建 +2.8 s**。
- 节点存 `ts_min/max`、`dur_min/max`、`count`、`bbox_union[4]`。
- 查询矩形 $[w_s,\ w_e-\Delta+\delta]\times[\Delta-\delta,\Delta+\delta]$；节点矩形不相交或 bbox 并不相交则剪枝。
- 不变量 I1 覆盖、I2 剪枝安全 ⇒ 硬约束不漏解。
- 实测：访问 9,633 节点（3.7%）、4,471 叶（3.4%）；叶过取 211,968 条，但净收益 1.86×。

### 2.3 排序与验证

- 排序：128 维段向量做固定随机投影到 64 维，在投影上跑“连续分段 DP”，取 top-C。
- 验证：原始 128 维带宽 DTW（$j\in[u+i-1,v-m+i]$），得到精确分数；按轨迹去重取 top-k。
- 可选 PQ：8 B/段、8 次查表/距离，排序 1.47×，召回 0.9966。

### 2.4 有界 slack

定义允许 $n-m$ 任意大；把 $n\le m+G$ 作为显式参数：$G=2$ 召回不变、4.68 ms；$G=0$ 仍 0.984、3.57 ms。说明最优 episode 绝大多数 $n=m$。

## 3. 实测数字

### 3.1 主 workload（potion、1 年、50 km）

| 方法 | Recall | mean ms | p95 ms | 备注 |
|---|---:|---:|---:|---|
| E1 精确（时间先行） | 1.000 | 25.2 | 50.2 | |
| B2 精确（空间先行） | 1.000 | 15.3 | 37.5 | 11,612 次 DTW |
| B3 全局 ANN 后过滤 | 0.313 | 3.2 | 10.5 | 失败模式 |
| B4 时间桶 ANN（topn 300/600/1000） | 0.681/0.794/0.902 | 17.6/31.7/49.7 | — | 预算越高越慢 |
| 平坦 catalog（旧） | 0.989 | 10.5 | 23.6 | |
| EST（G=∞） | 0.989 | 5.66 | 15.0 | |
| EST + G=2 | 0.989 | 4.68 | 11.4 | |
| EST + PQ（平坦版） | 0.997 | 7.73 | 16.9 | 未与 EST 合并 |

### 3.2 选择性扫描

| workload | 精确 | 空间先行 | 平坦 catalog |
|---|---:|---:|---:|
| 90 天 + 50 km | 16.3 | 9.0 | 5.7 (0.994) |
| 1 年 + 10 km | 28.6 | 15.7 | 5.4 (0.995) |
| 1 年 + 无空间 | 83.6 | 192.6 | 45.8 (0.991) |

EST 只在 potion/50 km 上测过；其他档位需要补测。

### 3.3 下界成本（决定性否定）

NY、10 查询、90,743 可行 episode：LB2 中位 0.75，按真实第 10 名阈值可剪 97.7%，只验证 2.3% 即精确；但 LB2 计算成本 = 完整 DTW 的 53.2%，带宽精确 DTW = 53.9%——**净收益≈0**。LB1 因单位范数完全失效。

## 4. 关键结论（含否定结果）

1. 精确扫描很强；统一 ANN 后过滤在本问题失效（Recall 0.23–0.31）。
2. 下界剪枝不是杠杆：成本由距离计算支配，DP 只占小头。
3. 旧 pilot embedding 有 bug（Frobenius 归一化、36.6% 名称碰撞）；换 potion 后性能格局不变——瓶颈不是语义质量。
4. 带宽引理不是新意（episode matching/SPRING）；只能作为实现观察。
5. EST 真实有效：硬约束结果与全扫描逐条一致，1.86×；$G=2$ 再省 19%。
6. 检索式查重未发现“多段 episode 联合跨度索引 + 每轨最佳 + top-k”的同形工作；但这是**未发现**，不是证明。

## 5. 产物地图

### 文档

| 文件 | 内容 |
|---|---|
| `problem-definition.md` | v4 形式定义（唯一规范） |
| `problem-comparison.md` | 与 ETQ/TISIS*/TRSSQ 等的逐条对照 |
| `novelty-audit.md` | 查重结论（含 2026-10-25 子代理表格） |
| `prior-work.md` | 前序工作与证据边界 |
| `baseline.md` | 标准基线 + 评测契约 |
| `approach.md` | 方法：catalog、EST、带宽、PQ、有界 slack |
| `datasets.md` | 数据集与 5 个 workload 变体 |
| `2026-10-22-exps/` | 首轮 ANN 实验（旧口径，勿与新结果混表） |
| `2026-10-23-exps/` | 问题对照纠正、TRSSQ 入库 |
| `2026-10-24-exps/` | 基线（E1/B2/B3/B4）、GT、产物布局 |
| `2026-10-25-exps/` | 查重、下界、规模、potion、PQ、EST |

### 代码

| 文件 | 说明 |
|---|---|
| `src/prep.py` | 旧 pilot workload 生成（有 bug，勿再用） |
| `src/prep_potion.py` | potion 128 维 workload 生成（当前主用） |
| `src/bench.cpp` | 模式 `gt/space/ann-post/ann-bucket/catalog` |
| `src/bench_est.cpp` | 追加 `catalog-est --leaf --max-slack` |
| `src/bench_pq.cpp` | 追加 `catalog-pq`（PQ-ADC 排序） |
| `src/pq_train.py` | PQ 训练与编码 |
| `src/scan.cpp`, `src/ann.cpp` | 早期基线（10-22） |
| `work/bench`, `work/bench_est`, `work/bench_pq` | 已编译二进制（`work/` 被 gitignore） |

### 数据

- 原始 CSV：`datasets/massive-steps/*_checkins.csv`（6 城，gitignored）。
- workload：`work/all6_8760h_s50_potion`（主）、`all6_8760h_s50`（旧 pilot）、`all6_2160h_s50`、`all6_8760h_s10`、`all6_8760h`（gitignored，需重建）。
- potion 模型：`production/semantic-mobility-search/models/potion-base-8M`（gitignored）；venv：`production/semantic-mobility-search/venv_potion`（gitignored）。
- 结果：`researches/.../2026-10-24-exps/data/`、`2026-10-25-exps/data/{scale,lb,potion,pq,est}/`。

### 关键提交

```
e668cda index: add episode span tree and bounded-slack study
6f7154a research: record PQ ranking and selectivity evidence
f6e7962 research: rebuild workload with potion embeddings and record branches
502daf4 research: record novelty, bound-cost and selectivity findings
a442f14 bench: prototype episode catalog and symbolic ranking
e07e87d docs: base the method on an episode catalog index
212b9b8 bench: add episode benchmark and standard baselines
```

## 6. 复现

```bash
# 主 workload（potion），需要先建 venv 与下载模型
python3 -m venv production/semantic-mobility-search/venv_potion
production/semantic-mobility-search/venv_potion/bin/pip install model2vec numpy
# 模型放到 production/semantic-mobility-search/models/potion-base-8M
python3 production/semantic-mobility-search/src/prep_potion.py --out work/all6_8760h_s50_potion ...

# 编译
g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib \
  production/semantic-mobility-search/src/bench.cpp -o production/semantic-mobility-search/work/bench
g++ ... src/bench_est.cpp -o work/bench_est

# 跑
B=production/semantic-mobility-search/work/bench
W=production/semantic-mobility-search/work/all6_8760h_s50_potion
$B --dir $W --mode gt --out $O
$B --dir $W --mode space --out $O
$B --dir $W --mode catalog --topc 300 --cap-hours 48 --out $O
work/bench_est --dir $W --mode catalog-est --leaf 16 --max-slack 2 --topc 300 --out $O
```

完整命令见 `2026-10-24-exps/experiments/build.md` 与 `2026-10-25-exps/data/*/…report.md`。

## 7. 已知问题与坑

1. **未提交文件**：`bench_pq.cpp`、`pq_train.py` 在交接时仍未入库；本次一并提交。
2. **`main` 分叉未解决**：`origin/main` 有 4 个提交不在本分支，`git merge-tree` 在 `papers/changelog.md`、`papers/papers.jsonl`、`containment-filtered-vector-search/index.md` 三处冲突。按约定未强推、未合并，需人工决定 merge/rebase。
3. **旧 pilot embedding 有 bug**：`prep.py` 的 `l2()` 对矩阵用 Frobenius 范数；36.6% 名称哈希碰撞。旧 workload 只作“弱语义”对照，不与 potion 结果混表。
4. **换 embedding 必须重算 GT**：potion GT 与 pilot GT 的 top-10 Jaccard 仅 0.24。
5. **二进制 qps 字段口径不一致**：`bench` 的 `SUMMARY qps` 是 `1000/mean_us`，`summary.csv` 用 `1e6/mean_us`；引用时注意。
6. **EST 未覆盖全部 workload**：目前只有 1 年/50 km 的 potion 结果；90 天、10 km、无空间需补测。
7. **$G=2$ 只在单一 workload 验证**：不能直接写成通用结论。
8. **精确模式不快于扫描**：EST/catalog 的优势来自“近似排序 + 精确验证”；若要求严格 top-k 全验证，收益消失。
9. **全局 bbox 护栏**：平坦 catalog 在无空间约束时会枚举全世界网格（已加 `space_global` 保护）；EST 不受影响。
10. **work/、models/、venv/ 均 gitignored**：新环境需重建（potion workload 约 1.2 GB）。

## 8. 下一步（按优先级）

**P0 — 决定论文定位**
- 做“选择性代价模型 + 等召回防御”：对比 `scan`、`flat catalog`、`EST`、`ANN post-filter`，并对照 PVLDB 2020 式后过滤与 TRSSQ 式 filter→verify。
- 需要回答：3× 是否足够；若目标是顶会，是否接受“首个联合跨度索引 + 系统刻画”的定位。

**P0 — 扩大证据**
- 在 90 天/50 km、1 年/10 km、1 年/无空间三档跑 EST 与 $G$ 扫描，确认 $G=2$ 的召回不降是否普遍。
- 补 `EST + PQ` 的合并实验（当前 PQ 只在平坦 catalog 上测过）。

**P1 — 方法补强**
- 用范围树/PST 替换 k-d 树，验证节点访问数能否再降。
- 精确模式：若采用有界 slack，带宽上界为 $G+1$，可把“精确验证全部可行 span”的成本压到可接受，从而给出真正的精确索引叙事。
- 代价模型：由选择性估计候选规模，决定 scan / flat / EST / ANN。

**P2 — 写作**
- 论文骨架：问题 → 前序差异 → 跨度代数与不变量 → EST → 代价模型 → 实验（选择性扫描 + 等召回）。
- 明确 negative results：LB 无效、post-filter 失效、embedding 不是瓶颈。

## 9. 交接提示

- 先读三份：`problem-definition.md`（问题）、`approach.md`（方法）、`novelty-audit.md`（边界）。
- 再读 `2026-10-25-exps/report.md` 与 `data/est/sweep.csv`（最新结论）。
- 任何“首次/最优”表述先查 `novelty-audit.md` 的 2026-10-25 表；不要重复已被证伪的主张。
- 不要在没有重算 GT 的情况下跨 embedding 比较。
- `main` 的分叉与冲突留给用户决定；不要强推。
