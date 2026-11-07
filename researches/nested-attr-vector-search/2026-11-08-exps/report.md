# 2026-11-08 实验记录：top-10 口径

## 目标

在 $k=10$ 口径下测量 ASI/DQS 与 NodeANN 的召回–吞吐关系。结果预算压缩会同时改变容量边界、表示误差的相对权重与每查询候选深度，因此参数网格按 $k$ 重设，而不是沿用其它 $k$ 的绝对值。

## 设置

- 128 维 SIFT，平方 L2；PQ($m=16$, 256 码字)还原后的 float32 用于建图和搜索，原始 FP32 用于真值。
- $k=10$。真值文件 [`data/gt_10.txt`](data/gt_10.txt) 为 200×10 个 int32。17 条查询的合格对象少于 10（最少 5），因此有 49 个 `-1` 填充，有效条目 1,951。召回分母取 $\min(k,|T_i|)$，填充不计入。
- 真值已用独立 FP32 全范围扫描核对：attribution 运行断言每条查询的 oracle top-10 与文件内容集合相同，200 条全部通过。该断言是必查项，不能仅凭文件生成方式假定真值正确。
- ASI：$M=16,efC=200,p=50,K_{cap}=8,ef_2=16$，$ef\in\{5,10,15,20,30,40,60,100,200,400\}$。查询结果上限为两通道 keep 并集，即 $ef+ef_2\ge10$ 才具备满容量。
- NodeANN：缓存的每属性 HNSW，$M=16,efC=200$，搜索宽度 $\max(ef,N)$。扫描 $N\in\{4,6,8,10,15,20,30,40,60\}$（$ef=10$）与耦合 $ef=N\in\{2,4,6,8,10,15,20,30\}$，另加 $N=10$ 时 $ef\in\{20,40,80\}$。
- Pre-filter 对 PQ 还原向量做范围内精确扫描，作为表示参照。
- CPU 2 单线程；两侧共用 `RawL2Space`，禁用编译器自动向量化与 hnswlib 手写向量路径。主曲线每点 3 个独立进程、每进程 3 批×200 查询；QPS 为总查询数除以总时间。构建、GT 计分、oracle 扫描与 trace 写盘不计时。

## 结果

| 方法/配置 | Recall@10 | QPS | 距离调用/查询 |
|---|---:|---:|---:|
| ASI $ef=5$ | 0.528447 | 30,164.7 | 196.4 |
| ASI $ef=10$ | 0.811379 | 29,230.6 | 222.2 |
| ASI $ef=15$ | 0.820092 | 26,309.8 | 243.0 |
| ASI $ef=20$ | 0.824193 | 23,234.9 | 259.1 |
| ASI $ef=30$ | 0.826756 | 20,987.0 | 285.0 |
| ASI $ef=40$ | 0.828806 | 17,700.8 | 308.5 |
| ASI $ef=60$ | 0.831369 | 16,607.2 | 342.5 |
| ASI $ef=100$ | 0.833931 | 13,805.4 | 389.1 |
| ASI $ef=200$ | 0.835982 | 9,956.0 | 464.5 |
| ASI $ef=400$ | 0.839057 | 7,430.2 | 564.5 |
| NodeANN $ef=N=10$ | 0.838032 | 22,822.5 | 258.2 |
| NodeANN $ef=15,N=15$ | 0.841107 | 20,448.8 | 298.2 |
| NodeANN $ef=N=20$ | 0.842645 | 17,504.5 | 329.4 |
| NodeANN $ef=10,N=40$（beam 40） | 0.843157 | 12,513.8 | 422.1 |
| Pre-filter exact reference | 0.843157 | 11,019 | 1,140.2 |

固定 $N=10$ 的 $ef=20,40,80$ 配置只增加搜索宽度；固定 $ef=10$ 时，$N>10$ 也会通过 $\max(ef,N)$ 增加搜索宽度。以下单列每属性预算 $N<10$ 的点：它们可能截断单个属性中的真近邻，多个属性归并后仍可能返回 10 个结果，不能将该标记等同于总输出不足。

| NodeANN（容量 < 10） | Recall@10 | QPS | 距离调用 |
|---|---:|---:|---:|
| $ef=N=2$ | 0.291133 | 39,094.6 | 170.0 |
| $ef=N=4$ | 0.496668 | 30,489.8 | 199.3 |
| $ef=N=6$ | 0.652486 | 27,630.6 | 221.6 |
| $ef=N=8$ | 0.759610 | 25,729.8 | 241.6 |
| $ef=10,N=8$ | 0.762686 | 22,660.7 | 258.2 |

### 消融（$ef=40,ef_2=16,p=50,M=16,efC=200,K_{cap}=8$，每点 1 进程 3 遍）

| 变量 | 取值 | Recall@10 | QPS | 距离调用/查询 |
|---|---:|---:|---:|---:|
| $ef_2$ | 8 | 0.827268 | 19,387.5 | 297.7 |
| $ef_2$ | 16 | 0.828806 | 19,362.5 | 308.5 |
| $ef_2$ | 32 | 0.833419 | 15,572.6 | 327.8 |
| $ef_2$ | 64 | 0.833419 | 16,168.0 | 356.0 |
| $p$ | 10 | 0.828806 | 15,679.5 | 313.1 |
| $p$ | 25 | 0.829831 | 18,972.6 | 310.5 |
| $p$ | 50 | 0.828806 | 18,047.8 | 308.5 |
| $p$ | 100 | 0.828806 | 17,195.9 | 303.4 |

$ef_2$ 从 16 增至 32 时，召回从 0.828806 增至 0.833419，QPS 从 19,362.5 降至 15,572.6；从 32 增至 64 没有召回增长。$p$ 的四档召回最大差异为 0.001025，吞吐非单调。以上均为单进程测量，不足以证明稳定趋势；`--ef2` 被截断至至少 1，因此这些消融没有关闭 $q_2$。

## 表示参照与预算边界

PQ 还原向量的精确 top-10 与原始 FP32 真值有 1,645/1,951 的 ID 重合，即 0.843157；这是排序重合参照，不是近似算法的数学上限。按既定归因优先级，未命中项中 PQ 排序位移占 $ef=10$ 的 76.9%、$ef=40$ 的 88.3%、$ef=200$ 的 93.8%；“属性内未导航到”分别占 22.8%、11.7%、6.2%。该分类不是量化与导航的因果分解。

固定 $ef=10$ 扫描 $N$ 时，$N=10$ 的召回为 0.838032，$N=40$ 为 0.843157；增加 $N$ 同时抬高有效搜索宽度。ASI 最小 $ef=5$ 与 $ef_2=16$ 的 keep 并集上界为 21，这个上界不保证实际返回条数。结果表不把这些条件称作精确搜索或可证明的召回保证。

## 局限

- ASI 侧全部是全闭包多种子、双通道原型，不是 root-only。
- $ef_2$ 与 $p$ 消融各一个进程，不提供跨种子区间；消融基线为 $ef=40$。
- 未报告邻接槽位扫描数，距离调用不能替代槽位成本。
- 工作负载只覆盖单属性查询前缀；不推广到多属性 OR 与其它选择率。
- 顶端结论依赖 SIFT 与 CCS 结构这一绑定，不代表语义向量数据。

## 下一步

在保持 $k=10$ 的前提下补多属性 OR 与不同闭包大小；用更大属性基数的工作负载检验交叉点是否随属性规模右移。参数、数据与程序哈希见 `data/scheme-protocol.json`，图见 `figures/`。

## 补充：SVG 兼容性与既有 top-10 trace 再分析

本补充没有新建索引、运行新导航算法或测量 FP32 rerank。主曲线及其计时数据保持原样。

- 九张 SVG 的可见文字统一导出为字形路径，消除显示时的字体/CSS 文字依赖。四张结构图原来的 83 个文字节点仍保存在 SVG metadata 中，可用 `svg_labels.py --extract-source` 恢复。已运行 `python3 codes/dag-ann-search/scripts/illustrate_asi.py render`，逐图检查没有可见 `<text>`、嵌入位图或外部引用，并检查实际图像预览；Pareto 仍只有 NodeANN 与 Ours (ASI)。具体计数和生成依赖版本见 [`data/figure-validation.json`](data/figure-validation.json)。用户阅读器的具体失效原因尚未复现，字体/CSS 兼容性原因仍属 `[INFERENCE]`。
- 对 `work/asi-evidence/trace-k10-ef-10-40-200.jsonl` 的 600 条已有记录重新计算全部 FP32 GT 的候选覆盖、PQ exact GT 的发现与返回、种子评分重复次数。源 trace SHA256 为 `60fc42400e7540991f5fb0bf8e1e8f3bc7c1a53e831669edacdd04b20a7640a4`。新增汇总 [`candidate-coverage.json`](data/candidate-coverage.json) 与逐查询数据 [`candidate-coverage-per-query.jsonl`](data/candidate-coverage-per-query.jsonl)；结果、解释和候选机制见 [`approaches.md` §12](../approaches.md#12-在当前框架内提高效率与召回)。
- 候选覆盖不是当前程序的返回 recall，也不包含重排时间。当前缺少 NodeANN 同口径候选 trace，没有证据声称 FP32 rerank 后 ASI 更快。下一轮优先级以 §12.5 为准，新机制尚未通过完整新颖性与理论门槛。

可从 §11 的 trace 命令重新生成输入，再用以下命令复核核心集合计数；它不重新运行搜索：

```bash
python3 - <<'PY'
import json
from collections import Counter, defaultdict
from pathlib import Path
counts = defaultdict(Counter)
for line in Path("work/asi-evidence/trace-k10-ef-10-40-200.jsonl").open():
    t = json.loads(line)
    F, P, R, V, S = (set(t[k]) for k in
                      ("fp_top", "pq_top", "returned", "evaluated", "seed_evaluated"))
    counts[t["ef"]].update(gt=len(F), returned=len(F & R), offered=len(F & V),
        unseen=len(F - (V | S)), found_not_returned=len((F & V) - R),
        pq_offered=len(P & V), pq_returned=len(P & R),
        duplicate_calls=len(t["evaluated"]) + len(t["seed_evaluated"]) - len(V | S))
for ef, c in sorted(counts.items()):
    assert c["returned"] + c["found_not_returned"] + c["unseen"] == c["gt"] == 1951
    assert c["pq_offered"] == c["pq_returned"]
    print(ef, dict(c))
PY
```

## 输出保持组：重复展开与种子重复评分

### 实现与设置

活跃 `attr_skeleton_bench.cpp` 新增独立的展开代号和种子评分缓存，仅在 pop 后跳过重复邻接扫描；未改变双通道准入、严格距离比较、首次接触、种子顺序或最终排序。缓存不会把未选中的代表点标为已访问。每进程复用的新增数组元素载荷为 $12n$ 字节，当前约 1.93 MiB，不是完整 RSS。

原逻辑先加入与修改后相同的距离、展开、槽位计数，保存对照快照。两侧均使用本记录原有的 scalar 编译选项、g++ 15.2.0、CPU 2；本组每点三个独立进程，每进程 100 轮 × 200 查询，依次 before/after、after/before、before/after。QPS 由查询计时聚合，构建、GT 计分、参考扫描和 trace 写盘排除；不能用进程 `wall_s` 代替。

### 已运行检查与结果

- 带计数的原逻辑逐项复现 600 条历史 trace；新增检查先在原逻辑观察到重复距离、重复展开，再在修改后通过。
- 1000 条真实查询/配置记录覆盖 $ef=5,10,40,200,400$。输出 ID、候选评分请求、准入、种子请求顺序、参考结果与资格状态逐项相同。修改后的展开序列等于原序列首次出现的顺序；实际距离调用等于两类评分请求并集中的不同对象数，缓存命中与减少次数相符。
- 同距向量 fixture 有 36 个对象、共享子属性的多父 DAG、重复属性 OR、空范围与重复代表点。$ef_2=1,16$ 共四次进程、30 对查询/配置检查通过，三轮聚合 recall 相同。仅首轮非空范围会写 trace，不声称有后续轮次逐条输出。
- 同插桩条件下，$ef=10$ 聚合 QPS 从 28,995.5 到 31,005.7（+6.93%），距离减少 7.68%，邻接槽位减少 16.16%；recall 保持 0.811379。$ef=400$ 聚合 QPS 反降 1.01%。完整五档及逐对波动见 [`approaches.md` §12.6](../approaches.md#126-输出保持组的实测结果)，不作所有工作点加速的结论。

### 数据与复核边界

- [`work-removal-protocol.json`](data/work-removal-protocol.json)：原提交、仅插桩的补丁、前后编译与诊断命令、源码/二进制/数据哈希、fixture 输入规格与四次输出、计数定义。
- [`work-removal-per-query.jsonl`](data/work-removal-per-query.jsonl)：1000 对主工作负载计数与语义比较。
- [`work-removal-fixture-traces.jsonl`](data/work-removal-fixture-traces.jsonl)：两档 $ef_2$、前后版本的 60 条首轮原始记录。
- [`work-removal-measurements.jsonl`](data/work-removal-measurements.jsonl) 与 [`work-removal-summary.json`](data/work-removal-summary.json)：六个计时进程、30 个配置结果、聚合值、每对变化和范围。

WSL 时钟和 CPU 独占性未控制，三对不构成跨构建种子置信区间。修改保持召回，不能填补与 NodeANN 的高召回差距；未运行新的导航机制或 FP32 rerank。旧主曲线和原始记录保留，不与本组 QPS 混接；DQS 流程图与活跃实现说明已同步。
