# c1c24e8收尾审读与新边界探针

## 来源与没有执行的工作

固定commit c1c24e87631c8b519caa32b2819c017d6347119a。read读取GitHub完整报告、commit API、runner；两名只读reviewer读取索引/runner/E0/网格及基线语义。Eval用已核8649e55源文件和commit API unified patch重建修改文件，逐行检查context并核Git blob SHA，原文件未改：

- `wqr_e0_tests.cpp`: `8ad129b8fc614c0abc3f83bab01d86d5b053d5ea`
- `wqr_index.h`: `5c7ebd3e71354008367ac76e6ddb7b1213a482e2`
- `wqr_research.cpp`: `9aa7ef786d13c8d092963310565ed09ba6b5ddcb`

快照保存在ignored work/wqr-review-c1c24e8/，不是生产修复。离开本会话可从固定commit的raw URL重新取源，不依赖临时artifact。没有重跑用户报告的80次网格、9组E0或真实数据负结果；服务器work/聚合本机不可访问。

## 实际执行：报告数值与有限集合不变量

在Python Eval执行：

```python
print((6052/5323-1)*100)  # 13.695284613939517
print((1-5634/9440)*100)  # 40.31779661016949
print(206/113)           # 1.823008849557522
print(11869*1024*4)      # 48615424
U=set(range(4)); tested=0
subsets=[{i for i in U if bits>>i&1} for bits in range(16)]
for G in subsets:
    for S in subsets:
        for T in subsets:
            if not T<=S: continue
            recall=len(T&G)/len(G) if G else (1.0 if not T else 0.0)
            coverage=len(S&G)/len(G) if G else 1.0
            assert recall<=coverage
            tested+=1
print(tested)            # 1296
```

这是基于用户报告数值的代数计算，不是独立从query CSV聚合；有限构造验证同口径不变量，不证明ANN精确。GT={a}、a属于块A时，选块{A,B}与{A,C}都覆盖1但不是同一组选块，说明coverage相等不足以证明proxy/选块一致。

## 实际执行：新split容量边界

原实现split的2049对象中位切分得到1024/1025，但两child容量都传1024。仅构造合法满块再经公开apply_I触发该边界；这不是重跑用户已报告的ANN失败。write工具将下列代码保存到work/wqr-review-c1c24e8/split_boundary.cpp：

```cpp
#include "wqr_index.h"
#include <iostream>
#include <numeric>
int main() {
    wqr::WQRIndex idx;
    const size_t full = 2*wqr::B0;
    idx.init(full+1, 2);
    idx.parent={-1};
    idx.euler.build(idx.parent);
    idx.direct.resize(1);
    std::vector<wqr::Id> ids(full);
    std::iota(ids.begin(),ids.end(),0);
    for (size_t id=0; id<=full; ++id) {
        idx.items[id].node=0;
        idx.items[id].alive=id<full;
        idx.vectors[2*id]=float(id)*0.0001f;
        if (id<full) {
            idx.direct_pos[id]=idx.direct[0].size();
            idx.direct[0].push_back(id);
        }
    }
    idx.buildBlockFromIds(ids,full);
    std::cerr << "before insert: live=" << idx.blocks[0].live
              << " capacity=" << idx.blocks[0].capacity
              << " incoming=" << full << "\n";
    wqr::MaintenanceCounters counters;
    idx.apply_I(full,0,0,0,0,counters);
    std::cerr << "completed split: " << counters.splits << "\n";
}
```

实际执行命令：

```sh
g++ -O1 -g -std=c++17 -D_GLIBCXX_ASSERTIONS -Iwork/wqr-review-c1c24e8 -Icodes/directory-aware-vector-attr-search/src -Icodes/vector-join-reproduction/build/_deps/hnswlib-src work/wqr-review-c1c24e8/split_boundary.cpp -o /tmp/wqr-c1c24e8-split -lpthread
/tmp/wqr-c1c24e8-split
```

使用现有公共RawL2Space与此前已核固定revision的hnswlib副本；未修改任一头文件。实际输出：

```text
before insert: live=2048 capacity=2048 incoming=2048
std::vector<size_t>::operator[]: Assertion '__n < this->size()' failed.
```

进程退出134；无`completed split`输出。仅做checked-vector边界诊断，未称通过ASan或完成修复。临时二进制在收尾删除；诊断源码可由本文重建，work快照不入库。

## 结果记录

[data/wqr-c1c24e8-closeout.json](../data/wqr-c1c24e8-closeout.json)记录来源、四行聚合口径疑点、比例计算、新split实际结果和未实施事项。对E1的影响必须查看既有updates.csv哪些run触发split；不能自动把全部质量负结果归为实现bug。没有新增真实数据ANN实验、没有生产修复、没有修复后结果。
