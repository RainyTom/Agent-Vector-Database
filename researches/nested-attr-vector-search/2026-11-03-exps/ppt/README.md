# 汇报 PPT：层级属性约束下的向量混合搜索

10 页，16:9。当前版本 2026-11-03。

- 成品：[hierarchical-hybrid-search-deck.pptx](hierarchical-hybrid-search-deck.pptx)
- 生成脚本：[build_deck_v4.py](build_deck_v4.py)（python-pptx；文字均为可编辑文本框）
- 版面检查：[check_deck.py](check_deck.py)（文字溢出、元素越界、文本框互相遮挡、图片超出页面）
- 校徽：`njupt-logo.png`；示意图：`fig/`（来源与提示词见 [fig/PROMPTS.md](fig/PROMPTS.md)）

## 页面结构

| 页 | 内容 |
|---|---|
| 1 | 封面 |
| 2 | 研究动机：层级属性摊平后的递归更新代价（图 1） |
| 3 | 现有工作：四种摊平方式及其更新代价 |
| 4 | 现有工作：层级感知索引与动态维护 |
| 5 | 问题定义：记号、合格集合、实验约束 |
| 6 | 索引形态：四个组成部分与数据形态（图 2） |
| 7 | 构建算法（算法 1 伪代码 + 参数与输出规模，图 3） |
| 8 | 合并规则：代表点选择与出度控制（伪代码 + 边类型表 + 行度分布） |
| 9 | 查询算法（算法 2 伪代码 + 执行性质） |
| 10 | 实验设置与结果、限制 |

先前版本（`dag-vector-search-deck.pptx`，四卡片式排版、含大量说明性副标题）已被本版替代；该文件与对应脚本保留在 git 历史中。

## 数字来源

| 数字 | 来源 |
|---|---|
| 1,929 属性 / 2,040 边 / 103 多父 / 深度 ≤ 6 / 扇出最大 18 | 本机统计 `work/dag-shape.json`（由 `work/dag-scope/workload-run1` 计算） |
| 整库祖先位图 880,649 条；子树 MOVE 中位 576、p90 7,511、根 156,370（17.8%） | 同上脚本，按 PE-OFFLINE 口径计数 |
| 5,697,634 有向边（本地 4,019,094 / 桥 1,652,935 / 快捷 25,605）；最大行度 193；构建 8.0–8.2 s | [构建成本实测](../data/dag-update-cost/build-cost.csv) |
| 行度 中位 26 / p90 74 / p99 98 / 均值 33.7；>64 共 26,254 行、>128 共 122 行 | 同上 CSV 的 `deg_*` 列 |
| 入口 6,589 条、非空属性 768 个 | 同上 |
| 加速比 0.79 / 1.16 / 2.49（G = 1/2/3） | [按子图数比较](../subgraph-efficiency-report.md) |
| 更新成本估算与失效条件 | [更新定位评估](../update-positioning.md) |
| 前序与创新性边界 | [alphaXiv 审查](../alphaxiv-dag-novelty.md) |

## 重新生成

```bash
python3 researches/nested-attr-vector-search/2026-11-03-exps/ppt/build_deck_v4.py \
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
- 示意图为结构示意，不对应真实拓扑或数据分布。
- 第 10 页保留“单作用域更慢、G ≥ 3 部分样本召回 0.983–0.997、更新未实现”等限制，表述前需一致。
