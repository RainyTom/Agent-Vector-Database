# 谓词可寻址二跳邻域：首轮冻结执行协议

依据2456849（不合并另一研究分支的论文库/历史），执行next-neighborhood-plan.md。WQR不参与本轮构建/生命周期，不扩大proxy，不回到几何界。本文保存于首次C++构建/实测前。

## 原型合同

- 同一原始FP32 squared-L2与hnswlib ca426729609b3221563047ceb269cc1213e0d376，M16/efConstruction200/seed100。
- 单一共享基础图；E2(v)=union(N(w),w∈N(v))去掉v，稳定外部对象ID去重。
- 256-ID页；<=16 offset用uint8数组，更多为256-bit bitmap；排序page目录，不加学习、分区或其他压缩。
- F_page=Live & Scope & Extra；当前parent拓扑、字面闭区间、启用字段AND/OR、tags四模式，和dynamic_triad语义一致。每query页/对象/owner缓存只在本query内有效。
- B逐边二跳→中心级epoch去重→ID排序→逐点缓存资格；不强迫B先生成scope bitmap或全页。
- C预建同集合页→按需F_page交集→ID排序候选。基础一跳边先照常处理，包括不合格导航点；二跳只注入合法对象，B/C共用visited/heap/停止内核。
- 初建后B/C图SHA与每query结果/距离/展开中心序列核对。E1在相同B搜索中心上重放，两臂集合逐中心比较。不因非空结果未到k而补扫、重试或读GT。

## 数据/查询冻结

WIKI用原root3626 native（11869×1024，151目录），SIFT用crossover sift-n12000-r1000-q64-d3-s20261029（13000潜在ID，初始12000live）。不重新生成属性；原数据SHA、source trace SHA、选中Q行和qidx split落盘。

分别从原qu1.txt、sift-d3-uniform-mixed-r0p1-qu1.txt按时间顺序取前200条Q定义，忽略其中更新仅构造初始快照的静态实验。保留原qidx/谓词，不沿用动态状态GT；当前初始状态公共核精确GT重新生成。按sorted unique qidx的rank奇偶分半：偶数calibration，奇数confirmation，同向量ID不得跨组；源query已被历史实验看过，不能称独立新采样，只是本轮按向量ID隔离确认。

scope fraction<=5%为narrow，>=25%为wide，其余middle；strong AND为启用普通字段>=2且combine0。保留实际eligible数/选择率，缺的单元如实报告，不按收益挑选。

## E1候选取得

同一冻结图、相同query、同一普通二跳搜索路径前8个展开中心（若不足取实际），B/C分别cold开始并跨中心复用本query资格缓存。比较精确候选集合和顺序；记录真实邻接ID读取、中心去重检查、页/offset/word、空页、资格时间、总取得时间。生成中心路径的ANN不计取得时延，但单独说明；这是trace replay，不冒充E2。

## E2端到端

A是真NodeANN：现有dynamic_triad --arm A --scan-threshold512，不改其行为；它包含目录目标和后代各自的local HNSW与正常小节点扫描，不拿PE-OFFLINE arm C代替。

B/C预算仅64、256、1024；calibration每臂至多3点，选满足nonempty macro/pair>=.999、empty正确、invalid/duplicate/state=0的最快预算。confirmation冻结该预算，单worker串行、轮转顺序重复3次，不按确认收益再选预算。未过校准门保留失败曲线，不继续扫。

主时延为相同统计口径的完整query均值/p50/p95；距离/资格/候选/空间为解释。B/C复用分配好的query workspace并用epoch逻辑清空，跨query不复用资格结果；所有query初始化/访问/排序/merge成本入e2e。粗粒度中心取得计时在两臂相同；C只给cold page编译加嵌套计时，B给整批逐点资格加嵌套计时，说明qualification列仅指二跳内资格子项。

## E3门禁

仅当C同质量稳定快于A和B，才执行已有V/T/M/R/I/D短窗口和图变更hook。否则记录未放行，不把未执行动态代码/复杂度写成已实测结论；不能以增加系统规模或替代算法追正结果。

## 空间与证据

邻域ID/页目录、基础HNSW payload/edges、资格bitplanes、epoch缓存与workspace都计空间。邻域索引不复制向量；B/C初建完释放输入base数组，GT使用图内统一kernel向量，无per-scope/per-neighborhood向量副本。保留失败和所有配置、小CSV/JSON/图入库，原始大输入/二进制留忽略work。严格区分取得成本失败、质量不足、端到端无增益、动态未放行与论文新颖性未核实。
