"""Exact finite semantic probes, not an ANN/filter performance implementation.
Run from any working directory; JSON is written beside this experiment's data/.
"""
import itertools
import json
from pathlib import Path


def run():
    results = {}
    universe = list(itertools.product((False, True), repeat=2))
    checked = 0
    # A no-false-negative may-exist filter can add positives arbitrarily.
    for mask in range(1 << len(universe)):
        block = [x for i, x in enumerate(universe) if mask & (1 << i)]
        actual_atoms = [any(x[j] for x in block) for j in range(2)]
        for advertised in itertools.product((False, True), repeat=2):
            if any(a and not p for a, p in zip(actual_atoms, advertised)):
                continue
            exact_and = any(all(x) for x in block)
            exact_or = any(any(x) for x in block)
            reject_and = not all(advertised)
            reject_or = not any(advertised)
            assert not (reject_and and exact_and)
            assert not (reject_or and exact_or)
            checked += 1
    results['safe_positive_predicate_gates'] = {'cases': checked, 'false_rejections': 0}

    crossed = [(True, False), (False, True)]
    assert all(any(x[j] for x in crossed) for j in range(2))
    assert not any(all(x) for x in crossed)
    # Even exact marginal existence does not imply a common witness.
    results['marginal_vs_same_object'] = {'marginal_positive': [True, True], 'eligible_objects': 0}
    # A positive approximate answer cannot certify all objects satisfy P,
    # and therefore cannot safely reject a query NOT P.
    block = [False]
    approximate_p = True
    assert approximate_p and any(not x for x in block)
    results['negation_is_not_filter_inversion'] = {'not_p_eligible_objects': 1, 'may_p': True}

    # Stable owner keys survive MOVE; an old DFS interval need not.
    parent = {0: None, 1: 0, 2: 0, 3: 1}
    owner = {10: 3, 11: 2}
    def descendants(root):
        found = {root}
        while True:
            expanded = found | {n for n, p in parent.items() if p in found}
            if expanded == found:
                return found
            found = expanded
    old_scope = descendants(1)
    old_owner = dict(owner)
    parent[3] = 2
    assert owner == old_owner
    assert {i for i, n in owner.items() if n in descendants(1)} == set()
    assert {i for i, n in owner.items() if n in descendants(2)} == {10, 11}
    assert {i for i, n in owner.items() if n in old_scope} == {10}
    results['move_requires_current_topology'] = {'object_key_rewrites': 0, 'stale_scope_wrong_ids': [10]}

    records = {10: (1, 7), 11: (1, 7)}
    exact_ids = {i for i, key in records.items() if key == (1, 7)}
    coordinate_set = set(records.values())
    assert len(exact_ids) == 2 and len(coordinate_set) == 1
    results['coordinate_set_is_not_object_set'] = {'objects': 2, 'distinct_coordinate_tuples': 1}

    # Top-1 is not deletion-invertible: same stored answer and deletion,
    # different required next answers. Full retained state is necessary.
    a, b = {'x': 1, 'y': 2}, {'x': 1, 'z': 3}
    first = lambda values: min(values, key=lambda i: (values[i], i))
    assert first(a) == first(b) == 'x'
    del a['x']
    del b['x']
    assert first(a) != first(b)
    results['truncated_topk_is_not_a_deletion_ring'] = {'next_a': first(a), 'next_b': first(b)}

    # Sharing a cell-local selector preserves the ID binding, unlike
    # replacing a relation by the product of its marginal projections.
    by_cell = {0: {10, 11}, 1: {12}}
    scopes = {'a': {10, 12}, 'b': {11, 12}}
    facet = {11, 12}
    local = {cell: ids & facet for cell, ids in by_cell.items()}
    for ids in scopes.values():
        lazy_union = set().union(*(ids & view for view in local.values()))
        assert lazy_union == ids & facet
    results['shared_local_views_preserve_ids'] = {'scopes_checked': len(scopes)}
    return {'kind': 'finite_semantic_probes', 'author_code_executed': False,
            'ann_experiment': False, 'performance_claim': False, 'checks': results}


if __name__ == '__main__':
    result = run()
    dest = Path(__file__).resolve().parents[1] / 'data' / 'cross-domain-semantics.json'
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
