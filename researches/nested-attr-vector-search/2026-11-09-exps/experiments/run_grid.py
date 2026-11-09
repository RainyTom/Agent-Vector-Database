#!/usr/bin/env python3
"""Run every method on the same k and ef grid and write one CSV.

Grid: k=10, ef in {10} U {50, 100, ..., 800}. Every method uses the same
hnswlib distance function, the same M=16 / efConstruction=200, and the same
200 filtered queries, so the only difference is the scheme.
"""
import argparse
import csv
import json
import os
import subprocess

EFS = [10] + list(range(50, 801, 50))


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f'failed: {" ".join(command)}\n{result.stderr[-2000:]}')
    return [json.loads(line) for line in result.stdout.splitlines() if line.strip().startswith('{')]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--root', default='.')
    parser.add_argument('--ours-index', required=True)
    parser.add_argument('--nodeann-index', required=True)
    parser.add_argument('--acorn-index', required=True)
    parser.add_argument('--k', type=int, default=10)
    parser.add_argument('--queries', type=int, default=200)
    parser.add_argument('--methods', default='ours,nodeann,acorn')
    parser.add_argument('--taskset', default='')
    parser.add_argument('--entry-expand', type=int, default=2)
    args = parser.parse_args()

    ef_text = ','.join(str(ef) for ef in EFS)
    prefix = args.taskset.split() if args.taskset else []
    methods = [m.strip() for m in args.methods.split(',') if m.strip()]
    rows = []

    if 'ours' in methods:
        for record in run(prefix + [
                f'{args.root}/codes/NestedAttrVectorRetrieval/build/asi', 'search',
                '--data', args.data, '--index', args.ours_index,
                '--k', str(args.k), '--efs', ef_text, '--queries', str(args.queries),
                '--entry-expand', str(args.entry_expand)]):
            rows.append(dict(method='ours', ef=record['ef'], recall=record['recall'],
                             qps=record['qps'], dists=record['dists_per_q'],
                             expanded=record['expanded_per_q'],
                             attributes_entered=record['attrs_entered']))

    if 'nodeann' in methods:
        for record in run(prefix + [
                f'{args.root}/codes/NodeANN/build/nodeann', 'search',
                '--data', args.data, '--index-dir', args.nodeann_index,
                '--per-n', str(args.k), '--k', str(args.k), '--ef', ef_text,
                '--queries', str(args.queries)]):
            rows.append(dict(method=f'nodeann_n{record["per_n"]}', ef=record['ef'],
                             recall=record['recall'], qps=record['qps'],
                             dists=record['dists_per_q'], expanded='',
                             attributes_entered=record['graph_searches_per_q']))

    if 'acorn' in methods:
        for record in run(prefix + [
                f'{args.root}/codes/ACORN/build/acorn_main', 'search',
                '--data', args.data, '--index', args.acorn_index,
                '--k', str(args.k), '--efs', ef_text, '--queries', str(args.queries)]):
            rows.append(dict(method='acorn', ef=record['ef'], recall=record['recall'],
                             qps=record['qps'], dists=record['dists_per_q'],
                             expanded=record['visited_per_q'],
                             attributes_entered=''))

    keys = ['method', 'ef', 'recall', 'qps', 'dists', 'expanded', 'attributes_entered']
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        for row in sorted(rows, key=lambda r: (r['method'], int(r['ef']))):
            writer.writerow({key: row.get(key, '') for key in keys})
    print(f'wrote {args.out} with {len(rows)} rows')


if __name__ == '__main__':
    main()
