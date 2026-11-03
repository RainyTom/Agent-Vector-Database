#!/usr/bin/env python3
"""Finite scope-equivalence experiment, not an ANN performance benchmark."""
import argparse
import hashlib
import json
from collections import Counter, deque
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--workload', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
objects = [int(x) for x in (a.workload/'objects.txt').read_text().split()]
edges = [tuple(map(int,line.split())) for line in (a.workload/'edges.tsv').read_text().splitlines() if line.strip()]
n = max(objects + [v for edge in edges for v in edge])+1
children, parents = [[] for _ in range(n)], [[] for _ in range(n)]
for child,parent in edges:
    children[parent].append(child)
    parents[child].append(parent)
degree = [len(row) for row in parents]
ready = deque(v for v in range(n) if not degree[v])
ancestor = [1 << v for v in range(n)]
processed = 0
while ready:
    v = ready.popleft()
    processed += 1
    for child in children[v]:
        ancestor[child] |= ancestor[v]
        degree[child] -= 1
        if not degree[child]: ready.append(child)
assert processed == n, 'cyclic input'
# Independent forward DFS, not a transpose copied from the ancestor result.
descendant = []
for start in range(n):
    seen,stack = set(),[start]
    while stack:
        v = stack.pop()
        if v in seen: continue
        seen.add(v)
        stack.extend(children[v])
    descendant.append(seen)
labels = [ancestor[v] | (1 << (n+v)) for v in range(n)]
for start in range(n):
    recursive_mask,direct_mask = 1 << start,1 << (n+start)
    for v in range(n):
        assert bool(labels[v]&recursive_mask) == (v in descendant[start])
        assert bool(labels[v]&direct_mask) == (v == start)
scopes = []
for line in (a.workload/'queries.tsv').read_text().splitlines():
    fields = list(map(int,line.split()))
    if fields:
        assert fields[0] > 0 and len(fields) == fields[0]+1
        scopes.append(fields[1:])
assert len(scopes) >= 120
members = Counter(objects)
histograms = {'direct':Counter(),'recursive':Counter()}
compound_checks = 0
or_and_counterexample = None
for i in range(120):
    scope = set(scopes[i*(len(scopes)-1)//119])
    qmask = sum(1 << v for v in scope)
    recursive_scope = set().union(*(descendant[v] for v in scope))
    for semantics,reference,mask in (('direct',scope,qmask << n),('recursive',recursive_scope,qmask)):
        for v in range(n):
            assert bool(labels[v]&mask) == (v in reference)
            compound_checks += 1
        g = sum(bool(members[v]) for v in reference)
        histograms[semantics][g] += 1
    if len(scope)>1 and or_and_counterexample is None:
        for v in range(n):
            if ancestor[v]&qmask and ancestor[v]&qmask != qmask:
                or_and_counterexample = dict(sampled_query=i,query_attributes=sorted(scope),owner=v,overlap=True,containment=False)
                break
assert or_and_counterexample is not None
result = dict(status='PASS',attributes=n,items=len(objects),edges=len(edges),
    singleton_mode_owner_checks=2*n*n,compound_mode_owner_checks=compound_checks,
    sampled_scopes=120,histograms={sem:dict(sorted(c.items())) for sem,c in histograms.items()},
    or_not_and_example=or_and_counterexample,
    qualification_only=True,ann_search_run=False,
    note='Namespaced direct-owner and inclusive-ancestor labels exactly match direct OR / recursive OR on all tested owners. Does not prove index, memory, update or recall equivalence. Item permutation does not affect this per-owner identity.',
    inputs={name:hashlib.sha256((a.workload/name).read_bytes()).hexdigest() for name in ('objects.txt','edges.tsv','queries.tsv')})
a.out.parent.mkdir(parents=True,exist_ok=True)
a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False))
