# 新索引／算法研究：固定 autoresearch 基准

## 阶段与研究边界

本轮仅搭建并运行评测harness，尚未实现候选优化。研究对象是 **hierarchy-constrained single-query top-k vector search**，不是几何vector join；后续若转为join需另立输入输出与指标。

起点是[完整WIKI两类基线](../report.md)：A为全局ANN+目录scope ID过滤，B为直接绑定节点上的局部ANN，各节点top-k后合并。基准从B的默认全HNSW开始，A和纯Flat保留作外部对照，不重复构建耗时约4,962秒的全局HNSW。

[TrieHI v1](https://arxiv.org/abs/2606.16903)已有公开预印本。正式venue尚未核实，**不意味着可不引用或只改名复用**。可借鉴DSQ/DSM定义、稳定节点身份、目录重挂及祖先聚合，但这些本身不是新方案的贡献。既有[新颖性审计](../../../alphaxiv-novelty-audit.md)对D0/D1/D3/D5停止独立立项、D2/D4暂停的决定不因建立harness而撤销。

两个特别需要纠正的方案表述：
- 局部精确top-k合并仍为全局精确top-k，是基线性质，不足独立宣称召回证书创新。
- 查询到质心的距离**不是**子树点距离下界。若使用球界，至少需半径等保守包络，例如L2下`max(0, ||q-c||-r)`；它又是已有metric-tree分支定界思想，仍需证明新的独立机制，不能只换目录场景。

后续研究需先给出最接近前序逐机制差异，再实现可独立消融的新增布局/索引/搜索机制。单纯换Flat、缓存、参数调优、组合已有组件或更换名称只能作为对照或工程改进，不当作已确认创新。当前未进行新的全网文献检索，不作新颖性认证。

## 固定测量合同

入口：仓库根目录 `bash autoresearch.sh`。无网络，无时间依赖工作负载，OMP/OpenBLAS/MKL/BLIS全部固定1；FAISS HNSW构造器默认固定rng(12345)，约束workload seed=20261026。

- 完整WIKI：1,941,679×1024；固定456查询（227 recursive、229 non-recursive，后者41个空scope）。
- base/test/tree/bindings/workload均对照已存SHA-256；不允许子采样、改约束、替换向量或按query id特判。
- 距离为保真原始float32 squared L2，top-10；query锚点是合成约束，不冒称发布方真实约束。
- 独立GT：`data/autoresearch_reference.json`由NumPy枚举scope并精确扫描生成，不调用C++ GT；每个query保存top-10 ID。NumPy按float32距离、global ID升序处理tie。2349个GT pairs。
- C++ per-query CSV追加`result_ids`，评估器独立核查实际返回ID的合法性、去重、scope资格与GT命中，不只相信候选程序自报的recall；输出发生在计时外。
- 每次调用重新构建C++，然后**串行执行3个独立进程**；每进程重建局部索引、运行同一workload一次。进程退出码或任何质量/资源门槛失败，则本次harness失败，不输出成功METRIC。
- 允许正常计时噪声；确定性指输入、顺序、种子与配置，而非墙钟数值逐次相同。

主指标：`recursive_e2e_ms`，3次运行中“227条递归查询平均e2e”的中位数，越低越好。e2e包含候选节点解析、搜索与合并，排除GT、审计、加载和CSV输出，沿用当前C++边界；禁止通过将查询工作移出计时来提速。

质量/资源硬门槛（**每次重复均需满足**）：

| 门槛 | 值 |
|---|---:|
| recursive query-average recall（仅非空） | ≥0.999 |
| recursive global pair recall | ≥0.999 |
| non-recursive macro及global recall | =1 |
| non-recursive mean e2e | ≤0.025 ms |
| 单进程peak RSS（含base及评估用内部对象） | ≤24 GiB |
| 索引构建计时（当前executor计时边界） | ≤180 s |

副指标：`recursive_p95_ms`、两个recursive recall、`nonrecursive_e2e_ms`、`nonrecursive_macro_recall`、`build_s`、`peak_rss_mib`、`recursive_qps`、`recursive_e2e_mad_ms`。召回取三次最小值，RSS取三次最大值，其余时延/构建取中位数；p95是query分布分位数，不是运行间置信区间。构建计时边界沿用当前实现，不冒称完整系统建库时间。

## 文件与运行产物

- 可迭代实现：`codes/directory-aware-vector-attr-search/src/`，必要时`build.sh`。
- 固定评估器：`scripts/autoresearch_benchmark.py`及根`autoresearch.sh`，会话内不得修改指标、门槛、计时边界或重复次数。
- 原始数据、冻结GT、既有论文证据与历史基线均为只读。既有PE-Online/Offline/TrieHI与局部HNSW/Flat对照不得删除。
- 运行日志、逐查询CSV、GNU time及`summary.json`写入已忽略的`production/nested-attr-vector-search/work/autoresearch_trie/run-*/`；只有报告和小体积汇总入库，不提交大数据或索引。
- 独立GT仅在setup通过`.../autoresearch_benchmark.py --prepare-reference`生成；优化循环不得重生成GT。

本轮实测结果与错误路径检查追加在当日报告的新节，既有全量基线记录不覆盖。
