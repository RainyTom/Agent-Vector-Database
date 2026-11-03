# 汇报用 PPT：层级属性约束下的向量检索

10 页，16:9，可直接打开编辑。生成日期 2026-11-03。

- 成品：[dag-vector-search-deck.pptx](dag-vector-search-deck.pptx)
- 生成脚本：[build_deck.py](build_deck.py)（python-pptx，纯形状，文字均可改）
- 版面检查：[check_deck.py](check_deck.py)（文字溢出、越界、文本框互相遮挡）
- 校徽：[njupt-logo.png](njupt-logo.png)

## 页面顺序

| 页 | 内容 |
|---|---|
| 1 | 封面：题目、副标题、校徽、日期 |
| 2 | 研究背景：属性是 DAG、两种作用域、数据与属性都在变 |
| 3 | 研究动机：逐属性局部图 / 全局图过滤 / 边摘要 三条路的问题 |
| 4 | 研究问题：输入、合格集、三个子问题、约束（不改距离核/不量化） |
| 5 | 方案总览：五步流程 + 与既有做法的关键差别 |
| 6 | 索引建立①：属性内 HNSW（M=64, efC=200）与两类入口代表 |
| 7 | 索引建立②：沿 DAG 逆拓扑组合，四类边与边数拆分 + 示意图 |
| 8 | 两种查询：Direct / Recursive 对照 + 单队列搜索流程（b=0） |
| 9 | 实测现状：4 个规模数字、按子图数 G 的加速比、三条边界 |
| 10 | 已实现 / 未实现 / 下一步（按优先级）+ 一句结论 |

## 数字来源（均可追溯到仓库）

| 数字 | 来源 |
|---|---|
| 168,937 项、1,929 属性、2,040 边、103 多父、768 非空直属集合 | [构图交付文档](../../2026-11-02-exps/dag-composition-delivery.md) |
| 5,697,634 有向边 = 本地 4,019,094 + 桥 1,652,935 + 快捷 25,605；最大行度 193；构建 7.99–8.21 s | [构建成本实测](../data/dag-update-cost/build-cost.csv) |
| 6,589 入口条目 | 同上 `portals` 列 |
| 每项祖先属性数 4.21（最大 11） | [更新成本模型](../data/dag-update-cost/update-cost-model.json) |
| G 分组加速比 0.79 / 1.16 / 2.49 等 | [按子图数比较](../subgraph-efficiency-report.md)、[ratios.csv](../data/dag-subgraph-efficiency/analysis/ratios.csv) |
| 距离调用只少 6%/5% | [原生工作量诊断](../direct-work-report.md) |
| 单次换属主中位约 3.15×10⁵ 次距离计算 | [更新定位评估](../update-positioning.md) |
| 创新性边界（Stitched/LSSG/UNG 等前序） | [alphaXiv 审查](../alphaxiv-dag-novelty.md) |

第 9、10 页刻意保留“未证实”措辞：单属性更慢、G≥3 有召回下降、更新未实现。对外汇报时不要把这些数字改成总体胜出。

## 校徽来源与替换

`njupt-logo.png` 取自第三方标识镜像站（urongda.com，1024×1047 RGBA），SHA256 为
`242611eac2115917faab4f186ce9d82bc8b5e3e1d274618d1b60f7b010a4b2df`。
**不是学校宣传部发布的矢量原件**；正式对外使用请替换为 [宣传部下载区](https://xcb.njupt.edu.cn/) 的官方 SVG/AI 版本，文件名保持 `njupt-logo.png` 即可重跑脚本。

## 重新生成

```bash
python3 researches/nested-attr-vector-search/2026-11-03-exps/ppt/build_deck.py \
  --logo researches/nested-attr-vector-search/2026-11-03-exps/ppt/njupt-logo.png \
  --out researches/nested-attr-vector-search/2026-11-03-exps/ppt/dag-vector-search-deck.pptx
python3 researches/nested-attr-vector-search/2026-11-03-exps/ppt/check_deck.py \
  --deck researches/nested-attr-vector-search/2026-11-03-exps/ppt/dag-vector-search-deck.pptx
```

渲染核对（需要 LibreOffice 与 poppler）：

```bash
soffice --headless --convert-to pdf --outdir work/ppt-njupt/render <pptx>
pdftoppm -r 110 -png work/ppt-njupt/render/*.pdf work/ppt-njupt/render/slide
```

本轮已实际执行：`check_deck.py` 报 `text_boxes_checked=160 problems=0`；LibreOffice 导出 10 页 PDF 并逐页转 PNG 人工查看，修正了封面校徽在深色底不可见、卡片过高留白、第 7 页结论行压框、第 8 页分隔条过重等问题。生成用的 LibreOffice/中文字体是本次为渲染核对新装的系统组件，PPT 文件本身不依赖它们。

## 已知限制

- 字号按 PowerPoint 的 Microsoft YaHei 设定；在缺少该字体的机器上会用替代中文字体，行宽略有差异。
- 第 7 页右侧 DAG 示意图是 5 节点示意，不是真实 1,929 节点拓扑。
- 页面文字为可直接编辑的文本框，调整措辞后建议重跑 `check_deck.py`。
