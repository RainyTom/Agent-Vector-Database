#!/usr/bin/env python3
"""Finite semantic evidence only; this is not an ANN implementation or benchmark."""
import argparse
import itertools
import json
from collections import deque
from pathlib import Path


def closures(n, edges):
    masks = [1 << u for u in range(n)]
    for u in reversed(range(n)):
        for a, b in edges:
            if a == u:
                masks[u] |= masks[b]
    return masks


def budget_walk(dominance):
    # A longer path contains fewer rejected vertices: FIFO discovery alone is insufficient.
    edges = {0: [1, 2], 1: [4], 2: [3], 3: [4], 4: [5], 5: [6], 6: []}
    allowed = {0, 2, 3, 6}
    queue = deque([(0, 2)])
    best = {0: 2}
    scored, expanded = {}, []
    while queue:
        u, remaining = queue.popleft()
        expanded.append([u, remaining])
        if u in allowed and u not in scored:
            scored[u] = float(u * u)  # Actual one-dimensional squared-L2, query=0.
        for v in edges[u]:
            cost = int(v not in allowed)
            if remaining < cost:
                continue
            next_remaining = remaining - cost
            if (v in best and (not dominance or next_remaining <= best[v])):
                continue
            best[v] = next_remaining
            queue.append((v, next_remaining))
    assert all(u in allowed for u in scored)
    return {'reached_qualified': sorted(scored), 'expanded_states': expanded,
            'distance_calls': len(scored), 'unqualified_distance_calls': 0}


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    dag_count = edge_checks = witness_checks = 0
    for n in range(1, 6):
        possible = list(itertools.combinations(range(n), 2))
        for mask in range(1 << len(possible)):
            edges = [edge for i, edge in enumerate(possible) if mask >> i & 1]
            desc = closures(n, edges)
            scopes = {0}
            for d in desc:
                scopes |= {s | d for s in tuple(scopes)}
            dag_count += 1
            for u in range(n):
                for v in range(n):
                    recursive_safe = all(not (scope >> u & 1) or scope >> v & 1 for scope in scopes)
                    direct_safe = all(not (scope >> u & 1) or scope >> v & 1 for scope in range(1 << n))
                    assert recursive_safe == bool(desc[u] >> v & 1)
                    assert direct_safe == (u == v)
                    edge_checks += 1
                    for w in range(n):
                        recursive_witness = all(not (scope >> u & 1 and scope >> v & 1) or scope >> w & 1
                                                for scope in scopes)
                        direct_witness = all(not (scope >> u & 1 and scope >> v & 1) or scope >> w & 1
                                             for scope in range(1 << n))
                        assert recursive_witness == bool((desc[u] | desc[v]) >> w & 1)
                        assert direct_witness == (w in (u, v))
                        witness_checks += 1
    plain, labelled = budget_walk(False), budget_walk(True)
    assert 6 not in plain['reached_qualified'] and 6 in labelled['reached_qualified']
    # Root r with three leaves a,b,c: common singleton labels do not protect a OR b.
    labels = {'p': {'r', 'a'}, 'v': {'r', 'b'}, 'w': {'r', 'c'}}
    query = {'a', 'b'}
    assert labels['p'] & labels['v'] <= labels['w']
    assert labels['p'] & query and labels['v'] & query and not labels['w'] & query
    # Pair scopes in a multi-parent DAG force at least one directed edge per pair.
    n, R = 2 * 128 + 2, 128
    pair_edges, capacity = n * (n - 1) // 2, n * R
    assert pair_edges > capacity
    result = {
        'scope': 'upper-triangular DAGs on 1..5 attributes; finite checks, not a general proof or ANN result',
        'dags': dag_count, 'edge_qualification_checks': edge_checks, 'replacement_witness_checks': witness_checks,
        'recursive_edge_safe_iff': 'target owner is descendant-or-self of source owner',
        'direct_edge_safe_iff': 'same owner',
        'recursive_OR_replacement_iff': 'w owner in descendants(u owner) OR descendants(v owner)',
        'direct_OR_replacement_iff': 'w owner equals u owner OR v owner',
        'singleton_cover_OR_counterexample': {'labels': {k: sorted(v) for k, v in labels.items()}, 'query': sorted(query)},
        'cumulative_b_2_no_reset': {'ordinary_visited': plain, 'remaining_budget_dominance': labelled},
        'pair_scope_degree_counterexample': {'items': n, 'per_item_out_cap': R,
            'required_directed_edges_lower_bound': pair_edges, 'available_edge_slots': capacity},
        'not_implemented': ['production layered graph', 'production b traversal', 'production OR-safe pruning'],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(f'PASS {dag_count} DAGs, {edge_checks} edge checks, {witness_checks} witness checks; budget-state and OR-pruning counterexamples')


if __name__ == '__main__':
    main()
