#!/usr/bin/env python3
"""Structural checks for the cone-directed graph design.

Two independent parts, neither of which involves vectors or ANN timing:

1. Teaching example (data/dag-12-item-teaching-example.json): for each demo
   query compute the eligible concept down-set D, the visitable cone Ant(D),
   and verify that every pruned concept subtree misses D.
2. Real CCS/SciHTC workload (work/dag-scope/workload-run1): distribution of
   |Ant(D)| in items, to test whether the cone is worth pruning at all.

Output: JSON evidence. No distances, no recall, no performance claims.
"""
import argparse
import json
import statistics
from collections import Counter
from pathlib import Path


def build_relations(concepts, edges):
    parents = {c: set() for c in concepts}
    children = {c: set() for c in concepts}
    for parent, child in edges:
        parents[child].add(parent)
        children[parent].add(child)

    def closure(starts, step):
        seen, stack = set(), list(starts)
        while stack:
            node = stack.pop()
            if node in seen:
                continue
            seen.add(node)
            stack.extend(step[node])
        return seen

    desc = {c: closure([c], children) for c in concepts}
    anc = {c: closure([c], parents) for c in concepts}
    return parents, children, desc, anc


def analyse(concepts, edges, owners, queries):
    parents, children, desc, anc = build_relations(concepts, edges)
    rows = []
    for name, scope in queries.items():
        down = set().union(*(desc[u] for u in scope)) if scope else set()
        ant = set().union(*(anc[d] for d in down)) if down else set()
        # Entry-minimal cone: D plus the ancestors of D's minimal elements.
        # Sound because D is down-closed, so every non-minimal element of D is
        # reachable from a minimal element using edges inside D.
        minimal = {d for d in down if not (anc[d] & down - {d})}
        entry = set().union(*(anc[m] for m in minimal)) if minimal else set()
        eligible = [c for c in concepts if c in down]
        visitable = [c for c in concepts if c in ant]
        pruned = [c for c in concepts if c not in ant]
        # Theorem check: a pruned concept's whole subtree must miss D.
        bad = [c for c in pruned if desc[c] & down]
        rows.append({
            'query': name,
            'scope': list(scope),
            'down_set': sorted(down),
            'cone_ant': sorted(ant),
            'eligible_concepts': sorted(eligible),
            'visitable_concepts': sorted(visitable),
            'pruned_concepts': sorted(pruned),
            'eligible_items': [i['name'] for i in owners['items'] if i['owner'] in down],
            'visitable_items': [i['name'] for i in owners['items'] if i['owner'] in ant],
            'pruned_items': [i['name'] for i in owners['items'] if i['owner'] not in ant],
            'minimal_in_D': sorted(minimal),
            'entry_cone': sorted(entry | down),
            'entry_cone_items': [i['name'] for i in owners['items']
                                 if i['owner'] in (entry | down)],
            'entry_pruned_concepts': sorted(c for c in concepts if c not in (entry | down)),
            'entry_pruned_items': [i['name'] for i in owners['items']
                                   if i['owner'] not in (entry | down)],
            'every_D_element_reachable_from_minimal': all(
                any(d in desc[m] for m in minimal) for d in down),
            'pruned_cone_can_still_reach_D': sorted(
                c for c in concepts
                if c not in (entry | down) and (desc[c] & down)),
            'pruned_but_reaches_D': bad,
            'prune_is_exact_for_down_edges': not bad,
        })
    return rows


def real_dag(path):
    edges = [tuple(map(int, l.split())) for l in (path / 'edges.tsv').read_text().splitlines() if l.strip()]
    owners = [int(l) for l in (path / 'objects.txt').read_text().splitlines() if l.strip()]
    queries = []
    for line in (path / 'queries.tsv').read_text().splitlines():
        if not line.strip():
            continue
        parts = list(map(int, line.split()))
        queries.append(parts[1:1 + parts[0]])

    count = 1 + max(max(e) for e in edges)
    children = [[] for _ in range(count)]
    parents = [[] for _ in range(count)]
    for child, parent in edges:
        children[parent].append(child)
        parents[child].append(parent)

    def closure(starts, step):
        seen, stack = set(), list(starts)
        while stack:
            node = stack.pop()
            if node in seen:
                continue
            seen.add(node)
            stack.extend(step[node])
        return seen

    desc = [closure([c], children) for c in range(count)]
    anc = [closure([c], parents) for c in range(count)]
    owner_count = Counter(owners)
    n = len(owners)

    rows = []
    for query in queries:
        down = set().union(*(desc[u] for u in query))
        cone = set().union(*(anc[d] for d in down))
        minimal = {d for d in down if not (anc[d] & down - {d})}
        entry = set().union(*(anc[m] for m in minimal)) if minimal else set()
        rows.append({
            'entry_cone_items': sum(owner_count[c] for c in (entry | down)),
            'entry_cone_concepts': len(entry | down),
            'scope_size': len(query),
            'down_concepts': len(down),
            'cone_concepts': len(cone),
            'eligible_items': sum(owner_count[c] for c in down),
            'visitable_items': sum(owner_count[c] for c in cone),
        })

    def q(values, p):
        values = sorted(values)
        return values[min(len(values) - 1, int(p * (len(values) - 1)))]

    visit = [r['visitable_items'] / n for r in rows]
    visit_entry = [r['entry_cone_items'] / n for r in rows]
    ratio = [r['visitable_items'] / r['eligible_items'] for r in rows if r['eligible_items']]
    return {
        'concepts': count,
        'items': n,
        'queries': len(rows),
        'concept_edges_requiring_bridges': len(edges),
        'eligible_items': {'mean': round(statistics.fmean(r['eligible_items'] for r in rows), 1),
                           'p50': q([r['eligible_items'] for r in rows], .5),
                           'p95': q([r['eligible_items'] for r in rows], .95),
                           'max': max(r['eligible_items'] for r in rows)},
        'visitable_items': {'mean': round(statistics.fmean(r['visitable_items'] for r in rows), 1),
                            'p50': q([r['visitable_items'] for r in rows], .5),
                            'p95': q([r['visitable_items'] for r in rows], .95),
                            'max': max(r['visitable_items'] for r in rows)},
        'visitable_fraction_of_items': {'mean': round(statistics.fmean(visit), 5),
                                        'p50': round(q(visit, .5), 5),
                                        'p95': round(q(visit, .95), 5),
                                        'max': round(max(visit), 5)},
        'visitable_over_eligible': {'mean': round(statistics.fmean(ratio), 2),
                                    'p50': round(q(ratio, .5), 2),
                                    'p95': round(q(ratio, .95), 2),
                                    'max': round(max(ratio), 2)},
        'queries_cone_at_least_half': round(sum(1 for v in visit if v >= .5) / len(visit), 4),
        'entry_cone_items': {'mean': round(statistics.fmean(r['entry_cone_items'] for r in rows), 1),
                             'p50': q([r['entry_cone_items'] for r in rows], .5),
                             'p95': q([r['entry_cone_items'] for r in rows], .95),
                             'max': max(r['entry_cone_items'] for r in rows)},
        'entry_cone_fraction_of_items': {'mean': round(statistics.fmean(visit_entry), 5),
                                         'p50': round(q(visit_entry, .5), 5),
                                         'p95': round(q(visit_entry, .95), 5),
                                         'max': round(max(visit_entry), 5)},
        'entry_cone_over_eligible': {'mean': round(statistics.fmean(
            r['entry_cone_items'] / r['eligible_items'] for r in rows if r['eligible_items']), 2),
            'p50': round(q([r['entry_cone_items'] / r['eligible_items'] for r in rows
                            if r['eligible_items']], .5), 2),
            'p95': round(q([r['entry_cone_items'] / r['eligible_items'] for r in rows
                            if r['eligible_items']], .95), 2)},
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--teaching', type=Path, required=True)
    parser.add_argument('--workload', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    teaching = json.loads(args.teaching.read_text())
    concepts = list(teaching['concept_ids'])
    edges = [(p, c) for p, c in teaching['edges']]
    queries = {
        'recursive_A': ['A'],
        'recursive_B': ['B'],
        'A_or_B': ['A', 'B'],
        'A_and_B': ['F'],
        'scattered_C_or_E': ['C', 'E'],
        'root_R': ['R'],
    }
    checks = analyse(concepts, edges, teaching, queries)
    _, _, desc, anc = build_relations(concepts, edges)
    inter = desc['A'] & desc['B']
    and_check = {
        'desc_A': sorted(desc['A']),
        'desc_B': sorted(desc['B']),
        'intersection': sorted(inter),
        'meet_downset_F': sorted(desc['F']),
        'intersection_equals_meet': inter == desc['F'],
        'note': 'conjunction of scopes is exact set intersection of down-sets; the shared '
                'descendant F is counted once and no item is duplicated',
    }

    result = {
        'kind': 'cone_structural_check_not_ann_experiment',
        'teaching_example': {
            'concepts': concepts,
            'edges': edges,
            'items': teaching['items'],
            'queries': checks,
            'and_intersection_check': and_check,
            'notes': [
                'A_and_B is evaluated as the exact down-set intersection, which equals the '
                'down-set of the DAG meet {F}',
                'visitable_items is the exact prune target of a down-edge-only graph',
                'no vectors or distances are involved; ordering cannot be inferred here',
            ],
        },
    }
    if args.workload and (args.workload / 'edges.tsv').exists():
        result['real_dag'] = real_dag(args.workload)
        result['real_dag']['source'] = str(args.workload)
    else:
        result['real_dag'] = {'status': 'skipped: workload not present in this checkout'}

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'teaching_example'}, ensure_ascii=False, indent=2))
    for row in checks:
        print(f"{row['query']:18s} D={len(row['down_set'])} cone={len(row['cone_ant'])} "
              f"eligible_items={len(row['eligible_items']):2d} visitable={len(row['visitable_items']):2d} "
              f"upcone={len(row['visitable_items']):2d} entry_cone={len(row['entry_cone_items']):2d} "
              f"pruned={len(row['entry_pruned_items']):2d} exact={row['prune_is_exact_for_down_edges']}")


if __name__ == '__main__':
    main()
