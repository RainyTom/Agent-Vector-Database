# 邻域页方案：本地有限集合探针

在Python Eval实际执行以下逻辑，未调用ANN、未读取真实向量，不是性能测试。下面保留代码以便复现；没有将它加入生产测试套件。

```python
n = 4
edges = [(u, v) for u in range(n) for v in range(n) if u != v]

def expand2(g, u):
    return set().union(*(g[w] for w in g[u])) - {u} if g[u] else set()

def pageencode(ids):
    p = {}
    for x in ids:
        p[x // 2] = p.get(x // 2, 0) | (1 << (x % 2))
    return p

def intersect_pages(p, F):
    fp = pageencode(F)
    return {2*b+i for b, bits in p.items() for i in range(2)
            if (bits & fp.get(b, 0)) >> i & 1}

filters = [{i for i in range(n) if m >> i & 1} for m in range(1 << n)]
set_checks = update_checks = 0
for bits in range(1 << len(edges)):
    g = [set() for _ in range(n)]
    for j, (u, v) in enumerate(edges):
        if bits >> j & 1:
            g[u].add(v)
    old = [expand2(g, u) for u in range(n)]
    for u in range(n):
        for F in filters:
            assert intersect_pages(pageencode(old[u]), F) == old[u] & F
            set_checks += 1
    for s, t in edges:
        g2 = [v.copy() for v in g]
        g2[s].symmetric_difference_update({t})
        affected = {s} | {u for u in range(n) if s in g[u] or s in g2[u]}
        rebuilt = [expand2(g2, u) if u in affected else old[u]
                   for u in range(n)]
        assert rebuilt == [expand2(g2, u) for u in range(n)]
        update_checks += 1
print(set_checks, update_checks)
```

实际计数：262144次候选集合核对、49152次单邻接修改核对，断言失败0；图4096个，页大小2。全部F子集只检查最终联合资格集合的接口，不验证目录实现、AND/OR编译器、HNSW或C++位运算边界。

依赖说明：E2(u)依赖N(u)及其中每个w的N(w)。当S中的出邻接改变时，S外只有旧/新出邻居包含S顶点的u可能受影响；合并S与新旧Pred(S)即可覆盖。多处修改取并集；不意味着维护工作量恒定，高入度可能扩大到大部分图。

结果保存于[neighborhood-page-probe.json](../data/neighborhood-page-probe.json)。新真实数据ANN实验0；性能、新颖性、动态成本仍未验证。没有把有限枚举数量当独立真实query数。
