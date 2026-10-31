#!/usr/bin/env python3
"""Synthetic algebra checks only: no ANN, dataset benchmark, or timing claim."""
import json
import math
import random
from pathlib import Path


def squared_l2(a, b):
    # Deliberately scalar analytical toy, NOT the project's performance kernel.
    return sum((x - y) ** 2 for x, y in zip(a, b))


def box_lower_bound(query, points):
    if not points:
        return math.inf
    return sum(max(min(p[j] for p in points) - q, 0,
                   q - max(p[j] for p in points)) ** 2
               for j, q in enumerate(query))


def sparse_enclosure(query, point, keep):
    support = set(sorted(range(len(point)), key=lambda j: abs(point[j]), reverse=True)[:keep])
    code = [x if j in support else 0.0 for j, x in enumerate(point)]
    residual = math.sqrt(squared_l2(point, code))
    lower = max(0.0, math.sqrt(squared_l2(query, code)) - residual) ** 2
    return lower


def main():
    rng = random.Random(20261029)
    points = [[rng.gauss(0, 1) for _ in range(8)] for _ in range(128)]
    queries = [[rng.gauss(0, 1) for _ in range(8)] for _ in range(16)]
    checks = 0
    for query in queries:
        for cutoff in (-1.0, 0.0, 1.0):
            eligible = [p for p in points if p[0] >= cutoff]
            parent_bound = box_lower_bound(query, points)
            child_bound = box_lower_bound(query, eligible)
            truth = min(squared_l2(query, p) for p in eligible)
            assert parent_bound <= child_bound + 1e-10 <= truth + 2e-10
            checks += 1
    special = [[0.0, 10.0], [10.0, 0.0]]
    global_bound = box_lower_bound([0, 0], special)
    conditioned_bound = box_lower_bound([0, 0], special[1:])
    assert global_bound == 0 and conditioned_bound == 100
    # Marginal summaries have witnesses, but their conjunction has none.
    tags = [{'red'}, {'large'}]
    assert all(any(label in t for t in tags) for label in ('red', 'large'))
    assert not any({'red', 'large'} <= t for t in tags)
    # Identity projection + top-1 WTA: an exact nearest point has no shared bit.
    query, near, far = [1, .99], [.99, 1], [3, 0]
    bit = lambda x: max(range(len(x)), key=x.__getitem__)
    assert bit(query) != bit(near) and bit(query) == bit(far)
    assert squared_l2(query, near) < squared_l2(query, far)
    # An ineligible vertex may still be a required navigation bridge.
    graph = {0: [1], 1: [2], 2: []}
    def reachable(drop):
        seen, pending = set(), [0]
        while pending:
            node = pending.pop()
            if node in seen or node in drop:
                continue
            seen.add(node)
            pending.extend(graph[node])
        return 2 in seen
    assert reachable(set()) and not reachable({1})
    envelope_checks = 0
    for query in queries:
        for point in points:
            for keep in (1, 2, 4):
                assert sparse_enclosure(query, point, keep) <= squared_l2(query, point) + 1e-9
                envelope_checks += 1
    out = {
        'kind': 'synthetic mathematical sanity checks; no performance or novelty evidence',
        'seed': 20261029,
        'conditional_box_checks': checks,
        'sparse_residual_enclosure_checks': envelope_checks,
        'conditioned_example': {'global_box_lower_bound': global_bound,
                                'eligible_box_lower_bound': conditioned_bound,
                                'true_eligible_distance_squared': 100},
        'marginal_presence_does_not_prove_conjunction': True,
        'identity_wta_zero_overlap_can_miss_nearest': True,
        'result_rejection_does_not_allow_navigation_rejection': True,
        'limits': [
            'Conditional boxes are computed by scanning eligible points here: oracle diagnostic, NOT an implemented fast index.',
            'WTA example uses the identity projection; not a reproduction or refutation of BioVSS experiments.',
            'Triangle inequality and conditional boxes are classical facts, not claimed new algorithms.',
            'No real-data pruning rate, latency, update performance, or ANN recall was measured.'
        ]
    }
    target = Path(__file__).resolve().parents[1] / 'data/pruning-representation-sanity-20261029.json'
    target.write_text(json.dumps(out, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(out, ensure_ascii=False))


if __name__ == '__main__':
    main()
