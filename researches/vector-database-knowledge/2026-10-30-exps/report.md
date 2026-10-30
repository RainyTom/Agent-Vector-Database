# 2026-10-30｜向量数据库知识库首轮学习与小探针

## 目标

在实验服务器执行期间，参考指定[lakehouse learning paths](https://wangyong9999.github.io/lakehouse-wiki/learning-paths/)建立可持续的本地向量数据库知识路径。重点是机制/语义/成本，而非转载产品排行榜或另起大规模实现。

## 本轮资料与文档产出

- 阅读参考站点总路径、一周、一月AI、一季度和向量数据库页，采用阅读→实践→自测→进阶的结构。
- 核对论文总索引与已有ACORN、TrieHI、系统过滤笔记；新增知识目录，不重复新增论文/关系记录。
- 五个知识章：[查询语义](../query-semantics.md)、[索引机制](../index-mechanisms.md)、[过滤与层级](../filtering-and-hierarchy.md)、[生命周期与系统](../lifecycle-and-systems.md)、[评测与成本](../evaluation-and-cost.md)；配[学习路径](../learning-paths.md)、[学习记录/勘误/队列](../learning-log.md)、[24条来源台账](../sources.jsonl)。
- 已读来源章节与版本在各章列明；活文档未冻结。未通读所有引用论文，不把官方性能描述当本机实测。官网Milvus页一次超时后改读官方v2.5.x原始文档，记录获取路径而非猜测内容。

## 实验设置

| 探针 | 输入与执行 | 覆盖边界 |
|---|---|---|
| 语义/反例 | Python标准库，确定性构造向量/集合 | 数学定义与有限反例，不是ANN或FP32认证 |
| 真实HNSW API | 固定hnswlib `ca426729609b3221563047ceb269cc1213e0d376`，4对象/4维，外部ID10/20/30/40，M4、efConstruction20、ef16、seed100 | 真建图/过滤/墓碑/恢复；不测性能、数据库事务或物理擦除 |

C++距离复用仓库`RawL2Space`，编译`g++ -O2 -std=c++17 -Wall -Wextra`，单进程执行；二进制留ignored work，不提交。完整[命令与stdout/stderr](data/run-log.json)、[SHA/版本](data/probe-manifest.json)、[复现说明](experiments/reproduction.md)。第三方头文件产生unused/type-limits警告，编译与执行退出均为0；未改第三方代码抑制警告。

## 实际结果

以下是有限构造的计算/API结果，不作为生产性能数据。

| 观察 | 实际输出 |
|---|---|
| 未归一化IP最大对象与cosine最大对象 | 分别是(10,10)和(1,0)；两者IP为10/1，cosine约0.7071/1 |
| 投影信息丢失 | 原空间平方距离100，保留坐标的距离0 |
| 同为1%合法对象、全局前50候选、k5 | 近邻区域合法集合返回5，远端合法集合返回0 |
| 均匀不放回抽样的零匹配概率模型 | 约0.597311；只适用于所设随机模型，不推给按距离排序的相关数据 |
| 两条非空Q的质量聚合 | macro0.5，pair约0.909091；混入成功空Q后macro约0.666667 |
| 精确rerank缺失候选 | 原GT[1,2]、候选[3,2]，精排[2,3]后recall仍0.5 |
| HNSW删除前过滤结果 | IDs[20,40]，平方距离[0.5625,7.5625] |
| markDelete(20)后 | 存储元素数仍4、墓碑1、返回[40] |
| unmarkDelete(20)后 | 返回[20,40]、墓碑0，原payload读取一致 |

另已计算同块边际非空但AND为空、DAG集合相交且互不包含的反例；全部显式校验通过。原始结果分别为[semantic-probes.json](data/semantic-probes.json)和[hnsw-lifecycle-probe.json](data/hnsw-lifecycle-probe.json)。实际过滤callback11次不用于推导一般搜索复杂度。

收尾导航检查已扫描知识库及根README共11份Markdown、85条本地链接，缺失文件/锚点0；24条来源ID唯一且所有local_notes存在。外部URL只做格式核验，未再次自动抓取，不能称全部HTTP可达性检查。具体记录保存在同目录data/navigation-check.json。

## 结论与限制

1. 知识库已具有可用正文、顺序、自测、版本来源和实测入口，不只是链接清单。
2. 参考站点的泛化复杂度、产品一致性标签与无配置性能数字不能直接继承；已按具体来源写入勘误与边界。
3. 向量搜索的exact资格、approximate排序、候选覆盖、压缩误差、写可见性和索引维护应独立解释。当前小例子支持这些区别，不证明新索引效率或数据库能力。
4. 新真实数据ANN运行0、数据库服务运行0、并发/故障注入0、服务器任务改动0。没有启动自动学习、定时任务或后台监控。

## 下一步

先在一次性、固定版本环境做pgvector EXPLAIN与迭代过滤小实验，再做两会话可见性；并在服务器结果返回后将新的机制/失败归因追加进知识页。按[队列](../learning-log.md)推进，不预先填写未执行结果。

## 同日续学：查询预算与合法结果堆

服务器5fc1be3结果到达后新增[学习记录](../learning-log.md#2026-10-30续学从5fc1be3负结果学停止规则与预算口径)与[评测知识续节](../evaluation-and-cost.md#续学查询预算应靠近工作点不能由极高质量门单向驱动)。核固定hnswlib参数说明及项目源码：efConstruction与在线ef独立，hnswlib ef即efSearch；当前全scope合法堆在E<ef时不能距离早停，连通骨架将导致全展开。

实际在原C++48对象探针执行E4<ef8及空GT，五臂编译边数不同但expanded/distance均48；另完成小预算A/U/R/H×15/20×3的24次构造输入回放，每次180Q。见[性质输出](../../nested-attr-vector-search/2026-10-30-exps/data/closure-budget-probe.json)及[流水线证据](../../nested-attr-vector-search/2026-10-30-exps/data/closure-small-ef-local-check.json)。不是原真实失败重跑或新的WIKI/SIFT结果。

按用户纠偏已交付[服务器小ef固定重跑](../../nested-attr-vector-search/2026-10-30-exps/closure-small-ef-server-prompt.md)，保留全部Recall—time点，不再为`.999`自动升到1024。历史质量/效率失败与未测Dynamic不改写；同预算不自动等于同质量，真实E<ef比例待只读原CSV统计。

