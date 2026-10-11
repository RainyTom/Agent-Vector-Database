# 复现审查修复记录

## 已修复

| 审查问题 | 当前处理 |
| --- | --- |
| 缓存窗口方法被标为 SimJoin | 重命名 hard-window-cache；独立实现论文派生 SimJoin/kJoinSlide |
| WindowOrder-epsilon 全扫描 root filter | 替换为图上近似 maxT 搜索；显式记录未被作者核验的搜索策略和宽度 |
| Fast AVJ 将 beam 当总桥接次数 | 恢复优先队列容量限制；回归图验证 beam=1 仍能连续扩展桥接节点 |
| Fast AVJ 种子、停止条件、边界不一致 | 修复全局入口+自身种子、merged 停止检查及严格阈值 |
| nSelectivity 使用近似返回数 | 使用完整真值；抽样/未评估输出 null，另存返回数统计 |
| 搜索时间冒充总在线时间 | 单列搜索、在线顺序、离线顺序、总在线时间 |
| 遗漏顺序与 OOD 的 NDC | 分项统计实际距离计算；明确聚合范围 |
| 同路径普通连接忽略 query-limit | 仅显式自连接可复用加载；普通连接独立处理 X/Y |
| 内存估算被称作实测峰值 | 区分规划估算、进程生命周期 RSS、OS 硬限制 |
| 部分运行入口没有内存约束 | pybind 导入与 C++ 数据/执行入口统一设置 RLIMIT_AS，默认十进制 2 GB |
| 禁用真值仍输出 0 recall | 改为 null；区分 full、prefix-sample、not-evaluated |
| Debug 结果被当正式性能 | 默认 Release；记录编译类型、flags、源码及 hnswlib revision |
| JSON 字符串未转义/缺输入证据 | 转义 provenance，记录输入 SHA256、所有 CLI 参数、硬件及划分说明 |
| Release 下 assert 测试失效 | 测试目标保留断言，新增算法反例与接口回归 |
| 编辑器无法解析绑定/包含路径 | 提供 .pyi，使用 CMake compile_commands，配置可迁移 workspace 路径 |

所有修复测试均为小型合成输入；CTest 在额外 512 MiB 地址空间限制下通过
6/6 项。没有执行论文数据集规模实验，也没有测得任何可冒充论文表格的性能值。
旧状态报告已加失效标记，旧输出不应与修复后的 JSONL 混合汇总。

## 仍不具备完整复现资格

作者未公开/尚未核验的参数、默认 NSG 构图、VBase/XJoin/FGF-Hilbert、维护
与 FreshDiskANN 对照、全量实验与图表仍未完成。导入用户标注的图不等于
核验相应算法。WindowOrder-epsilon 的近似查询具体策略、C++20 工具链和
当前 HNSW 后端必须作为偏离项报告。单元测试通过只证明其覆盖的行为，
不证明与作者程序全面等价。

后续按 [远程交接](remote-experiments.md) 收集原始结果；论文设定、用户配置、
论文报告值和实测结果分别记录，不根据目标结果反向篡改设置。
