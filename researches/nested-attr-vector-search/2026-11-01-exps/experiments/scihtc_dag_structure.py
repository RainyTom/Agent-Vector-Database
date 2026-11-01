#!/usr/bin/env python3
"""Measure DAG structural costs on the real CCS 2012 ontology and SciHTC labels.

No vectors, no ANN: this only measures closure sizes, per-concept scope sizes,
pre-dedup expansion cost, and duplication introduced by multi-parent concepts.
Inputs are the CCS 2012 flat ontology HTML and the SciHTC label CSVs.
"""
import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
import statistics
import sys

_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(_ROOT / 'codes/directory-aware-vector-attr-search/scripts'))
import dag_ontology  # noqa: E402

load_ontology = dag_ontology.load_ontology
closure = dag_ontology.closure
resolve_path = dag_ontology.resolve_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ontology', type=Path, required=True)
    parser.add_argument('--labels', type=Path, nargs='+', required=True)
    parser.add_argument('--out-json', type=Path, required=True)
    parser.add_argument('--out-csv', type=Path, required=True)
    args = parser.parse_args()

    names, parents, children, roots, li_count = load_ontology(args.ontology)
    # Paths start with "CCS", which the flat dump does not materialise; add it
    # as a virtual root over the real top-level categories.
    top_level = [c for c in roots if names.get(c) != 'CCS']
    if not top_level:
        raise SystemExit('no top-level concepts parsed from the ontology dump')
    names = dict(names, CCS='CCS')
    children = defaultdict(set, children)
    children['CCS'] = set(top_level)
    parents = defaultdict(set, parents)
    for concept in top_level:
        parents[concept].add('CCS')
    roots = ['CCS']

    multi_parent = {c: sorted(p) for c, p in parents.items() if len(p) > 1}

    papers, broken, unresolved = 0, 0, 0
    terminals = Counter()
    for path in args.labels:
        with path.open(newline='', encoding='utf-8', errors='replace') as handle:
            for row in csv.DictReader(handle):
                category = (row.get('Category') or '').strip()
                if not category:
                    continue
                papers += 1
                if '~' in category:
                    broken += 1
                    continue
                parts = [p.strip() for p in category.split('->')][1:]
                concept = resolve_path(children, names, parts)
                if concept is None:
                    unresolved += 1
                    continue
                terminals[concept] += 1

    n = sum(terminals.values())
    ancestors = {c: closure(c, parents) for c in names}
    descendants = {c: closure(c, children) for c in names}
    scope = Counter()
    for concept, count in terminals.items():
        for anc in ancestors[concept]:
            scope[anc] += count

    # Pre-dedup expansion cost: sum of direct postings over the descendant set.
    # Online expansion touches one posting entry per descendant concept plus one
    # bitmap entry per directly labelled object. Under single-terminal labels the
    # object term equals |S(u)|, so repeated objects are not the variable cost.
    expansion = {c: len(descendants[c]) + sum(terminals.get(d, 0) for d in descendants[c])
                 for c in names}
    posting_words = {c: sum(terminals.get(d, 0) for d in descendants[c]) for c in names}

    # Two parent branches of a multi-parent concept are not disjoint: every
    # paper under the shared child belongs to both parent scopes.
    branch_overlap = []
    for concept, ps in sorted(multi_parent.items()):
        if concept not in names or len(ps) < 2:
            continue
        scopes = [descendants[p] for p in ps]
        inter = set.intersection(*scopes)
        union = set.union(*scopes)
        inter_papers = sum(terminals.get(c, 0) for c in inter)
        union_papers = sum(terminals.get(c, 0) for c in union)
        if not union_papers:
            continue
        branch_overlap.append({
            'concept': concept, 'name': names.get(concept), 'parents': len(ps),
            'parent_names': [names.get(x, x) for x in ps],
            'scope': scope.get(concept, 0),
            'parent_scope_intersection': inter_papers,
            'parent_scope_union': union_papers,
            'overlap_ratio': round(inter_papers / union_papers, 4),
        })

    def summary(values):
        data = [v for v in values if v is not None]
        if not data:
            return {}
        ordered = sorted(data)
        pick = lambda q: ordered[min(len(ordered) - 1, int(q * (len(ordered) - 1)))]
        return {'n': len(data), 'mean': round(statistics.fmean(data), 3),
                'p50': pick(.5), 'p95': pick(.95), 'max': ordered[-1]}

    bitmap_words = max(1, (n + 63) // 64)
    wide = [c for c in names if expansion[c] > bitmap_words]
    budget_bytes = sum((n + 7) // 8 for _ in wide)

    concept_rows = []
    for c in names:
        if scope.get(c, 0) == 0 and terminals.get(c, 0) == 0:
            continue
        concept_rows.append({
            'concept': c, 'name': names.get(c, ''),
            'parents': len(parents.get(c, ())), 'descendants': len(descendants[c]),
            'direct': terminals.get(c, 0), 'scope': scope.get(c, 0),
            'expansion': expansion[c], 'posting_entries': posting_words[c],
            'objects_per_descendant': round(scope[c] / max(1, len(descendants[c])), 3),
            'materialize': int(expansion[c] > bitmap_words),
        })
    concept_rows.sort(key=lambda r: (-r['expansion'], r['concept']))
    args.out_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.out_csv.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(concept_rows[0]))
        writer.writeheader()
        writer.writerows(concept_rows)

    result = {
        'kind': 'ccs_scihtc_dag_structure_measurement',
        'inputs': {'ontology': str(args.ontology), 'labels': [str(p) for p in args.labels]},
        'new_ann_runs': 0,
        'graphs': {
            'concepts': len(names), 'edges': sum(len(v) for v in children.values()),
            'li_instances': li_count, 'multi_parent_concepts': len(multi_parent),
            'multi_parent_share': round(len(multi_parent) / len(names), 4),
            'roots': len(roots),
        },
        'labels': {'rows_read': papers, 'usable': n, 'broken_tilde_rows': broken,
                   'unresolved_paths': unresolved,
                   'papers_touching_multi_parent_concept':
                       sum(count for c, count in terminals.items()
                           if ancestors[c] & set(multi_parent)),
                   'papers_terminal_is_multi_parent':
                       sum(count for c, count in terminals.items() if c in multi_parent),
                   'mean_ancestors_per_paper': round(
                       sum(len(ancestors[c]) * count for c, count in terminals.items()) / n, 3),
                   'max_ancestors_per_paper': max(len(ancestors[c]) for c in terminals),},
        'closure': {
            'ancestors': summary(len(ancestors[c]) for c in names),
            'descendants': summary(len(descendants[c]) for c in names),
        },
        'scope': {'size': summary(scope.get(c, 0) for c in names),
                  'expansion': summary(expansion[c] for c in names),
                  'descendants': summary(len(descendants[c]) for c in names)},
        'asymmetry': {
            'update_concepts': summary(len(ancestors[c]) for c in names),
            'query_objects_per_scope': summary(scope.get(c, 0) for c in names),
            'max_scope_over_max_ancestors': round(
                max(scope.get(c, 0) for c in names) / max(len(ancestors[c]) for c in names), 1),
        },
        'materialization_rule': {
            'bitmap_words': bitmap_words,
            'concepts_selected': len(wide),
            'share_selected': round(len(wide) / len(names), 4),
            'bitmap_bytes_total': budget_bytes,
        },
        'multi_parent_branch_overlap': {
            'n': len(branch_overlap),
            'overlap_ratio': summary(r['overlap_ratio'] for r in branch_overlap),
            'top': sorted(branch_overlap, key=lambda r: -r['overlap_ratio'])[:10],
        },
        'limits': [
            'Structural measurement only: no vectors, no ANN, no query latency.',
            'Each paper is assigned its path terminal as the single direct label; multi-label objects are not modelled.',
            'Expansion counts direct-posting entries before deduplication; scope counts distinct papers.',
            'Rows containing "~" are excluded rather than repaired, so the usable total is below the full corpus.',
        ],
    }
    args.out_json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('graphs', 'labels', 'closure', 'scope', 'asymmetry',
                                             'materialization_rule')}, ensure_ascii=False, indent=1))


if __name__ == '__main__':
    main()
