#!/usr/bin/env python3
"""Reference implementation of the down-closed cone graph (DCG).

Purpose: make the index construction and the search algorithm unambiguous, and
check the two structural claims on the fixed 12-item teaching example:

  L1  Pruning a concept whose whole subtree misses D is exact (no build
      assumptions).  Visitable set = Ant(D).
  L2  With build invariants I1 (concept-internal connectivity) and I2 (one
      bridge per concept edge), pruning to the entry-minimal cone
      E(D) = D u Anc(min D) is also sufficient: every eligible item is still
      reachable from a seed using only nodes in E.

The 2D coordinates below are SCHEMATIC. They exist only to exercise ordering,
deduplication, the k-th bound and the stop rule.  They are not real vectors and
nothing here is an ANN performance result.
"""
import argparse
import heapq
import itertools
import json
from collections import deque
from pathlib import Path

SCHEMATIC_VECTORS = {           # schematic only, see module docstring
    'r1': (0.0, 0.0), 'r2': (0.1, 0.1),
    'a1': (1.0, 0.0), 'a2': (1.1, 0.1),
    'b1': (-1.0, 0.0), 'b2': (-1.1, 0.1),
    'c1': (2.0, 0.0), 'c2': (2.1, 0.1),
    'e1': (-2.0, 0.0), 'e2': (-2.1, 0.1),
    'f1': (0.0, 1.0), 'f2': (0.1, 1.1),
}


def d2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2


class Index:
    """Nodes = items.  Edges = S (same owner) and D (direct concept child)."""

    def __init__(self, concepts, edges, items, vectors, m_bridge=1):
        self.concepts = list(concepts)
        self.items = items
        self.vectors = vectors
        self.owner = {it['name']: it['owner'] for it in items}
        self.by_owner = {c: [] for c in self.concepts}
        for it in items:
            self.by_owner[it['owner']].append(it['name'])
        self.children = {c: [] for c in self.concepts}
        self.parents = {c: [] for c in self.concepts}
        for parent, child in edges:
            self.children[parent].append(child)
            self.parents[child].append(parent)
        self.desc = {c: self._closure(c, self.children) for c in self.concepts}
        self.anc = {c: self._closure(c, self.parents) for c in self.concepts}
        self.out = {it['name']: [] for it in items}
        self._build_s_edges()
        self._build_d_bridges(m_bridge)
        self.invariants = self._check_invariants(edges)

    @staticmethod
    def _closure(start, step):
        seen, stack = set(), [start]
        while stack:
            node = stack.pop()
            if node in seen:
                continue
            seen.add(node)
            stack.extend(step[node])
        return seen

    def _build_s_edges(self):
        """I1: every concept's items form a connected S-subgraph.

        Reference build uses all pairs (n_c is 2 here).  A production build runs
        an HNSW restricted to items(c) and keeps its adjacency.
        """
        for concept, names in self.by_owner.items():
            for left, right in itertools.combinations(names, 2):
                self.out[left].append((right, 'S'))
                self.out[right].append((left, 'S'))

    def _build_d_bridges(self, m_bridge):
        """I2/I3: per concept edge, nearest pairs, directed parent -> child."""
        for parent, children in self.children.items():
            for child in children:
                pairs = sorted(
                    ((d2(self.vectors[a], self.vectors[b]), a, b)
                     for a in self.by_owner[parent] for b in self.by_owner[child]))
                for _, a, b in pairs[:m_bridge]:
                    self.out[a].append((b, 'D'))

    def _check_invariants(self, concept_edges):
        # I1: each concept's items connected through S edges
        i1 = True
        for concept, names in self.by_owner.items():
            if len(names) < 2:
                continue
            seen = {names[0]}
            stack = [names[0]]
            while stack:
                node = stack.pop()
                for target, kind in self.out[node]:
                    if kind == 'S' and target not in seen:
                        seen.add(target)
                        stack.append(target)
            i1 &= len(seen) == len(names)
        # I2: every concept edge has at least one bridge
        bridges = {(self.owner[a], self.owner[b])
                   for a in self.out for b, kind in self.out[a] if kind == 'D'}
        i2 = all((p, c) in bridges for p, c in concept_edges)
        # I3: every edge goes to the same owner or a strict descendant
        i3 = all(self.owner[b] == self.owner[a] or self.owner[b] in self.desc[self.owner[a]]
                 for a in self.out for b, _ in self.out[a])
        return {'I1_concept_internal_connected': i1,
                'I2_bridge_per_concept_edge': i2,
                'I3_edges_only_downward': i3}

    # ---------- query plan ----------

    def down_set(self, groups):
        """groups: list of concept lists; AND across groups, OR within a group."""
        result = None
        for group in groups:
            union = set()
            for concept in group:
                union |= self.desc[concept]
            result = union if result is None else result & union
        return result or set()

    def entry_cone(self, down):
        minimal = {d for d in down if not (self.anc[d] & down - {d})}
        entry = set(down)
        for m in minimal:
            entry |= self.anc[m]
        return entry, minimal

    def seeds(self, entry):
        out = []
        for concept in entry:
            if not self.by_owner[concept]:
                continue
            has_item_ancestor = any(
                self.by_owner[a] for a in self.anc[concept] if a != concept and a in entry)
            if not has_item_ancestor:
                out.extend(self.by_owner[concept])
        return out

    # ---------- search ----------

    def search(self, query_vec, groups, k, budget=None, attribute_ok=None, use_bound=True):
        down = self.down_set(groups)
        if not down:
            return {'results': [], 'down': [], 'entry': [], 'seeds': [], 'visited': []}
        entry, minimal = self.entry_cone(down)
        seeds = self.seeds(entry)
        heap = []
        for name in seeds:
            heapq.heappush(heap, (d2(query_vec, self.vectors[name]), name))
        visited = set()
        results = []            # bounded max-heap of size k, stores (-d, name)
        expansions = 0
        order = []
        while heap and (budget is None or expansions < budget):
            dist, name = heapq.heappop(heap)
            if use_bound and len(results) == k and dist > -results[0][0]:
                break
            if name in visited:
                continue
            visited.add(name)
            expansions += 1
            order.append(name)
            if self.owner[name] in down and (attribute_ok is None or attribute_ok(name)):
                if len(results) < k:
                    heapq.heappush(results, (-dist, name))
                elif dist < -results[0][0]:
                    heapq.heapreplace(results, (-dist, name))
            for target, _kind in self.out[name]:
                if self.owner[target] not in entry:      # exact prune, no distance
                    continue
                if target in visited:
                    continue
                heapq.heappush(heap, (d2(query_vec, self.vectors[target]), target))
        ranked = sorted((-d, n) for d, n in results)
        payload = {'results': [(n, round(d, 6)) for d, n in ranked],
                   'down': sorted(down), 'entry': sorted(entry), 'minimal': sorted(minimal),
                   'seeds': sorted(seeds), 'visited': order,
                   'expansions': expansions}
        return payload

    # ---------- reference ----------

    def brute_force(self, query_vec, groups, k):
        down = self.down_set(groups)
        names = [n for n in self.vectors if self.owner[n] in down]
        ranked = sorted((d2(query_vec, self.vectors[n]), n) for n in names)[:k]
        return [(n, round(d, 6)) for d, n in ranked]

    def reachable_inside_entry(self, down, entry):
        """L2 check: every eligible item reachable from seeds using only E nodes."""
        seeds = self.seeds(entry)
        seen = set(seeds)
        stack = list(seeds)
        while stack:
            node = stack.pop()
            for target, _ in self.out[node]:
                if self.owner[target] in entry and target not in seen:
                    seen.add(target)
                    stack.append(target)
        eligible = {n for n in self.vectors if self.owner[n] in down}
        return eligible <= seen, sorted(eligible - seen)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--teaching', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    data = json.loads(args.teaching.read_text())
    concepts = list(data['concept_ids'])
    edges = [(p, c) for p, c in data['edges']]
    index = Index(concepts, edges, data['items'], SCHEMATIC_VECTORS, m_bridge=1)

    queries = [
        ('recursive_A', [['A']], (1.0, 0.5), 3),
        ('recursive_B', [['B']], (-1.0, 0.5), 3),
        ('A_or_B', [['A', 'B']], (0.0, 0.5), 3),
        ('A_and_B', [['A'], ['B']], (0.0, 1.0), 3),
        ('scattered_C_or_E', [['C', 'E']], (0.0, 0.0), 3),
        ('root_R', [['R']], (1.0, 1.0), 5),
    ]

    report = {'kind': 'dcg_reference_control_flow_check_not_experiment',
              'vector_note': 'schematic 2D coordinates, not real vectors',
              'invariants': index.invariants,
              'edges': {n: [(t, k) for t, k in v] for n, v in index.out.items()},
              'queries': {}}

    for name, groups, qvec, k in queries:
        down = index.down_set(groups)
        entry, minimal = index.entry_cone(down)
        reachable, missing = index.reachable_inside_entry(down, entry)
        exact = index.search(qvec, groups, k, budget=None)
        exhaustive = index.search(qvec, groups, k, budget=None, use_bound=False)
        brute = index.brute_force(qvec, groups, k)
        # budget-limited: 1 and 2 expansions
        weak = index.search(qvec, groups, k, budget=1)
        report['queries'][name] = {
            'groups': groups,
            'down': sorted(down),
            'entry': sorted(entry),
            'minimal_in_down': sorted(minimal),
            'eligible_items': sorted(n for n in index.vectors if index.owner[n] in down),
            'pruned_concepts': sorted(set(concepts) - entry),
            'pruned_items': sorted(n for n in index.vectors if index.owner[n] not in entry),
            'L2_every_eligible_reachable_within_entry': reachable,
            'L2_missing': missing,
            'exact_search': exact,
            'brute_force': brute,
            'exact_matches_brute_force': exact['results'] == brute,
            'exhaustive_entry_search': exhaustive['results'],
            'exhaustive_expansions': exhaustive['expansions'],
            'exhaustive_matches_brute_force': exhaustive['results'] == brute,
            'budget1_results': weak['results'],
            'budget1_visited': weak['visited'],
        }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')

    print(json.dumps(report['invariants']))
    for name, row in report['queries'].items():
        print(f"{name:18s} D={len(row['down'])} E={len(row['entry'])} "
              f"pruned_items={len(row['pruned_items']):2d} "
              f"L2={row['L2_every_eligible_reachable_within_entry']} "
              f"bound==brute:{row['exact_matches_brute_force']} "
              f"exhaustive==brute:{row['exhaustive_matches_brute_force']} "
              f"visited(bound)={len(row['exact_search']['visited']):2d} "
              f"expansions(exhaustive)={row['exhaustive_expansions']:2d}")


if __name__ == '__main__':
    main()
