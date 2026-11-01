#!/usr/bin/env python3
"""Emit a compact DAG scope workload from CCS 2012 and SciHTC labels.

Output (one object per labelled paper, single direct terminal label):
  edges.tsv      child<TAB>parent       (includes a synthetic CCS root)
  objects.txt    one terminal concept id per line, before the C++ ID shuffle
  queries.tsv    one query per line: "<count> <concept> ..." (scope union)
  meta.json      sizes and provenance hashes
"""
import argparse
import csv
import hashlib
import json
import random
from collections import Counter, defaultdict
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dag_ontology as dag  # noqa: E402


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ontology', type=Path, required=True)
    parser.add_argument('--labels', type=Path, nargs='+', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--antichains', type=int, default=400)
    parser.add_argument('--seed', type=int, default=20261101)
    parser.add_argument('--replicate', type=int, default=1,
                        help='duplicate every object k times to scale the bitmap size')
    args = parser.parse_args()

    names, parents, children, roots, _ = dag.load_ontology(args.ontology)
    top = [c for c in roots if names.get(c) != 'CCS']
    names = dict(names, CCS='CCS')
    children = defaultdict(set, children)
    children['CCS'] = set(top)
    parents = defaultdict(set, parents)
    for c in top:
        parents[c].add('CCS')

    ancestors = {c: dag.closure(c, parents) for c in names}
    terminals = Counter()
    for path in args.labels:
        with path.open(newline='', encoding='utf-8', errors='replace') as handle:
            for row in csv.DictReader(handle):
                category = (row.get('Category') or '').strip()
                if not category or '~' in category:
                    continue
                parts = [p.strip() for p in category.split('->')][1:]
                concept = dag.resolve_path(children, names, parts)
                if concept:
                    terminals[concept] += 1

    # Compact integer ids for concepts.
    concept_ids = {c: i for i, c in enumerate(sorted(names))}
    objects = []
    for concept in sorted(terminals, key=lambda c: concept_ids[c]):
        objects.extend([concept] * terminals[concept])
    if args.replicate > 1:
        objects = objects * args.replicate

    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / 'edges.tsv').open('w') as handle:
        for child in sorted(names):
            for parent in sorted(parents.get(child, ())):
                handle.write(f'{concept_ids[child]}\t{concept_ids[parent]}\n')
    with (args.out / 'objects.txt').open('w') as handle:
        for concept in objects:
            handle.write(f'{concept_ids[concept]}\n')

    scope = Counter()
    for concept, count in terminals.items():
        for anc in ancestors[concept]:
            scope[anc] += count
    size_of = {concept_ids[c]: scope[c] for c in names}

    # Queries: nonempty singleton scopes plus sibling samples (not necessarily antichains in a DAG).
    queries = [[concept_ids[c]] for c in sorted(scope, key=lambda c: concept_ids[c]) if scope[c]]
    rng = random.Random(args.seed)
    siblings = [sorted(children[c]) for c in names if len(children[c]) > 1]
    for _ in range(args.antichains):
        if not siblings:
            break
        group = rng.choice(siblings)
        pick = rng.sample(group, min(len(group), rng.randint(2, 4)))
        queries.append(sorted(concept_ids[c] for c in pick))
    with (args.out / 'queries.tsv').open('w') as handle:
        for query in queries:
            handle.write(f'{len(query)}\t' + '\t'.join(map(str, query)) + '\n')

    meta = {
        'objects': len(objects),
        'concepts': len(names),
        'edges': sum(len(v) for v in parents.values()),
        'multi_parent_concepts': sum(1 for c in names if len(parents.get(c, ())) > 1),
        'queries': len(queries),
        'multi_concept_queries': sum(1 for q in queries if len(q) > 1),
        'scope_size': {'min': min(size_of.values()) * args.replicate,
                       'max': max(size_of.values()) * args.replicate,
                       'mean': round(sum(size_of.values()) * args.replicate / len(size_of), 2)},
        'inputs': {'ontology': str(args.ontology), 'ontology_sha256': sha(args.ontology),
                   'labels': [str(p) for p in args.labels],
                   'labels_sha256': [sha(p) for p in args.labels]},
        'replicate': args.replicate,
        'note': 'Vectors are intentionally absent: this workload measures scope resolution only. '
                'Each paper contributes one direct terminal label; replicate>1 duplicates objects '
                'to scale the bitmap, not to add new labels.',
    }
    (args.out / 'meta.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(meta, ensure_ascii=False, indent=1))


if __name__ == '__main__':
    main()
