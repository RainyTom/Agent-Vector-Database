# 基准与产物布局

更新日期：2026-10-24。本文件固定 benchmark、ground truth 与结果文件的保存形态，后续所有实验沿用同一布局。

## 1. 工作负载

| 项 | 值 |
|---|---|
| workload id | `all6_8760h_s50` |
| 数据 | Massive-STEPS 6 城（NY, Istanbul, Jakarta, Petaling Jaya, São Paulo, Moscow） |
| 原始行 | 2,097,813；完整行 1,838,793 |
| 轨迹 | 63,084 条用户级轨迹 |
| 语义段 | 每次签到一段，$s$=签到数 |
| 向量 | 128 维：64 维类别 PPMI+SVD，64 维 venue name 哈希 |
| 查询 | 500 条，$m\in[3,6]$，同一天采样 |
| 时间窗 | 8760 h（1 年） |
| 空间范围 | 查询 footprint 外扩 50 km 的 bbox |
| 时长容差 | $\max(30\text{min},25\%)$ |
| $k$ | 10 |
| 排除 | 查询来源用户不计入结果 |

查询与数据生成命令见 §5。

## 2. 目录布局

```text
production/semantic-mobility-search/
  src/{prep.py, bench.cpp}          # 生成与评测
  work/all6_8760h_s50/              # 大文件，gitignored
    points.bin users.bin queries.bin queries_cat.bin
    user_city.bin cities.txt meta.json

researches/semantic-spatiotemporal-hybrid-search/2026-10-24-exps/
  data/
    queries.csv                     # benchmark 定义（可提交）
    groundtruth.csv                 # 精确 top-k（可提交）
    results_<method>.csv            # 每查询结果（可提交）
    summary.csv                     # 方法汇总（可提交）
  experiments/
    layout.md                       # 本文件
    build.md                        # 编译与运行命令、环境
  report.md                         # 当日结论与局限
```

大文件（points.bin、queries.bin、索引）不入库；入库文件均为小体积 CSV。

## 3. 文件格式

### 3.1 `queries.csv`

| 列 | 含义 |
|---|---|
| `qid` | 查询编号，与 groundtruth/results 对齐 |
| `source` | 采样来源用户，评测时排除 |
| `m` | 语义段数 |
| `ws`, `we` | 绝对时间窗（epoch 秒） |
| `dur_s` | 查询时长（秒） |
| `tol_s` | 时长容差（秒） |
| `min_lat`, `max_lat`, `min_lon`, `max_lon` | 空间 bbox |

### 3.2 `groundtruth.csv`

| 列 | 含义 |
|---|---|
| `qid`, `rank` | 查询与名次（1..k） |
| `user` | 轨迹（用户）id |
| `score` | $D/m$，DTW 步长仅 $(1,1)$ 与 $(0,1)$ |
| `a`, `b` | 最佳 episode 在用户轨迹内的起止下标 |
| `start_ts`, `end_ts` | 对应绝对时间 |

### 3.3 `results_<method>.csv`

| 列 | 含义 |
|---|---|
| `qid` | 查询 |
| `time_us` | 单查询耗时（微秒） |
| `cand_users` | 进入验证的候选轨迹数 |
| `verified` | 实际验证数（当前等于 cand） |
| `dtw` | DTW 调用次数 |
| `hits` | 返回结果数 |
| `recall` | 相对 groundtruth 的轨迹级 Recall@10 |

### 3.4 `summary.csv`

`method,queries,recall,mean_us,p95_us,qps,mean_cand,mean_dtw,empty`

## 4. 方法编号

| 编号 | 含义 | 模式 |
|---|---|---|
| E1 | 时间先行，精确 | `--mode gt`（同时产出 groundtruth） |
| B2 | 空间先行（网格），精确 | `--mode space` |
| B3 | 向量先行（全局 HNSW），后过滤 | `--mode ann-post` |
| B4 | 时间桶先行（按月分区 HNSW），后过滤 | `--mode ann-bucket` |

所有方法共用同一验证函数与同一 $D/m$；B3/B4 的候选生成可近似，验证必须精确。

## 5. 复现命令

```bash
# 1) 准备数据（一次性，约 940 MB）
python3 production/semantic-mobility-search/src/prep.py \
  --out production/semantic-mobility-search/work/all6_8760h_s50 \
  --queries 500 --window-hours 8760 --spatial-km 50 \
  --csv datasets/massive-steps/ny_checkins.csv --city ny \
  --csv datasets/massive-steps/istanbul_checkins.csv --city istanbul \
  --csv datasets/massive-steps/jakarta_checkins.csv --city jakarta \
  --csv datasets/massive-steps/petaling_jaya_checkins.csv --city petaling_jaya \
  --csv datasets/massive-steps/sao_paulo_checkins.csv --city sao_paulo \
  --csv datasets/massive-steps/moscow_checkins.csv --city moscow

# 2) 编译
g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib \
  production/semantic-mobility-search/src/bench.cpp \
  -o production/semantic-mobility-search/work/bench

# 3) 运行
B=production/semantic-mobility-search/work/bench
W=production/semantic-mobility-search/work/all6_8760h_s50
O=production/semantic-mobility-search/work/bench_out
$B --dir $W --mode dump --out $O
$B --dir $W --mode gt --out $O
$B --dir $W --mode space --out $O
$B --dir $W --mode ann-post --out $O --topn 300 --cand 1250 --ef 200 --threads 8
$B --dir $W --mode ann-bucket --out $O --topn 300 --cand 1250 --ef 200 --threads 8
```

## 6. 环境

- CPU：10 vCPU；内存 11 GB；单线程查询，构建用 8 线程。
- 查询延迟为单线程、热缓存、单次运行；多次重复时取中位数。
- HNSW 构建参数：$M=16$，`efConstruction=200`，查询 `ef=max(topn,64)`。

## 7. 已知边界

- embedding 是可复现的 PPMI-SVD + 名称哈希，不是神经文本编码器；绝对召回不可外推到生产 embedding。
- 查询从数据中采样，存在同分布乐观偏差。
- 1 年窗与 50 km 是压力测试参数，不是领域真实窗口。
- 时间戳按 naive UTC 处理，未处理夏令时。
