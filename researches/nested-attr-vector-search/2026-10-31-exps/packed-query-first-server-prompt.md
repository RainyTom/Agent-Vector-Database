# 最新服务器任务：只验证查询效率，复用已有数据/GT/驻留索引

**执行源码提交：`d4add37`，基于`cdc28f3`；两者缺哪个就先接入哪个。** 本地12项CLI回归及36pass复用烟测完成，见[本地报告](packed-query-first-local-report.md)。

## 0. 立即切换目标

用户最新要求：远端太慢，先取得查询效率超过基线；增删改先理论分析即可。本提示词**替代**packed-server-prompt中的114次诊断、100k四单元、维护包络和1M门控。

不要继续排队maintenance、envelope、final repack、并发、新数据生成或扩规模。已有结果保留；当前程序若正在写结果，保留完整日志与已落盘文件，再停止调度后续任务。不要删除索引、输入、GT或历史失败点，也不要并发跑多个计时任务。

本轮不再问用户是否继续实现/是否先交付。核心复用能力和驱动已经提供；只需集成与执行。

## 1. 先读与方案边界

读同目录 `packed-query-first-plan.md`：只测纯A、P-dfs和P-dfs-aware，固定ef20/k10/B4096，3遍。旧H继续停止。算法就是已实现的整目录DFS共享图，不新设计过滤/导航机制。

源码由本次研究提交接入（最终聊天附提交号），基于cdc28f3的P实现；更新 `src/dynamic_triad.cpp`，新增 `scripts/run_packed_query.py` 和静态batch行为回归。已有源码不要整分支覆盖；按提交cherry-pick，冲突只做明确接入，不改A搜索算法或P图布局。

`--static-batch`在每进程仅构建一次图：A三遍，P六遍(cap/hybrid交替)。重复查询实际执行，不缓存答案。两进程共2次构建、9遍查询。P两策略共用同一张图。旧进程已退出且未保存的图无法恢复，不能声称直接加载了那张图；本次只保证新session中不重复建。

## 2. 构建与最小行为验证

从代码目录 `codes/directory-aware-vector-attr-search` 执行：

```bash
bash build_dynamic_triad.sh
bash build_closure.sh
python -m unittest discover -s tests -p 'test_static_batch.py' -v
```

若服务器已验证相同源码的依赖，不重做所有旧维护用例。实际构建失败/新batch状态错误必须先修，保留失败记录，不将其当索引性能负结果。

## 3. 优先复用已完成准备的SIFT单元

先看本机已生成的 `work/packed/static-*`，选一个有data、static/events.trace、oracle.queries.csv的SIFT单元。**优先复用当前已在做的单元，不新建一套。** 有100k就用100k；只有旧12000也先用旧单元，不等1M数据。若有多个同规模单元，优先细目录random，不能看性能后择优选数据；记录选择理由。

### A. 已有run_packed静态目录：直接复用GT

例如已有 `work/packed/static-sift100k-l2048-random-run1`：

```bash
python scripts/run_packed_query.py \
  --from-static ../../work/packed/static-sift100k-l2048-random-run1 \
  --name sift100k-l2048-random \
  --out ../../work/packed/query-first-sift100k-run1 --cpu 2
```

路径、name、CPU按已有文件替换。可用已落盘GT但静态大矩阵尚未结束的目录；程序核对输入/trace/GT SHA后复用，不需要等待33/57个配置完成。若原调度仍在写同目录，先停止其继续计时，避免资源争用。

### B. 只有已生成data和原trace

```bash
python scripts/run_packed_query.py \
  --dataset sift=../../actual/existing/data \
  --trace sift=../../actual/existing/mixed.trace \
  --out ../../work/packed/query-first-existing-sift-run1 --cpu 2
```

只替换真实既有路径；不运行prepare_packed_sift。程序抽取已有Q、生成一次独立GT，然后2次建图9遍查询。不要在性能运行使用`--smoke`；它只用于本地合成输入验证，不是加速真实实验的开关。

### 已有成果复用原则

- 复用数据、属性快照、同一静态Q和验证过的GT；不能把更新后的快照与初始GT混用。
- 不把旧计时混入新session的三遍平均，不从不同机器/CPU混拼加速比。
- 当前二进制没有从旧CSV恢复图的能力，已释放图只能首次重建；不得伪称磁盘索引已恢复。
- 输出新目录，旧文件只读。输入SHA不一致要找出原因，不绕过检查。

## 4. 本轮唯一实验矩阵

| 变体 | 图 | 策略 | efSearch | 重复 |
|---|---|---|---:|---:|
| A-graph | 原纯NodeANN | off、scan0 | 20 | 3 |
| P-dfs | 既有DFS共享图B4096 | cap | 20 | 3 |
| P-dfs-aware | **同一P图** | hybrid、E≤512扫描 | 20 | 3 |

M16、efConstruction200、k10不改。暂不跑node/shuffle/Exact、ef15、其他B或维护。不要凭比较结果继续追加实验。

建图只2次，summary需显示static_index_reused；所有查询真实执行。9遍属于相同索引的驻留重复，不是9次独立建图，也不是不同臂交错轮换；报告warm/resident、按臂成组的计时限制。峰值RSS是session high-water辅助数字，不要求候选内存更优。

## 5. 成功判定：先取得可用效率点

主要表格只看非空查询，给每臂macro/pair、均值/p50/p95/p99、相对A耗时比；空GT单列，独立qidx数和scope/E范围写清楚。

- 双方同时达到.99或.95的层分别展示；质量层以实际macro和pair同时判断，不能只看macro。
- **共同质量层上，候选平均e2e<A即满足本轮方向目标**；不再设置必须20%加速、必须更省内存或必须维护实测获胜的额外门槛。三遍结果同时披露，不隐藏波动或较差一遍。
- 候选Recall更低且未达到共同层：时延只能作诊断，不说同质量加速；不自动提高ef或改算法追正。
- aware胜、cap不胜：如实说“包含小集合执行策略的组合方案胜出”，也算本轮可用效率点；独立层级贡献暂未确认，之后再做归因。
- 两候选都不胜：立即交真实差距、资格时间、距离/graph_calls归因，本轮结束，不自发扩规模/追参。

共同层是目标质量层，不表示两方实际Recall完全相等；表中保留实际差异。旧较大N门槛、full repack摊销或更新吞吐不作为本轮放行条件。

## 6. 更新仅交理论边界，不再跑成本包络

现有代码保留全部操作，本轮不删除、不重新实现：

- V/T：固定宽度bitplane更新，不重建几何图。
- M：两个direct posting维护，物理loc不变，不重插向量。
- B：必须枚举m个受影响对象，做m次元数据移动，不能说O(1)。
- R：不搬向量，但当前实现重算rank为O(D)，还有scope计数，不谎称O(logD)。
- I/D：正常局部HNSW插入/墓碑，触发时局部压实/split；重建成本显式记G(b)，当前D成员查找O(b)。
- 全repack：可能昂贵，本轮未测，不填0，不宣称所有更新都已证明低延迟。

理论可行性结论只到“属性/归属更新与几何维护解耦，插删能局部处理”；不需要拿到所有操作实测才能验证查询。但论文最终若要宣称动态性能优势，后续仍需补证，此轮不做。

## 7. 交付格式

先给：是否在共同质量层超过A、最快候选及耗时比、实际Recall。接着给3臂查询表、三遍波动、空GT/源query数量、资格与搜索计数、一次build与辅助RSS。

保存report、命令、manifest/summary/points小JSON/CSV及raw inventory；源SHA、输入SHA、GT SHA完整。大raw仍留work。注明未运行维护/扩规模，没有把它们当失败；更新理论分析引用新plan即可。

提交fetch后推送当前服务器实验分支，不强推。不要向用户再问“是否继续maintenance或实现”，也不自动恢复旧完整矩阵。
