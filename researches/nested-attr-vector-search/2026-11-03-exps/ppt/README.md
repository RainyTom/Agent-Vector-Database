# 汇报 PPT：层级属性约束下的向量混合搜索

10 页，16:9。当前版本 2026-11-03。

- 成品：[hierarchical-hybrid-search-deck.pptx](hierarchical-hybrid-search-deck.pptx)
- 生成脚本：[build_deck_v7.py](build_deck_v7.py)（python-pptx；文字、示意图与伪代码均为可编辑对象）
- 版面检查：[check_deck.py](check_deck.py)（文字溢出、元素越界、文本框互相遮挡、图片超出页面）
- 校徽：`njupt-logo.png`；示意图：`fig/`（来源与提示词见 [fig/PROMPTS.md](fig/PROMPTS.md)）

## 页面结构

| 页 | 内容 |
|---|---|
| 1 | 封面 |
| 2 | 应用场景：ACM CCS 概念体系与两种检索语义（图 1） |
| 3 | 两种语义的规模差异（直属 / 含后代，SciHTC 统计） |
| 4 | 研究动机：概念范围摊平后，变更代价沿子树传播（图 2） |
| 5 | 现有工作：仅列已发表工作 |
| 6 | **由查询语义推导索引结构**（事实 → 要求 → 决策 → 步骤） |
| 7 | **构建流程的依赖关系**（数据依赖图 + 每步输入输出） |
| 8 | 步骤一 局部图（前置/操作/后置 + 图 3） |
| 9 | 步骤二 代表点（两类代表递推 + 图 4） |
| 10 | 步骤三 合并（四类边 + 图 5） |
| 11 | 步骤四 逆拓扑序推进（图 6） |
| 12 | 步骤五 收尾落盘（CSR 与入口表 + 图 7） |
| 13 | **构建不变量 I1–I7 与检验方式** |
| 14 | 算法汇总：构建（算法 1） |
| 15 | 算法汇总：查询（算法 2） |
| 16 | 实验设置与结果、限制 |

索引章节按“推导 → 依赖 → 五步（每步前置/操作/后置）→ 不变量 → 伪代码”组织：
第 6 页给出五条查询事实与对应决策，第 7 页给出数据依赖图，第 8–12 页每步都有前置/后置条件与规模，
第 13 页把七条不变量与检验方式集中列出，第 14–15 页才是完整伪代码。

## 数字来源

| 数字 | 来源 |
|---|---|
| 1,929 概念 / 2,040 边 / 103 多父 / 深度 ≤ 6 / 扇出最大 18 | 本机统计，已归档为 [dag-shape.json](../data/dag-update-cost/dag-shape.json) |
| 整库祖先位图 880,649 条；子树 MOVE 中位 576、p90 7,511、根 156,370（17.8%） | 同上，按 PE-OFFLINE 口径计数 |
| SciHTC 186,160 篇、816 个概念、26 个多父；Information retrieval 3,722/10,881 等 | [ccs-scenario-stats.json](../data/ccs-scenario-stats.json)，由 [ccs_scenario_stats.py](../experiments/ccs_scenario_stats.py) 生成 |
| 5,697,634 有向边（本地 4,019,094 / 桥 1,652,935 / 快捷 25,605）；最大行度 193；构建 8.0–8.2 s | [构建成本实测](../data/dag-update-cost/build-cost.csv) |
| 行度 中位 26 / p90 74 / p99 98 / 均值 33.7；>64 共 26,254 行、>128 共 122 行 | 同上 CSV 的 `deg_*` 列 |
| 入口 6,589 条、非空属性 768 个 | 同上 |
| 加速比 0.79 / 1.16 / 2.49（G = 1/2/3） | [按子图数比较](../subgraph-efficiency-report.md) |
| 更新成本估算与失效条件 | [更新定位评估](../update-positioning.md) |
| 前序与创新性边界 | [alphaXiv 审查](../alphaxiv-dag-novelty.md) |

## 重新生成

```bash
python3 researches/nested-attr-vector-search/2026-11-03-exps/ppt/build_deck_v7.py \
  --logo researches/nested-attr-vector-search/2026-11-03-exps/ppt/njupt-logo.png \
  --figdir researches/nested-attr-vector-search/2026-11-03-exps/ppt/fig \
  --out researches/nested-attr-vector-search/2026-11-03-exps/ppt/hierarchical-hybrid-search-deck.pptx
python3 researches/nested-attr-vector-search/2026-11-03-exps/ppt/check_deck.py \
  --deck researches/nested-attr-vector-search/2026-11-03-exps/ppt/hierarchical-hybrid-search-deck.pptx
```

渲染核对（需 LibreOffice 与 poppler）：

```bash
soffice --headless --convert-to pdf --outdir /tmp/deck <pptx>
pdftoppm -r 110 -png /tmp/deck/*.pdf /tmp/deck/slide
```

本轮已执行：`check_deck.py` 报 `text_boxes_checked=141 problems=0`；导出 PDF 并逐页转 PNG 检查，修掉了图片超出页面、图注压页码、表格列宽溢出与行距错位等问题。

## 校徽来源

`njupt-logo.png` 取自第三方标识镜像站（1024×1047 RGBA），SHA256 `242611eac2115917faab4f186ce9d82bc8b5e3e1d274618d1b60f7b010a4b2df`。
不是学校宣传部矢量原件；对外使用请替换为[宣传部下载区](https://xcb.njupt.edu.cn/)的官方版本，文件名保持不变即可重跑脚本。

## 限制

- 字号按 Microsoft YaHei 设定，缺少该字体时行宽略有差异。
- 图 1 为 CCS 概念的缩进树示意，节点名为真实概念，篇数取自 SciHTC 标注；图 2–4 为示意图，不对应真实拓扑。
- 第 10 页保留“单作用域更慢、G ≥ 3 部分样本召回 0.983–0.997、更新未实现”等限制，表述前需一致。
