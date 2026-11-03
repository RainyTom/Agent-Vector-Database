#!/usr/bin/env python3
"""Cost model for one item relabel under the current static builder.

Counts candidate distance evaluations, not measured time. The two variants are:
  naive    - rerun build_variant's loops for every affected attribute
  targeted - refresh only the moved item's bridges plus the affected portals
Neither is implemented; this only sizes the work the code would do.
"""
import argparse
import json
import statistics as st
from collections import Counter, deque
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--workload', type=Path, required=True)
p.add_argument('--dim', type=int, default=128)
p.add_argument('--portals', type=int, default=8)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()

objects = [int(x) for x in (a.workload/'objects.txt').read_text().split()]
edges = [tuple(map(int, l.split())) for l in (a.workload/'edges.tsv').read_text().splitlines() if l.strip()]
n = max(objects + [v for e in edges for v in e]) + 1
children = [[] for _ in range(n)]
parents = [[] for _ in range(n)]
for c, par in edges:
    children[par].append(c)
    parents[c].append(par)
degree = [len(r) for r in parents]
ready = deque(v for v in range(n) if not degree[v])
topo = []
while ready:
    v = ready.popleft()
    topo.append(v)
    for c in children[v]:
        degree[c] -= 1
        if not degree[c]:
            ready.append(c)
assert len(topo) == n, 'cyclic input'
# parents precede children in this topological order.
anc = [set([v]) for v in range(n)]
for v in topo:
    for c in children[v]:
        anc[c] |= anc[v]
members = Counter(objects)
nonempty = [x for x in range(n) if members[x]]
p8, dim = a.portals, a.dim

def attribute_cost(c):
    groups = (1 if members[c] else 0) + sum(1 for ch in children[c] if members[ch])
    pool = min(p8, members[c]) + p8*len(children[c])
    naive = (members[c] + sum(members[ch] for ch in children[c]))*groups*p8
    return naive, groups*p8*dim + p8*pool*dim

naive_total = portal_total = 0
per_attr = {}
for c in range(n):
    per_attr[c] = attribute_cost(c)
    naive_total += per_attr[c][0]
    portal_total += per_attr[c][1]

rows = []
for owner in nonempty:
    naive = sum(per_attr[c][0] for c in anc[owner])
    targeted = sum(per_attr[c][1] for c in anc[owner])
    rows.extend([(naive, targeted, len(anc[owner]))]*members[owner])

def summary(index):
    vals = sorted(r[index] for r in rows)
    return dict(median=st.median(vals), p95=vals[int(.95*len(vals))], maximum=vals[-1],
                mean=sum(vals)/len(vals))

result = dict(
    status='COST_MODEL_ONLY__NOT_IMPLEMENTED',
    items=len(objects), attributes=n, owners=len(nonempty),
    ancestors_per_item=dict(maximum=max(r[2] for r in rows), mean=sum(r[2] for r in rows)/len(rows)),
    full_build=dict(naive_bridge_pairs=naive_total, portal_distances=portal_total),
    per_relabel=dict(naive_bridge_pairs=summary(0), targeted_distances=summary(1)),
    relabels_equal_to_one_rebuild=dict(
        naive=naive_total/st.median([r[0] for r in rows]),
        targeted=portal_total/st.median([r[1] for r in rows])),
    update_classes={
        'A_rename_metadata_only': 'no vector or graph change; not modeled here',
        'B_membership_change': 'one item leaves owner a and joins owner b: local HNSW delete plus insert, then both ancestor chains',
        'C_topology_MM': 'attribute DAG MOVE/MERGE changes ancestors of whole subtrees'},
    note='Distance counts assume farthest-first portals and the current bridge loops. They exclude local HNSW delete/insert, reverse-bridge repair and allocation. A targeted implementation may fuse or cache work, so these are upper-bound style sizes of the current code paths, not achievable costs.',
    inputs={name: __import__('hashlib').sha256((a.workload/name).read_bytes()).hexdigest()
            for name in ('objects.txt', 'edges.tsv')})
a.out.parent.mkdir(parents=True, exist_ok=True)
a.out.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
print(json.dumps(result, ensure_ascii=False))
