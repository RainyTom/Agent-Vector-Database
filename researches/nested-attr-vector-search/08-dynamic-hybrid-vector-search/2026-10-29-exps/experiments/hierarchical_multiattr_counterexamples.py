#!/usr/bin/env python3
"""Exact tiny counterexamples, not an ANN performance experiment or new theorem."""
import json
from pathlib import Path


def reachable(graph, start, target, allowed):
    pending, seen = [start], set()
    while pending:
        node = pending.pop()
        if node not in allowed or node in seen:
            continue
        seen.add(node)
        pending.extend(graph[node])
    return target in seen


def main():
    # All four objects are in the same external hierarchy subtree H.
    # Each single-attribute view has a path s -> its witness -> t.
    # The conjunctive induced subgraph loses both witnesses.
    graph = {'s': ['a', 'b'], 'a': ['t'], 'b': ['t'], 't': []}
    scope = set(graph)
    red = {'s', 'a', 't'}
    large = {'s', 'b', 't'}
    conjunction = scope & red & large
    assert reachable(graph, 's', 't', scope & red)
    assert reachable(graph, 's', 't', scope & large)
    assert not reachable(graph, 's', 't', conjunction)
    assert reachable(graph, 's', 't', scope)

    # Scope refinement is laminar, but two attribute restrictions of one scope
    # can overlap without either containing the other.
    assert red & large and red - large and large - red

    # Exact top-1 of separate predicates cannot recover their conjunction.
    # Distances are squared L2 from q=0 to coordinates 1,2,3.
    distances = {'a': 1, 'b': 4, 't': 9}
    a_candidates, b_candidates = {'a', 't'}, {'b', 't'}
    top_a = min(a_candidates, key=distances.__getitem__)
    top_b = min(b_candidates, key=distances.__getitem__)
    exact_and = min(a_candidates & b_candidates, key=distances.__getitem__)
    assert exact_and not in {top_a, top_b}

    # Same eligible cardinality, different induced navigation reachability.
    connected = {'s', 'a', 't'}
    graph2 = {'s': ['a'], 'a': ['t'], 'b': [], 't': []}
    disconnected = {'s', 'b', 't'}
    assert len(connected) == len(disconnected)
    assert reachable(graph2, 's', 't', connected)
    assert not reachable(graph2, 's', 't', disconnected)

    # A fixed finite soft penalty is not universally a hard eligibility gate.
    penalty = 100
    valid_distance, invalid_distance = 1000, 0
    assert invalid_distance + penalty < valid_distance
    output = {
        'kind': 'constructive semantic counterexamples only; no ANN performance evidence',
        'same_scope_attribute_family_is_not_laminar': True,
        'single_attribute_reachability_does_not_compose_under_and': True,
        'per_attribute_exact_top1_union_misses_conjunctive_top1': True,
        'same_eligible_count_does_not_determine_reachability': True,
        'fixed_finite_penalty_is_not_universal_hard_filter': True,
        'limits': [
            'Induced-graph failure does not prove failure for algorithms allowed to traverse ineligible vertices.',
            'The per-attribute top-1 example rejects naive candidate union, not algorithms with sufficient expansion or bounds.',
            'On bounded distances, a sufficiently large verified penalty can encode hard ranking; this says nothing about ANN navigation quality.',
            'No current dataset, learned hubness, graph construction, speedup, or scientific novelty was measured.'
        ]
    }
    target = Path(__file__).resolve().parents[1] / 'data/hierarchical-multiattr-counterexamples-20261029.json'
    target.write_text(json.dumps(output, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(output, ensure_ascii=False))


if __name__ == '__main__':
    main()
