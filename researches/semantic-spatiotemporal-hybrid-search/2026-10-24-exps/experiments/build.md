# 构建与运行记录

日期：2026-10-24。对应 workload `all6_8760h_s50`，产物见 `../data/`。

## 环境

| 项 | 值 |
|---|---|
| CPU | 10 vCPU |
| 内存 | 11 GB |
| 编译器 | g++ 15.2.0 |
| 编译选项 | `-O3 -march=native -std=c++17 -pthread` |
| 依赖 | `deps/hnswlib`（header-only） |
| 线程 | 构建 8 线程；查询单线程 |

## 构建

```bash
g++ -O3 -march=native -std=c++17 -pthread -I deps/hnswlib \
  production/semantic-mobility-search/src/bench.cpp \
  -o production/semantic-mobility-search/work/bench
```

## 数据准备

```bash
python3 production/semantic-mobility-search/src/prep.py \
  --out production/semantic-mobility-search/work/all6_8760h_s50 \
  --queries 500 --window-hours 8760 --spatial-km 50 \
  --csv datasets/massive-steps/ny_checkins.csv --city ny \
  --csv datasets/massive-steps/istanbul_checkins.csv --city istanbul \
  --csv datasets/massive-steps/jakarta_checkins.csv --city jakarta \
  --csv datasets/massive-steps/petaling_jaya_checkins.csv --city petaling_jaya \
  --csv datasets/massive-steps/sao_paulo_checkins.csv --city sao_paulo \
  --csv datasets/massive-steps/moscow_checkins.csv --city moscow
```

产出：1,838,793 点、63,084 用户、500 查询、128 维向量。

## 运行

```bash
B=production/semantic-mobility-search/work/bench
W=production/semantic-mobility-search/work/all6_8760h_s50
O=production/semantic-mobility-search/work/bench_out

$B --dir $W --mode dump --out $O
$B --dir $W --mode gt   --out $O
$B --dir $W --mode space --out $O

$B --dir $W --mode ann-post   --out $O --topn 300  --cand 1250 --ef 200 --threads 8
$B --dir $W --mode ann-post   --out $O --topn 600  --cand 2500 --ef 400 --threads 8
$B --dir $W --mode ann-bucket --out $O --topn 300  --cand 1250 --ef 200 --threads 8
$B --dir $W --mode ann-bucket --out $O --topn 600  --cand 2500 --ef 400 --threads 8
$B --dir $W --mode ann-bucket --out $O --topn 1000 --cand 5000 --ef 600 --threads 8
```

## 构建耗时（HNSW）

| 运行 | 索引 | build_s |
|---|---|---:|
| ann-post topn300 | 全局单索引 | 50.7 |
| ann-post topn600 | 全局单索引 | 97.0 |
| ann-bucket topn300 | 72 个月桶 | 40.6 |
| ann-bucket topn600 | 72 个月桶 | 76.5 |
| ann-bucket topn1000 | 72 个月桶 | 115.9 |

## 产物对应

| 文件 | 来源 |
|---|---|
| `../data/queries.csv` | `--mode dump` |
| `../data/groundtruth.csv` | `--mode gt`（同时给出 E1 计时） |
| `../data/results_e1_time_first.csv` | `--mode gt` |
| `../data/results_b2_space_first.csv` | `--mode space` |
| `../data/results_b3_postfilter_topn300.csv` | ann-post 300 |
| `../data/results_b3_postfilter_topn600.csv` | ann-post 600 |
| `../data/results_b4_timebucket_topn300.csv` | ann-bucket 300 |
| `../data/results_b4_timebucket_topn1000.csv` | ann-bucket 1000 |
| `../data/summary.csv` | 汇总 |

**缺失项**：ann-bucket topn600 的逐查询 CSV 在随后一次 topn1000 运行中被覆盖，只保留了 stdout 汇总（已写入 `summary.csv`，并标注来源）。

## 备注

- 本记录中 E1 与 groundtruth 是同一次运行，二者结果必然一致；E1 的价值是精确方法的延迟参照。
- B2 与 E1 都是精确方法（Recall=1.0），区别只在候选轨迹的生成顺序。
- B3/B4 的候选生成近似，验证精确；召回相对 groundtruth 计算。
