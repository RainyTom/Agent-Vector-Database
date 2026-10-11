# 远程实验交接

## 状态与边界

这是论文派生实现的修复版，不是完整复现认证。当前可运行 SimJoin、
kJoinSlide 和另一篇 Fast AVJ 的独立入口；所有 HNSW 使用同一 hnswlib，
搜索核心均为 C++，Python 只负责参数、输入记录和结果输出。
JoinSlide 与 kJoinSlide 已分源文件，没有新增总控运行脚本。

VBase、XJoin、FGF-Hilbert、FreshDiskANN、全局维护基线及经核验的 NSG/Vamana
构图仍未完成；外部图接口不能代替这些方法的实现。不要用 Fast AVJ 或
independent 冒充 VBase。完整实验矩阵和未确认项见
[复现方案](simjoin-reproduction-plan.md)。

## 在另一台服务器构建

从仓库根目录执行。以下 32 GB 上限仅为远程环境示例，必须按该服务器
资源修改；**本服务器禁止提高默认 2,000,000,000 字节限制**。

```bash
git fetch origin
git switch --track origin/fix/reproduction-audit
cmake -S codes/vector-join-reproduction -B codes/vector-join-reproduction/build \
  -DCMAKE_BUILD_TYPE=Release -DVJ_FETCH_HNSWLIB=ON -DVJ_FETCH_PYBIND11=ON \
  -DVJ_MEMORY_LIMIT_BYTES=32000000000
cmake --build codes/vector-join-reproduction/build -j2
ctest --test-dir codes/vector-join-reproduction/build --output-on-failure
export PYTHONPATH="$PWD/codes/vector-join-reproduction/build"
```

建议使用干净工作树构建：结果记录的是 CMake 配置时的源码提交和 dirty 状态。
更换提交后重新配置并构建。所有运行入口都会设置 Linux RLIMIT_AS 硬上限，
更严格的继承上限不会被提高；该限制是地址空间而非物理 RSS。

## 第一轮：验证输入与质量

兼容数据为无文件头、连续行主序 float32，显式指定维数；fvecs/bvecs/带头部
格式不能直接改扩展名使用。输入必须有限且与距离语义相符。不要自动归一化：
如果自行归一化或重划分，记录原始来源、转换过程和划分种子。不限数据集名称。
下面路径、维数、种子、w、M、ef 均是用户需替换的示例，**不是论文参数**。

```bash
python3 codes/vector-join-reproduction/scripts/run_simjoin.py \
  --base /data/Y.f32 --queries /data/X.f32 --dim 128 \
  --epsilon 0.4 --w 64 --order window-order-sort \
  --graph-source hnswlib --graph-m 20 --ef-construction 200 --seed 1234 \
  --dataset-name user-data --preprocessing none \
  --split-description 'user-provided X/Y; record source and split seed here' \
  --base-limit 10000 --query-limit 1000 --ground-truth-all --repeat 5 \
  --output experiment-results/pilot/simjoin-sort.jsonl

python3 codes/vector-join-reproduction/scripts/run_kjoinslide.py \
  --base /data/Y.f32 --queries /data/X.f32 --dim 128 \
  --k 10 --w 64 --order window-order-sort \
  --graph-source hnswlib --graph-m 20 --ef-construction 200 --seed 1234 \
  --dataset-name user-data --preprocessing none \
  --split-description 'same X/Y as epsilon pilot' \
  --base-limit 10000 --query-limit 1000 --ground-truth-all --repeat 5 \
  --output experiment-results/pilot/kjoin-sort.jsonl
```

首轮规模是本项目选择的诊断规模，不是论文规模。先返回这两份 JSONL 再决定
完整规模，避免直接计算昂贵的全量暴力真值。需要逐点复核时增加
`--pairs-output`，但高选择率输出可能很大。内存不足应保留失败日志，不应
静默缩小规模并将其标成完整实验。

顺序消融分别改 `--order base-mst`、`window-order-sort`、
`window-order-epsilon`，最后一种必须显式提供 `--order-search-w 32` 等自选值。
该宽度和 JoinSlide maxT 搜索加 top-1 回退属于实现选择，尚非作者核验设定。
每种配置使用不同输出文件。Table 3 的在线预处理重复测量使用
`--order-repeat 100`；不要把这一重复数当成所有实验的通用规定。
自连接用 `--self-join` 并省略 `--queries`，令 query-limit 为 0，base-limit
决定同一个集合；对角线不会被擅自删除。普通连接即使 X/Y 路径相同仍按
独立限制加载，不能因此自动变成自连接。

## 参数扫描与公平性

按复现方案的逐项矩阵改变 epsilon、w、k、规模及图类型；其余输入哈希、
划分、精度、构图参数、线程和计时口径保持一致。五次重复使用同一数据和
构图种子，衡量运行波动，不冒充五次独立随机划分。目标召回率 0.99 需要实际
扫描并报告达到该目标的配置；不可修改结果或直接认定某个 w 已达标。

外部图需要逐节点邻接文件、入口 ID、不可变源码提交、完整构图参数和构图日志。
使用 `--graph-source external --ef-construction 0`，其余参数见 `--help`。
即使标签写 NSG，也只作为用户声明而非已核验的 NSG。本文默认 NSG 与当前
HNSW 后端的差异必须一直保留在报告中。

Fast AVJ 各自的 run 文件通过 `--help` 提供命令行参数；其 threshold 是
**平方 L2** 且严格小于，SimJoin epsilon 是 L2 且小于等于。两篇论文的边界
语义不同，不能直接把相同数值传入并比较结果。旧 hard-window-cache 不是 SimJoin。

## 结果解释与返还

- `total_online_ms` 是在线顺序时间加搜索时间；索引、离线 MST、IO 和真值
  计算分别排除。`join_ms` 单独使用会漏算在线顺序。
- NDC 分搜索、在线顺序、离线顺序记录；Fast AVJ 另记 OOD 距离计算。
- 平均 recall/precision 按查询宏平均；空真值/空返回集合约定见 JSON。
  禁用真值时为 null；前缀抽样明确标为 prefix-sample，不是全量指标。
- `paper_formula_nselectivity` 来自完整真值对数/|Y|，不是近似返回对数；
  另存真值平均每查询结果数，保留论文公式与文字解释的差异。
- `process_lifetime_peak_rss_bytes` 是进程生命周期峰值，后续重复可能继承
  前次峰值。要测独立峰值，请每次启动新进程、使用 `--repeat 1` 和新输出名。
  `estimated_peak_bytes` 仅为规划估算，不是实测 RSS。

请返还原始 JSONL、构建及测试日志、失败 stderr、外部图构建日志（若有），
以及数据来源/预处理说明。无需把大型数据提交 GitHub。JSONL 已记录输入
SHA256、完整参数、硬件、源码和依赖版本；不要仅返回截图或平均数。
汇总时按相同配置分组，分别计算各次指标的均值和波动，不把不同 w 混合平均。
这批数据用于下一轮核验，尚不能宣称论文性能一致。
