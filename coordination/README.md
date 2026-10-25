# Planner–Executor 协作协议

本目录是服务器上 planner（AI 规划模型）与 executor（执行模型）之间的唯一接口。所有任务、结果、状态以文件为准；对话只用于澄清，不作为交付凭证。

## 1. 目录

```text
coordination/
├── README.md            # 本协议
├── state.json           # 当前项目状态（唯一事实来源）
├── log.md               # 追加式时间线，一行一个事件
├── tasks/
│   ├── _template.json
│   └── T-0001.json      # 待执行/进行中的任务
├── results/
│   ├── _template.json
│   └── T-0001.json      # 对应任务的结果
└── archive/             # 已完成任务的归档（可选）
```

- 任务文件只由 **planner** 创建或修改目标/验收；
- 结果文件只由 **executor** 写入；
- `state.json` 由 executor 在每次任务结束后更新，planner 审核时可修改 `decisions`；
- `log.md` 只追加，不删除历史。

## 2. 角色与边界

### Planner

1. 读 `state.json` 与最近的结果，决定下一件事；
2. 一次只给一个**单一目标**，写成任务 JSON；
3. 验收必须可观察：命令 + 期望产物/指标；
4. 不要求 executor 在无数据时做方向性判断；
5. 审核结果：`accepted` 或 `rejected`，拒绝时给出具体缺口；
6. 不直接改代码；需要改代码时写成任务。

### Executor

1. 读 `PROJECT_HANDOFF.md`、`AGENTS.md` 与本协议；
2. 领取任务（把结果文件状态置为 `claimed`），执行，回报；
3. **不得编造数字**；区分“实测 / 推断 / 未核实”；
4. 不改研究定义、不新增新颖性主张，除非任务明确要求；
5. 大文件写 `work/`，只提交小体积 CSV/MD；
6. 不提交 `datasets/`、`work/`、`models/`、`venv_potion/`、`deps/`；
7. 不 `git push --force`，不重写历史；
8. 破坏性命令（删除数据、重置分支、覆盖结果）需 planner 或用户批准；
9. 超预算（墙钟/运行次数/磁盘）立即停止并回报部分结果。

## 3. 任务格式

`tasks/T-0001.json`：

```json
{
  "id": "T-0001",
  "title": "短标题",
  "priority": "P0",
  "objective": "一句话说明要回答什么",
  "inputs": [
    "要读的文件或数据路径",
    "已有二进制/脚本"
  ],
  "constraints": [
    "不得修改 src/",
    "只用 potion workload",
    "单次运行不超过 10 分钟"
  ],
  "acceptance": [
    "命令 X 退出码 0",
    "产出 results/foo.csv，含列 a,b,c",
    "指标 Y 与基线 Z 的差异在 ±5% 内"
  ],
  "artifacts": [
    "researches/.../data/foo.csv",
    "coordination/results/T-0001.json"
  ],
  "budget": {"wall_minutes": 60, "max_runs": 5},
  "parallel_ok": false,
  "depends_on": []
}
```

要求：

- `objective` 不含两个以上独立目标；
- `acceptance` 每条都能用命令或文件检查；
- 需要新建文件时给出准确路径；
- 依赖其他任务时写 `depends_on`，不要口头约定。

## 4. 结果格式

`results/T-0001.json`：

```json
{
  "task_id": "T-0001",
  "status": "completed",
  "summary": "不超过 10 行；结论先行",
  "commands": [
    "cd ... && ./work/bench --dir ... --mode gt --out ..."
  ],
  "metrics": {
    "recall_at_10": 0.9894,
    "mean_us": 5656.2,
    "p95_us": 14959.2
  },
  "artifacts": [
    "researches/.../data/est/summary.csv",
    "work/bench_out_est/est_rep1.log"
  ],
  "errors": [],
  "missing": [],
  "next": ["建议的后续任务，不是承诺"],
  "env": {"cpu": "10 vCPU", "ram_gb": 11, "gcc": "15.2.0"},
  "commit": "e668cda"
}
```

`status` 取值：

| 状态 | 含义 |
|---|---|
| `claimed` | executor 已领取，尚未开始 |
| `running` | 正在执行 |
| `completed` | 验收全部满足 |
| `partial` | 部分完成，给出已完成与未完成 |
| `blocked` | 缺信息/权限/数据；`missing` 必须具体 |
| `failed` | 执行失败；`errors` 含关键日志 |
| `cancelled` | planner 取消 |

**禁止**：`completed` 但验收未过；用“大概/应该”代替指标；只给结论不给命令与产物。

## 5. 工作流

```text
planner: 写 tasks/T-0001.json
executor: 读任务 → 写 results/T-0001.json {status: claimed}
executor: 执行 → 更新 status=running（可选）
executor: 完成 → 写 metrics/artifacts/commands → status=completed
executor: 更新 state.json；log.md 追加一行
planner: 审阅 → accepted / rejected（写进 result 的 review 字段）
```

并行规则：

- 默认一次一个写任务；
- 若两个任务互不写同一文件且 `parallel_ok=true`，可并行；
- 并行任务必须在 `depends_on` 中声明资源冲突（同一 workload、同一输出目录、同一构建）。

## 6. 状态文件

`state.json` 字段：

| 字段 | 含义 |
|---|---|
| `updated` | 日期 |
| `project` | 项目名 |
| `git` | 分支、HEAD、main 是否同步 |
| `phase` | 当前阶段 |
| `best_results` | 主 workload 的最好数字（含方法名） |
| `workloads` | 可用 workload 与状态 |
| `open_decisions` | 需要 planner/用户拍板的问题 |
| `next_tasks` | 建议的下一批任务（id + 一句话） |
| `constraints` | 不可违反的约束（数据不入库、不强推等） |

Executor 每次任务结束必须更新 `best_results`（若刷新）与 `updated`；planner 可更新 `open_decisions` 的决议。

## 7. 日志格式

`log.md` 一行一个事件：

```text
2026-10-25T15:04Z T-0001 completed recall=0.9894 mean_us=5656 artifacts=data/est/summary.csv commit=e668cda
2026-10-25T15:20Z T-0002 blocked missing="Istanbul CSV not present"
```

## 8. 升级与阻塞

Executor 遇到以下情况必须 `blocked`，不要自行假设：

- 数据/模型/二进制缺失且无法重建；
- 任务与 `problem-definition.md` 冲突；
- 需要改研究定义或新颖性主张；
- 需要删除/覆盖既有结果或数据；
- 预算不足以完成验收。

`missing` 写清楚：缺什么、尝试过什么、需要谁提供。

## 9. 安全与合规

- 不把 API key、token、私有数据写进仓库或结果文件；
- 下载仅限 `datasets.md` 列出的公开数据与 `models/` 列出的模型；
- 引用论文结论必须给出来源（DOI/arXiv/本地路径）；
- 论文库更新遵循 `AGENTS.md`：先查重、按主题目录、更新 `papers.jsonl` 与 `changelog.md`。

## 10. 验收模板（planner 用）

好的验收：

```text
- 运行 `work/bench --mode gt --limit 5`，退出码 0
- `selftest bad=0`
- 产出 `data/foo.csv`，列名与 `data/est/summary.csv` 一致
- Recall 与既有结果差异 ≤ 0.5 个百分点
```

坏的验收：

```text
- 看看效果好不好
- 优化一下
- 尽量快
```
