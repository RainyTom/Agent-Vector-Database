#!/usr/bin/env python3
"""构建「带属性的 SIFT」模拟数据集，供层级品类 × facet 约束下的向量连接实验使用。

产物（默认写到 datasets/sift-attr/）：
  objects_A.tsv / objects_B.tsv  id  vec_index  concept  f0 f1 f2   (-1 表示该 facet 未知)
  queries.tsv                    query_id  u  n_phi  (f,v)...       φ 为空时 n_phi=0
  meta.json                      参数、种子、源文件 SHA256、统计摘要
向量本身不复制：vec_index 指向 datasets/sift-128-euclidean.hdf5 的 train 行号。

用法：
  python3 build_dataset.py --n 20000 --seed 20261105
"""
import argparse
import hashlib
import json
import os
import random
from collections import Counter, defaultdict

UNKNOWN = -1


def read_tree(path):
    lines = open(path).read().split('\n')
    n = int(lines[0])
    children = defaultdict(list)
    parent = {}
    nodes = set()
    for line in lines[1:]:
        if not line.strip():
            continue
        c, p = (int(x) for x in line.split()[:2])
        nodes.add(c)
        parent[c] = p
        if p >= 0:
            nodes.add(p)
            children[p].append(c)
    roots = [c for c, p in parent.items() if p < 0]
    return n, nodes, children, parent, roots


def depths(nodes, children, roots):
    depth = {r: 0 for r in roots}
    frontier = list(roots)
    while frontier:
        nxt = []
        for v in frontier:
            for c in children[v]:
                depth[c] = depth[v] + 1
                nxt.append(c)
        frontier = nxt
    return depth


def sha256(path, limit=None):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        if limit is None:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                h.update(chunk)
        else:
            h.update(f.read(limit))
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tree', default='datasets/scihtc/scihtc_tree.txt')
    ap.add_argument('--bindings', default='datasets/scihtc/scihtc_bindings.txt')
    ap.add_argument('--out', default='datasets/sift-attr')
    ap.add_argument('--n', type=int, default=40000, help='两侧对象总数')
    ap.add_argument('--ratio', type=float, default=0.5, help='A 侧占比')
    ap.add_argument('--seed', type=int, default=20261105)
    ap.add_argument('--n-queries', type=int, default=200)
    ap.add_argument('--facet-doms', default='8,6,12')
    ap.add_argument('--known-A', default='0.9,0.9,0.2')
    ap.add_argument('--known-B', default='0.2,0.9,0.9')
    ap.add_argument('--depth-A-max', type=int, default=4, help='A 侧可选品类最大深度')
    ap.add_argument('--depth-B-min', type=int, default=3, help='B 侧可选品类最小深度')
    ap.add_argument('--vec-space', type=int, default=1000000, help='SIFT train 行数上界')
    ap.add_argument('--min-scope-A', type=int, default=500)
    ap.add_argument('--min-scope-B', type=int, default=500)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    doms = [int(x) for x in args.facet_doms.split(',')]
    known_A = [float(x) for x in args.known_A.split(',')]
    known_B = [float(x) for x in args.known_B.split(',')]
    assert len(doms) == len(known_A) == len(known_B)

    edge_count, nodes, children, parent, roots = read_tree(args.tree)
    assert len(roots) == 1, f'期望单根，实际 {roots}'
    raw_edges = [tuple(int(x) for x in l.split()[:2])
                 for l in open(args.tree).read().split('\n')[1:] if l.strip()]
    dup_edges = len(raw_edges) - len(set(raw_edges))
    multi_parent = 0  # 单父树：parent 只有一个键
    depth = depths(nodes, children, roots)
    orphan = [c for c in nodes if c not in depth]

    counts = Counter(int(x) for x in open(args.bindings) if x.strip())
    assert set(counts) <= nodes, '绑定中出现树外概念'

    pool_A = [c for c in counts if depth[c] <= args.depth_A_max]
    pool_B = [c for c in counts if depth[c] >= args.depth_B_min]
    assert pool_A and pool_B, '深度约束后候选池为空'

    nA = int(round(args.n * args.ratio))
    nB = args.n - nA

    def sample_concept(pool):
        return rng.choices(pool, weights=[counts[c] for c in pool], k=1)[0]

    def sample_facets(known):
        row = []
        for d, dom in enumerate(doms):
            row.append(rng.randrange(dom) if rng.random() < known[d] else UNKNOWN)
        return row

    vecs = rng.sample(range(args.vec_space), nA + nB)
    rows_A, rows_B = [], []
    for i in range(nA):
        rows_A.append([i, vecs[i], sample_concept(pool_A)] + sample_facets(known_A))
    for i in range(nB):
        rows_B.append([i, vecs[nA + i], sample_concept(pool_B)] + sample_facets(known_B))

    # 查询：u 取"两侧后代里都有对象"的概念，权重=两侧较小规模；φ 只用两侧都常知的维度
    catA_cnt, catB_cnt = Counter(r[2] for r in rows_A), Counter(r[2] for r in rows_B)

    def desc_sum(u, cnt):
        tot, stack, seen = 0, [u], set()
        while stack:
            v = stack.pop()
            if v in seen:
                continue
            seen.add(v)
            tot += cnt.get(v, 0)
            stack.extend(children[v])
        return tot

    qconcepts, qweights = [], []
    for c in counts:
        na, nb = desc_sum(c, catA_cnt), desc_sum(c, catB_cnt)
        if na >= args.min_scope_A and nb >= args.min_scope_B:
            qconcepts.append(c)
            qweights.append(min(na, nb))
    assert qconcepts, '没有两侧都非空的查询根'

    shared_dims = [d for d in range(len(doms)) if min(known_A[d], known_B[d]) >= 0.5]
    queries = []
    for qid in range(args.n_queries):
        u = rng.choices(qconcepts, weights=qweights, k=1)[0]
        k_phi = rng.choices([0, 1, 2], weights=[0.2, 0.6, 0.2], k=1)[0]
        dims = rng.sample(shared_dims, min(k_phi, len(shared_dims)))
        phi = [(d, rng.randrange(doms[d])) for d in dims]
        queries.append([qid, u] + [len(phi)] + [x for d, v in phi for x in (d, v)])

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'objects_A.tsv'), 'w') as f:
        for r in rows_A:
            f.write('\t'.join(str(x) for x in r) + '\n')
    with open(os.path.join(args.out, 'objects_B.tsv'), 'w') as f:
        for r in rows_B:
            f.write('\t'.join(str(x) for x in r) + '\n')
    with open(os.path.join(args.out, 'queries.tsv'), 'w') as f:
        for r in queries:
            f.write('\t'.join(str(x) for x in r) + '\n')

    # 统计：兼容品类对的数量与块内候选上界
    by_cat_A, by_cat_B = Counter(r[2] for r in rows_A), Counter(r[2] for r in rows_B)
    bucket_B_rows = defaultdict(list)
    for r in rows_B:
        bucket_B_rows[r[2]].append(r[3:])
    anc = {}
    for v in nodes:
        chain, cur = [], v
        while cur >= 0:
            chain.append(cur)
            cur = parent.get(cur, -1)
        anc[v] = chain
    compat_pairs, cand = 0, 0
    for b, nb in by_cat_B.items():
        for a in anc[b]:                      # a 是 b 的祖先或自身
            na = by_cat_A.get(a, 0)
            if na:
                compat_pairs += 1
                cand += na * nb
    # 精确统计：同时通过品类兼容与属性兼容的 pair 数（distance 之外的全部候选）
    attr_ok = 0
    bucket_A = defaultdict(list)
    for r in rows_A:
        bucket_A[r[2]].append(r[3:])
    for b, nb in by_cat_B.items():
        for a in anc[b]:
            block = bucket_A.get(a)
            if not block:
                continue
            for br in bucket_B_rows[b]:
                for ar in block:
                    ok = True
                    for d in range(len(doms)):
                        x, y = ar[d], br[d]
                        if x != UNKNOWN and y != UNKNOWN and x != y:
                            ok = False
                            break
                    if ok:
                        attr_ok += 1

    meta = {
        'source_tree': args.tree, 'source_bindings': args.bindings,
        'tree_sha256': sha256(args.tree), 'bindings_sha256': sha256(args.bindings),
        'tree_nodes': len(nodes), 'edge_rows': edge_count, 'real_edges': len(nodes) - len(roots),
        'tree_roots': len(roots),
        'edge_duplicates': dup_edges, 'multi_parent_nodes': multi_parent,
        'unreachable_nodes': len(orphan),
        'concepts_with_objects_in_original': len(counts),
        'depth_hist': dict(sorted(Counter(depth.values()).items())),
        'n_objects': args.n, 'n_A': nA, 'n_B': nB, 'seed': args.seed,
        'facet_doms': doms, 'known_rate_A': known_A, 'known_rate_B': known_B,
        'depth_A_max': args.depth_A_max, 'depth_B_min': args.depth_B_min,
        'vec_space': args.vec_space, 'vector_source': 'datasets/sift-128-euclidean.hdf5 (train)',
        'n_queries': args.n_queries, 'min_scope': [args.min_scope_A, args.min_scope_B],
        'shared_dims_for_phi': shared_dims,
        'distinct_concepts_A': len(by_cat_A), 'distinct_concepts_B': len(by_cat_B),
        'compatible_concept_pairs': compat_pairs,
        'candidate_pair_upper_bound': cand,
        'attr_compatible_pairs': attr_ok,
        'attr_pass_rate': round(attr_ok / cand, 4) if cand else 0.0,
        'unknown_rate_A': [round(sum(1 for r in rows_A if r[3 + d] == UNKNOWN) / nA, 4) for d in range(len(doms))],
        'unknown_rate_B': [round(sum(1 for r in rows_B if r[3 + d] == UNKNOWN) / nB, 4) for d in range(len(doms))],
    }
    with open(os.path.join(args.out, 'meta.json'), 'w') as f:
        json.dump(meta, f, indent=1, ensure_ascii=False)
    print(json.dumps({k: meta[k] for k in (
        'n_A', 'n_B', 'distinct_concepts_A', 'distinct_concepts_B', 'compatible_concept_pairs',
        'candidate_pair_upper_bound', 'unknown_rate_A', 'unknown_rate_B')}, indent=1, ensure_ascii=False))


if __name__ == '__main__':
    main()
