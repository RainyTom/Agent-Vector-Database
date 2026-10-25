# 项目移交手册：语义时空 Episode 检索（planner–executor）

更新：2026-10-25。仓库根目录。

## 0. 角色与阅读顺序

本仓库将迁移到实验服务器。服务器上部署一个 AI 模型作为 **planner**（决定方向、拆任务、验收）；另一个 AI 模型作为 **executor**（执行命令、改代码、跑实验、回报）。本文件定义项目状态与交接信息；协作协议见 [coordination/README.md](coordination/README.md)。

**必读顺序**：

1. 本文件（项目状态、环境、约定）；
2. [coordination/README.md](coordination/README.md)（任务/结果格式与边界）；
3. [AGENTS.md](AGENTS.md)（研究助理长期规则，仍然有效）；
4. `researches/semantic-spatiotemporal-hybrid-search/HANDOFF.md`（研究细节）；
5. `researches/semantic-spatiotemporal-hybrid-search/problem-definition.md`（唯一规范定义）。

## 1. 项目一句话与状态

**研究问题**：轨迹由坐标、时间戳、段向量集组成；查询是语义序列 + 相对时间偏移 + 绝对时间窗 + 空间范围 + 时长容差；在每条轨迹中找最佳连续 episode，再返回全局 top-k。

**当前方法**：物化 48 h 内全部 span（6.21M 条）→ 对 `(start_ts, dur)` 建静态 k-d 树（EST）→ 64 维投影上的分段 DP 排序 → top-C 的 128 维精确验证。

**当前最好结果**（6 城、1.84M 签到、500 查询、1 年窗、50 km、potion 128 维）：

| 方法 | Recall@10 | mean ms | p95 ms |
|---|---:|---:|---:|
| 精确空间先行（全扫描） | 1.000 | 15.3 | 37.5 |
| 平坦桶 catalog | 0.989 | 10.5 | 23.6 |
| EST（$G=\infty$） | 0.989 | 5.66 | 15.0 |
| **EST + $G=2$** | **0.989** | **4.68** | **11.4** |

EST 与全量扫描在 500/500 查询上硬约束结果逐条一致（可行 11,673.3、验证 293.5）。

**当前阶段**：方法已验证，进入“新颖性定位 + 取舍”阶段。核心未决问题：3× 的加速是否足以支撑一篇索引论文，还是应转向“何时物化 episode”的系统性刻画或聚合谓词/流式维护等新问题。

## 2. 研究结论摘要（含否定结果）

可辩护：

- 多段 episode 的**联合跨度访问路径**（时间/时长/bbox/段数可精确索引），检索式查重未发现同形工作；
- 物化 span + EST 的**硬约束不漏解**，且与全量扫描逐条一致。

不可辩护（已证伪或前序已有）：

- 带宽引理 $j\in[u+i-1,v-m+i]$ 是 episode matching/SPRING 的已有区间；
- 下界剪枝：成本 53.2% vs 带宽 DTW 53.9%，净收益≈0；
- ANN 后过滤：Recall 0.23–0.31；
- “换更强 embedding 会改变格局”：potion 重建后结论不变；
- 单纯物化、PQ/ANN 叠加。

详细证据见 `researches/semantic-spatiotemporal-hybrid-search/novelty-audit.md` 与 `2026-10-25-exps/`。

## 3. 仓库地图

```text
.
├── AGENTS.md                     # 研究规则（必须遵守）
├── PROJECT_HANDOFF.md            # 本文件
├── coordination/                 # planner/executor 协议、状态、任务与结果
├── papers/                       # 论文库（29 篇、27 条关系）
│   ├── papers.jsonl / relations.jsonl / changelog.md
│   ├── vector-join/ filtered-ann/ prefix-vector-search/
│   ├── similarity-join/ trajectory-search/
│   └── README.md
├── researches/
│   ├── semantic-spatiotemporal-hybrid-search/   # 当前主线
│   │   ├── problem-definition.md    # v4 定义（唯一规范）
│   │   ├── approach.md              # 完整方法规范
│   │   ├── baseline.md              # 标准基线 + 评测契约
│   │   ├── novelty-audit.md         # 查重与边界
│   │   ├── point-vs-span.md         # 点级存在 vs 片段语义
│   │   ├── open-problems.md         # 候选新研究问题
│   │   ├── HANDOFF.md               # 方向级交接
│   │   ├── 2026-10-2x-exps/         # 每日实验记录与数据
│   │   └── evidence/                # 候选/受限证据
│   ├── vector-join/                  # 早期主线（已暂停）
│   ├── containment-filtered-vector-search/
│   └── nested-attr-vector-search/
├── production/semantic-mobility-search/
│   ├── src/                      # prep、bench、bench_est、bench_pq、pq_train
│   ├── work/                     # 大文件（gitignored）
│   ├── models/                   # potion 模型（gitignored）
│   └── venv_potion/              # Python venv（gitignored）
├── datasets/                     # 原始 CSV（gitignored）
├── deps/                         # 第三方源码（gitignored）
├── codes/                        # 早期复现代码
├── paper-production/             # 论文写作工程（未用于本主线）
└── academic-paper-writer/
```

**Git 状态**：`main` 与远程同步于 `9fc9a15`；当前工作分支同名。`datasets/`、`deps/`、`work/`、`models/`、`venv_potion/` 均不入库（见 `.gitignore`）。

## 4. 环境与迁移

### 4.1 当前机器

| 项 | 值 |
|---|---|
| CPU | 10 vCPU |
| 内存 | 11 GB（potion prep 峰值 4.7 GB；EST 查询峰值约 2 GB） |
| 磁盘 | `/tmp` 为 5.9 GB tmpfs，注意不要放大文件 |
| 编译器 | g++ 15.2.0 |
| Python | 3.14.4（系统）+ `venv_potion`（model2vec） |
| 关键依赖 | hnswlib（header-only）、numpy、model2vec |

### 4.2 服务器最低要求

| 项 | 建议 |
|---|---|
| 内存 | ≥16 GB（重建 workload 与目录时峰值 5 GB，留余量） |
| 磁盘 | ≥20 GB（datasets 3.8 GB + workload 1.2–5 GB + deps 0.4 GB + venv/model 0.2 GB + repo 0.2 GB） |
| CPU | 支持 AVX2；构建用 8 线程 |
| 编译器 | g++ ≥ 11，C++17 |
| Python | ≥3.10（`prep_potion.py` 只用 numpy/model2vec） |
| 网络 | PyPI、HuggingFace model 仓库可达；数据集可离线拷贝 |

### 4.3 需要从旧机器带走的东西

| 内容 | 大小 | 是否在 git |
|---|---:|---|
| `datasets/massive-steps/*.csv` | 3.8 GB | 否 |
| `production/semantic-mobility-search/work/`（可选） | ~5 GB | 否 |
| `production/semantic-mobility-search/models/potion-base-8M` | 31 MB | 否 |
| `production/semantic-mobility-search/venv_potion` | 134 MB | 否 |
| `deps/hnswlib` | 414 MB（全部 deps） | 否 |

workload 可以重建（prep 约 46 s）；模型与 venv 建议直接拷贝，避免重复下载。

### 4.4 迁移步骤

```bash
# 1) 代码
git clone <repo> vector-database && cd vector-database
git checkout main        # 9fc9a15

# 2) 数据集（或从旧机器 rsync）
curl -sL 'https://huggingface.co/datasets/CRUISEResearchGroup/Massive-STEPS-New-York/resolve/main/new_york_checkins.csv' \
  -o datasets/massive-steps/ny_checkins.csv
# 其余五城同理，见 researches/semantic-spatiotemporal-hybrid-search/datasets.md

# 3) 第三方依赖
git clone --depth 1 https://github.com/nmslib/hnswlib.git deps/hnswlib
# 版本记录见 deps/manifest.tsv

# 4) Python 环境与模型
python3 -m venv production/semantic-mobility-search/venv_potion
production/semantic-mobility-search/venv_potion/bin/pip install model2vec numpy
mkdir -p production/semantic-mobility-search/models/potion-base-8M
curl -L 'https://huggingface.co/minishlab/potion-base-8M/resolve/main/model.safetensors' \
  -o production/semantic-mobility-search/models/potion-base-8M/model.safetensors
curl -L 'https://huggingface.co/minishlab/potion-base-8M/resolve/main/tokenizer.json' \
  -o production/semantic-mobility-search/models/potion-base-8M/tokenizer.json

# 5) 重建 workload（约 46 s，峰值 4.7 GB）
production/semantic-mobility-search/venv_potion/bin/python \
  production/semantic-mobility-search/src/prep_potion.py \
  --out production/semantic-mobility-search/work/all6_8760h_s50_potion ...

# 6) 编译
g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib \
  production/semantic-mobility-search/src/bench.cpp \
  -o production/semantic-mobility-search/work/bench
g++ ... src/bench_est.cpp -o work/bench_est
g++ ... src/bench_pq.cpp   -o work/bench_pq
```

`prep_potion.py` 的完整参数见 `2026-10-25-exps/data/potion/potion-report.md`。

## 5. 数据与产物约定

### 5.1 workload

| ID | 时间窗 | 空间 | 用途 |
|---|---|---:|---|
| `all6_8760h_s50_potion` | 1 年 | 50 km | **主实验**（potion 128 维） |
| `all6_8760h_s50` | 1 年 | 50 km | 旧 pilot（弱语义对照，勿混表） |
| `all6_2160h_s50` | 90 天 | 50 km | 窗口敏感性 |
| `all6_8760h_s10` | 1 年 | 10 km | 空间敏感性 |
| `all6_8760h` | 1 年 | 无 | 无空间约束 |

### 5.2 文件格式

- `points.bin`：`int32 N, int32 D=128`，随后 N 条 `int32 user, int64 ts, float lat, float lon, int32 cat, float emb[128]`；
- `users.bin`：`int32 U`，随后 U 对 `int32 off, int32 len`；
- `queries.bin`：`int32 Q, int32 D, int32 MAXM`，随后每条 `m,source,ws,we,dur,tol,bbox[4],tau[m],emb[m][128]`；
- 结果 CSV 的 schema 见 `2026-10-24-exps/experiments/layout.md`。

### 5.3 结果与指标

- 主指标：轨迹级 Recall@10（相对精确 oracle，排除来源用户）；p50/p95；候选数；DTW 次数；构建时间；索引字节。
- **不得混表**：不同 embedding 的 GT 不同（pilot vs potion top-10 Jaccard 仅 0.24）。
- **qps 口径**：`bench` 的 `SUMMARY qps` 是 `1000/mean_us`；`summary.csv` 用 `1e6/mean_us`。引用时注明。

## 6. 复现与冒烟测试

首次登录服务器后，先做最小验证（不要直接跑全量）：

```bash
B=production/semantic-mobility-search/work/bench
W=production/semantic-mobility-search/work/all6_8760h_s50_potion
$B --dir $W --mode selftest --limit 30        # banded DP 与全 DP 对拍
$B --dir $W --mode gt --out /tmp/smoke --limit 5
$B --dir $W --mode space --out /tmp/smoke --limit 5
```

期望：`selftest bad=0`；GT 与 space 的 top-10 用户集合一致；再跑 catalog/EST 对比 0.989 召回。

## 7. Planner–Executor 协议

见 [coordination/README.md](coordination/README.md)。摘要：

- Planner 写 `coordination/tasks/T-####.json`，目标单一、验收可观察；
- Executor 领取任务，执行，写 `coordination/results/T-####.json`，更新 `coordination/state.json`，在 `coordination/log.md` 追加一行；
- 同一时间只允许一个“写实验/改代码”的任务，除非任务声明 `parallel_ok`；
- 结果必须包含确切命令、指标、产物路径、失败原因；
- 阻塞时返回 `blocked` + `missing`，不要猜；
- 破坏性操作（删除数据、强推、重置分支）需 planner 或用户批准。

## 8. 当前决策点

1. **新颖性定位**：3× 加速 + 首个联合跨度索引，是否足够；还是转为“何时物化 episode”的系统性刻画（`open-problems.md` P10/P9）。
2. **$G=2$ 的普适性**：目前只在 1 年/50 km 验证，需在 90 天、10 km、无空间三档复核。
3. **精确 vs 近似**：0.989 召回是否可接受；是否需要“带误差证书”的版本（P5）。
4. **是否把 PQ 并入 EST**：目前 PQ 只在平坦 catalog 上测过（0.997@7.73 ms）。
5. **下一个研究问题**：P1 聚合属性谓词、P2 流式维护、P3 episode join 三选一（或并行）。

## 9. 路线图

**P0（迁移后第一周）**

- 环境冒烟 + 复现主表（exact/catalog/EST/EST+G2）；
- 在三档 workload 上补 EST 与 $G$ 扫描；
- 写“选择性代价模型”：scan vs flat catalog vs EST vs ANN 的决策边界。

**P1（方法补强）**

- 聚合属性谓词（P1）：EST 节点加 `present/universal` bitset，稀有属性前缀计数；
- PQ 与 EST 合并；
- 精确模式：利用有界 slack 的带宽上界，给出可接受的精确叙事。

**P2（新问题）**

- 流式 span 索引维护（P2）或 episode join（P3）；
- benchmark（P9）作为低风险产出。

## 10. 风险与坑

1. **embedding 历史**：`prep.py` 的 `l2()` 对矩阵用 Frobenius 范数，类别通道失效、36.6% 名称碰撞。只用 `prep_potion.py`；旧结果只作弱语义对照。
2. **GT 依赖 embedding**：换编码器必须重算 GT（约 15–20 s），否则召回无意义。
3. **workload 体积**：potion workload 1.2 GB；`/tmp` 是 tmpfs，勿把大文件写进去。
4. **内存峰值**：prep 4.7 GB；同时跑两个 6 城进程会 OOM。
5. **qps 口径**：见 §5.3。
6. **EST 未覆盖全部 workload**：90 天/10 km/无空间只有平坦 catalog 结果。
7. **精确模式不快于扫描**：优势来自近似排序 + 精确验证。
8. **不要强推**：`main` 已同步；历史分叉已通过合并解决（`9fc9a15`），不要重写历史。
9. **数据许可**：Massive-STEPS 标注 Apache-2.0；上游 Semantic Trails 的再分发条件仍需在投稿前确认。potion 模型许可未核实（使用前确认）。
10. **论文库规则**：新增论文先查重、按主题目录、更新 `papers.jsonl` 与 `changelog.md`；不要凭标题合并版本。

## 11. 交接检查清单

- [ ] 服务器能 clone 仓库并 checkout `main`（`9fc9a15`）
- [ ] 六城 CSV 到位，`datasets/` 路径正确
- [ ] `deps/hnswlib` 重建，`bench`/`bench_est`/`bench_pq` 编译通过
- [ ] `venv_potion` 可用，`potion-base-8M` 本地加载成功
- [ ] `prep_potion.py` 重建 `all6_8760h_s50_potion`，查询集与旧文件逐字段一致
- [ ] `selftest bad=0`；5 查询 GT/space 对拍一致
- [ ] 复现主表：15.3 / 10.5 / 5.66 / 4.68 ms，召回 0.989
- [ ] `coordination/state.json` 与最新结果一致
- [ ] planner 已读 `coordination/README.md`，能写出符合 schema 的任务
- [ ] 用户确认新颖性定位与下一个研究问题
