#!/usr/bin/env python3
"""Inspect existing layout artifacts; no index build or performance rerun.

Run from the repository root. Optional argument: output JSON path.
Historical experiment labels remain unchanged for traceability.
"""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
RAW = ROOT / 'work/packed/static-sift100k-l2048-random-run1'


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    manifest_path = RAW / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    result = {
        'kind': 'existing_artifact_structure_review',
        'new_ann_runs': 0,
        'source_manifest': str(manifest_path.relative_to(ROOT)),
        'source_manifest_sha256': fingerprint(manifest_path),
        'M': manifest['M'],
        'efConstruction': manifest['efConstruction'],
        'layouts': [],
        'sources': {},
        'limitations': [
            'These block counts describe one existing dataset, not a general equality guarantee.',
            'No cache-miss counters or new performance measurements were collected.',
            'Historical labels are not names of separate ANN algorithms.',
        ],
    }
    for label in ('P-node', 'P-shuffle', 'P-dfs', 'P-dfs-aware'):
        path = RAW / 'sift100k-l2048-random/static/runs' / (label + '-ef20-rep0.summary.json')
        summary = json.loads(path.read_text())
        result['layouts'].append({
            'historical_label': label,
            'source': str(path.relative_to(ROOT)),
            'sha256': fingerprint(path),
            **{key: summary[key] for key in (
                'pack_layout', 'pack_size', 'physical_block_count',
                'active_packed_graph_count', 'eligibility_policy',
            )},
        })
    for name in ('eligibility_index.h', 'packed_index.h', 'dynamic_triad.cpp'):
        path = ROOT / 'codes/directory-aware-vector-attr-search/src' / name
        result['sources'][str(path.relative_to(ROOT))] = fingerprint(path)
    text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if len(sys.argv) > 1:
        destination = Path(sys.argv[1])
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
