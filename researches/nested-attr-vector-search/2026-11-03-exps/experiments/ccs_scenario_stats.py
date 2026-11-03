#!/usr/bin/env python3
"""Count direct vs recursive paper counts for CCS concepts used in the deck.

Source: released SciHTC label CSVs (one CCS path per paper). The per-concept
multi-parent count here comes from the label paths themselves; the index
workload (1,929 concepts / 2,040 edges / 103 multi-parent) is a separate
construction of the full CCS tree, recorded in work/dag-scope/workload-run1/meta.json.
"""
import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--src', type=Path, default=Path('/tmp/scihtc_labels/IDs_and_categories'))
ap.add_argument('--out', type=Path, required=True)
a = ap.parse_args()

paths = []
for split in ('train', 'dev', 'test'):
    with open(a.src / f'{split}_ids_all_categories.csv', encoding='utf-8') as f:
        for row in csv.DictReader(f):
            parts = [p.strip() for p in row['Category'].split('->')][1:]
            cleaned = []
            for seg in parts:
                if not seg:
                    continue
                if '~' in seg:                     # author label, not CCS
                    head = seg.split('~')[0].strip()
                    if head:
                        cleaned.append(head)
                    break
                cleaned.append(seg)
            if cleaned:
                paths.append(cleaned)

direct, recursive = Counter(), Counter()
parents, children = defaultdict(set), defaultdict(set)
for path in paths:
    direct[path[-1]] += 1
    for i, name in enumerate(path):
        recursive[name] += 1
        if i:
            parents[name].add(path[i - 1])
            children[path[i - 1]].add(name)

multi = {k: sorted(v) for k, v in parents.items() if len(v) > 1}
sample = ['Information systems', 'Information retrieval', 'Machine learning', 'Computer vision',
          'Search engine architectures and scalability', 'Document representation',
          'Retrieval models and ranking']
result = dict(
    papers=len(paths),
    concepts_in_paths=len(parents),
    multi_parent=len(multi),
    multi_parent_examples={k: multi[k] for k in list(multi)[:5]},
    concepts={c: dict(direct=direct[c], recursive=recursive[c],
                      ratio=(round(recursive[c] / direct[c], 2) if direct[c] else None),
                      children=len(children[c])) for c in sample},
    note='Counts come from label paths only; the index workload expands the full CCS tree.',
)
a.out.parent.mkdir(parents=True, exist_ok=True)
a.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(result, ensure_ascii=False))
