# PiPNN 本轮审读与有限构造复现

日期：2026-11-01。范围是论文获取、索引入库与 Algorithm 3 字面规则的有限例子；**不是作者实现或 ANN 性能复现**。没有克隆/构建作者仓库，没有新增永久测试。

## 原文与环境

- 分析 PDF：`https://arxiv.org/pdf/2602.21247v3`，18 页含附录；本地 `papers/vector-ann/2602.21247-pipnn/paper.pdf`。
- PDF SHA256：`75e9048e910590a6a998ee8b43a1bd498779aecaf90352f6a93da022dbe5a0d9`；3,421,153 bytes。
- 提取工具：Python `requests` + PyMuPDF；`page.get_text(sort=False)`；每页用 `## PDF page N` 分隔。版本及文本 SHA 以日期化 metadata 为准，PDF 图/公式原样保留在原文件中。
- 正式书目：Crossref `10.1145/3770855.3817891`；原始响应保存到论文目录 `crossref-2026-11-01.json`。正式ACM页面/PDF返回403，未比对正式正文。
- 代码入口：`https://github.com/ParAlg/PiPNN`，作者主页交叉验证；核到 commit `443a328dc71a3b7be753d52ca2b3c9a4ba77f3fd`。只读仓库说明，**未核比较器/并发实现**。
- 本轮提取时先尝试 `fitz` 但环境缺模块；安装 `pymupdf` 后以 `import pymupdf` 成功提取。最终全文固定 v3；没有把初次取得的 v1 留作第二份本地全文。

## 实际运行的 HashPrune 规则构造

固定桶编号，隔离哈希/浮点/并发，仅按 Algorithm 3 的严格 `<` 更新。这段完整代码已在 Python 执行；运行下列命令可复现关键断言与输出，不依赖数据集：

```bash
python - <<'PY'
from itertools import permutations

def prune(stream, capacity):
    reservoir = {}
    for ident, bucket, dist in stream:
        if bucket in reservoir:
            if dist < reservoir[bucket][2]:
                reservoir[bucket] = (ident, bucket, dist)
        elif len(reservoir) < capacity:
            reservoir[bucket] = (ident, bucket, dist)
        else:
            farthest = max(reservoir, key=lambda h: reservoir[h][2])
            if dist < reservoir[farthest][2]:
                del reservoir[farthest]
                reservoir[bucket] = (ident, bucket, dist)
    return tuple(sorted(v[0] for v in reservoir.values()))

unique = [('a',0,5), ('b',0,1), ('c',1,2), ('d',2,3), ('e',3,4)]
unique_outputs = {prune(p, 2) for p in permutations(unique)}
assert unique_outputs == {('b','c')}
ties = [('a',0,1), ('b',0,1)]
tie_outputs = {prune(p, 1) for p in permutations(ties)}
assert tie_outputs == {('a',), ('b',)}
print('unique: 120 orders, outputs:', sorted(unique_outputs))
print('ties: 2 orders, outputs:', sorted(tie_outputs))
PY
```

实际观察：互异距离例子全部 120 顺序输出同一集合 `{b,c}`；同桶等距例子两种顺序输出不同ID。结构化输入/输出在 `../data/pipnn-hashprune-semantics.json`。这不构成一般定理证明或官方实现漏洞确认；若官方实现使用 `(distance, ID)` 一致总序，等距反例不适用。bf16会引入并列，具体影响需核实现后判断。

## 持久化核验

通过 Python JSON 解析、唯一ID/关系端点/主题目录与新增证据文件存在性检查，重新打开 PDF 检查18页与SHA，按 PiPNN/DOI/arXiv 别名从总索引检出同一条记录；同时检查新增 Markdown 相对文件链接。结果写 `../data/pipnn-archive-validation.json`。这只证明归档可检索/可定位，不自动证明论文事实或复现性能。

论文结果摘录 `../data/pipnn-paper-results.json` 与本轮有限构造分开保存，不混记实验来源。尚未运行的同RSS首次/复用top-k实验仅列研究问题，不写成结果。
