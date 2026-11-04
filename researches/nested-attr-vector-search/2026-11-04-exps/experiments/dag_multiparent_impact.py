#!/usr/bin/env python3
"""量化多父对查询合格集的实际影响。

对每个递归查询 A，比较两种合格集：
  DAG : ∪_{a∈A} desc_dag(a)
  TREE: ∪_{a∈A} desc_tree(a)，其中每个多父节点只保留一个 canonical parent
输出查询层面的差异比例与 item 层面的差异规模。只做集合统计，不涉及 ANN。
"""
import argparse, json
from collections import defaultdict


def load_edges(path):
    children = defaultdict(list)
    parents = defaultdict(list)
    nodes = set()
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        child, parent = (int(x) for x in line.split()[:2])
        children[parent].append(child)
        parents[child].append(parent)
        nodes.add(child)
        nodes.add(parent)
    return children, parents, nodes


def closure(roots, children):
    seen = set()
    stack = list(roots)
    while stack:
        x = stack.pop()
        if x in seen:
            continue
        seen.add(x)
        stack.extend(children.get(x, ()))
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--workload', default='work/dag-scope/workload-run1')
    ap.add_argument('--out', default='researches/nested-attr-vector-search/2026-11-04-exps/data/multiparent-impact.json')
    args = ap.parse_args()

    children, parents, nodes = load_edges(f'{args.workload}/edges.tsv')
    owner = [int(x) for x in open(f'{args.workload}/objects.txt')]
    members = defaultdict(int)
    for o in owner:
        members[o] += 1

    multi = sorted(n for n in nodes if len(parents.get(n, ())) >= 2)

    # canonical tree:每个多父节点只保留最小 ID 的父
    tree_children = defaultdict(list)
    for n in nodes:
        ps = parents.get(n, ())
        if not ps:
            continue
        tree_children[min(ps)].append(n)

    # 参照：保留最大 ID 的父，用于检查选择是否敏感
    tree_children_max = defaultdict(list)
    for n in nodes:
        ps = parents.get(n, ())
        if not ps:
            continue
        tree_children[max(ps)].append(n)

    queries = []
    for line in open(f'{args.workload}/queries.tsv'):
        parts = line.split()
        if not parts:
            continue
        cnt = int(parts[0])
        queries.append([int(x) for x in parts[1:1 + cnt]])

    def items_of(attr_set):
        return sum(members.get(a, 0) for a in attr_set)

    rows = []
    for q in queries:
        d = closure(q, children)
        t = closure(q, tree_children)
        t2 = closure(q, tree_children_max)
        miss = t - d          # 树里有、DAG 无（伪合格）
        lost = d - t          # DAG 有、树里无（漏掉）
        rows.append({
            'query': q,
            'n_dag': len(d),
            'n_tree': len(t),
            'attr_diff': len(d ^ t),
            'items_dag': items_of(d),
            'items_tree': items_of(t),
            'items_lost': items_of(lost),
            'items_spurious': items_of(miss),
            'tree_max_attr_diff': len(d ^ t2),
        })

    changed = [r for r in rows if r['items_lost'] or r['items_spurious']]
    lost_total = sum(r['items_lost'] for r in rows)
    dag_total = sum(r['items_dag'] for r in rows)
    summary = {
        'attributes': len(nodes),
        'multi_parent': len(multi),
        'queries': len(rows),
        'queries_with_item_level_diff': len(changed),
        'queries_with_item_level_diff_frac': len(changed) / max(1, len(rows)),
        'queries_with_attr_level_diff': sum(1 for r in rows if r['attr_diff']),
        'items_dag_total': dag_total,
        'items_lost_total': lost_total,
        'items_lost_frac': lost_total / max(1, dag_total),
        'canonical_choice_sensitivity': {
            'queries_diff_between_min_and_max_parent': sum(1 for r in rows if r['tree_max_attr_diff'] != r['attr_diff']),
        },
        'largest_queries': sorted(rows, key=lambda r: -r['items_lost'])[:5],
    }
    out = {'summary': summary, 'per_query': rows}
    with open(args.out, 'w') as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
    print(json.dumps(summary, indent=1, ensure_ascii=False)[:1600])


if __name__ == '__main__':
    main()
