# 配图生成记录

三张示意图由 GrsAI 图像接口生成，用于本目录 PPT。生成日期 2026-11-03。

| 文件 | 用途 | 出现页 |
|---|---|---|
| `motivation.png` | 层级结构摊平为扁平列表 | 第 2 页 图 1 |
| `morphology.png` | 属性组、代表点与合并后的单层邻接 | 第 6 页 图 2 |
| `merge.png` | 选点、桥接、合并四步 | 第 7 页 图 3 |

## 调用参数

- 接口：`POST https://grsai.dakka.com.cn/v1/api/generate`
- 认证：`Authorization: Bearer <key>`，密钥取自本机 `~/.config/grsai.key`，不入库
- 模型：`gpt-image-2`
- 比例：`16:9`（输出 1672×941）
- `replyType`：`json`，返回 `results[0].url` 后直接下载

## 提示词

三张图共用的风格前缀：

> flat vector technical illustration for an academic paper, pure white background, thin navy blue strokes (#1F3F86), light blue fills, one small orange accent, generous white space, no text, no letters, no numbers, no labels, no watermark

1. `motivation.png`：`left side a small branching hierarchy of circles connected by thin lines, an arrow pointing right to a single horizontal row of small squares representing a flattened list, one branch highlighted in orange with several squares on the right also highlighted orange, clean minimalist diagram`
2. `morphology.png`：`top: five rounded circles arranged as a directed acyclic graph, each circle surrounded by a small cluster of tiny dots; middle: thin curved lines connecting clusters across groups; bottom: one long horizontal strip of small squares representing a single merged adjacency row, thin connectors from clusters down to the strip`
3. `merge.png`：`four aligned panels left to right separated by thin arrows: separate cluster of dots, then a subset of dots marked with rings, then bridges drawn between clusters, then all dots merged into one connected network, minimal geometric, plenty of white space`

## 复现

```bash
KEY=$(cat ~/.config/grsai.key)
curl -sS -X POST https://grsai.dakka.com.cn/v1/api/generate \
  -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
  -d '{"model":"gpt-image-2","aspectRatio":"16:9","replyType":"json","prompt":"<风格前缀>, <构图描述>"}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["results"][0]["url"])'
```

## 说明

- 图像不含文字，图中标注由 PPT 的图注与正文承担；更换图片时保持 16:9，版式按宽度缩放。
- 图像为示意性质，不表示真实 1,929 节点拓扑，也不对应具体向量数据。
