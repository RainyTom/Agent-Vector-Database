# 接收0837918：本地实际审读与执行记录

本轮只更新研究设计与验收，不修改线上源码，不重跑服务器已经报告的失败。工具：`read`读取固定commit网页，`functions.eval(language="py")`执行下面的有限计算；无shell benchmark、无新ANN、无服务器原始query网格重放。

## 来源

固定基址：

`https://raw.githubusercontent.com/RainyTom/Agent-Vector-Database/0837918/researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps/`

完整读取`joint-certificate-report.md`、`joint-certificate-analysis.md`、`joint-information-protocol.md`、`experiments/joint-reproduction.md`和`data/joint-information-run2/analysis/audit.json`。`pooled.csv`的reader结果保留在本次会话`artifact://161`；Eval读取完整载荷，不受聊天显示截断影响。

## 实际执行的CSV处理

以下代码片段在Eval内执行。`read`为工具提供的reader辅助函数，不是Python标准库函数；离开本会话应从上面的固定URL取得CSV再交给`csv.DictReader`，不能依赖临时artifact永久存在。

```python
import csv, io
joint_csv_artifact = read('artifact://161')
joint_csv = joint_csv_artifact[joint_csv_artifact.index('dataset,family,r,g,order,'):].strip()
joint_rows = list(csv.DictReader(io.StringIO(joint_csv)))
assert len(joint_rows) == 452
selected_joint = []
for dataset, dim in [('wiki-root3626',64),('wiki-root3626',256),
                     ('wiki-root3626',1024),('sift12000',64)]:
    row = next(r for r in joint_rows
               if r['dataset']==dataset and r['family']=='group'
               and r['r']==str(dim) and r['g']==str(dim)
               and r['order']=='contiguous'
               and r['threshold_mode']=='filtered_topk'
               and r['control']=='correlated')
    den = int(row['eligible'])
    selected_joint.append({
        'dataset':dataset, 'r':dim, 'eligible_weight':den,
        'whole_items':int(row['whole_skip_items']),
        'conditional_items':int(row['conditional_skip_items']),
        'whole_pct':100*int(row['whole_skip_items'])/den,
        'conditional_pct':100*int(row['conditional_skip_items'])/den,
        'filter_extra_pp':100*int(row['filter_extra_items'])/den})
```

输出完整四行见[`../data/wqr-round2-evidence.json`](../data/wqr-round2-evidence.json)。CSV SHA计算对象是`joint_csv.encode()`，即reader载荷经strip处理的文本，不是Git原文件字节。

## 实际执行的三组有限构造

```python
q=(0,3,0); xp=(0,3,0); xm=(0,-3,0)
d2=lambda a,b:sum((x-y)**2 for x,y in zip(a,b))
assert xp[0]==xm[0] and sum(v*v for v in xp[1:])==sum(v*v for v in xm[1:])
# 原平方距离输出[0,36]，保留第一维及余范数时两下界均0。
print([d2(q,xp),d2(q,xm)])

gt={'a'}; exact_blocks={'B'}; alternate_blocks={'A'}; owner={'a':'A','b':'B'}
coverage=lambda blocks:sum(owner[x] in blocks for x in gt)/len(gt)
assert coverage(exact_blocks)==0 and coverage(alternate_blocks)==1
# 这里只演算两个给定选块集合的coverage；不是执行HNSW。
# 对应解析几何：A含合法近点但proxy距q=10；B的合法点及proxy距q=1。
# 精确proxy排序取B；若近似搜索偶然取A，最终GT覆盖可以更高。

cs=[1.0,0.5]; ls=[0.5,1.0]
perq_final=[c*l for c,l in zip(cs,ls)]
assert sum(perq_final)/2 != (sum(cs)/2)*(sum(ls)/2)
print(sum(perq_final)/2, (sum(cs)/2)*(sum(ls)/2))
# 实际输出0.5、0.5625。
```

这些构造只校验逻辑边界，不是浮点全称认证、真实数据收益或新颖性证明。服务器audit所称173568行一致、9216行首轮对应、16条源query等未在本机独立逐条重核；保持来源归属。
