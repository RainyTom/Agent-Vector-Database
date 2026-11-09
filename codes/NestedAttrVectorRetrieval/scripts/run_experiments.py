#!/usr/bin/env python3
"""Run the experiment grid for every scheme on one unified dataset.

Steps (each is a separate process, so build and query stay decoupled):
  1. build missing indices   (asi build / nodeann build / acorn build)
  2. run the query grids     (asi search / nodeann search / acorn search)
  3. append one JSON record per measured point to <out>/data/measurements.jsonl

Dataset argument is any directory in the unified flat layout; nothing here is
specific to SIFT or to the current hierarchy.

Example:
  python3 codes/NestedAttrVectorRetrieval/scripts/run_experiments.py \
      --data datasets/sift1m/ccs-attr \
      --out researches/nested-attr-vector-search/2026-11-09-exps \
      --methods asi,nodeann,acorn --repeat 1
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BINARIES = {
    'asi': ROOT / 'codes/NestedAttrVectorRetrieval/build/asi',
    'nodeann': ROOT / 'codes/NodeANN/build/nodeann',
    'acorn': ROOT / 'codes/ACORN/build/acorn_main',
}


def run(command):
    started = time.time()
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if completed.returncode != 0:
        raise SystemExit(f'command failed ({completed.returncode}): {" ".join(map(str, command))}\n{completed.stderr[-2000:]}')
    records = [json.loads(line) for line in completed.stdout.splitlines() if line.strip().startswith('{')]
    return records, time.time() - started, completed.stderr


def build_index(method, data, index_path, extra):
    binary = BINARIES[method]
    if not binary.exists():
        raise SystemExit(f'missing binary {binary}; run the scheme build.sh first')
    if method == 'asi':
        if index_path.exists():
            return 'reused'
        command = [str(binary), 'build', '--data', str(data), '--index', str(index_path)] + extra
    elif method == 'nodeann':
        if index_path.exists() and any(index_path.iterdir()):
            return 'reused'
        index_path.mkdir(parents=True, exist_ok=True)
        command = [str(binary), 'build', '--data', str(data), '--index-dir', str(index_path)] + extra
    else:
        if index_path.exists():
            return 'reused'
        command = [str(binary), 'build', '--data', str(data), '--index', str(index_path)] + extra
    print(f'[build] {method}: {" ".join(command)}', flush=True)
    records, seconds, _ = run(command)
    print(f'[build] {method} done in {seconds:.0f}s: {records[0] if records else "no output"}', flush=True)
    return 'built'


def search(method, data, index_path, options):
    binary = BINARIES[method]
    if method == 'asi':
        command = [str(binary), 'search', '--data', str(data), '--index', str(index_path)]
    elif method == 'nodeann':
        command = [str(binary), 'search', '--data', str(data), '--index-dir', str(index_path)]
    else:
        command = [str(binary), 'search', '--data', str(data), '--index', str(index_path)]
    return run(command + options)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--data', required=True, help='unified dataset directory')
    parser.add_argument('--out', required=True, help='experiment directory (writes data/measurements.jsonl)')
    parser.add_argument('--methods', default='asi,nodeann,acorn')
    parser.add_argument('--efs', default='10,20,40,80,160,320')
    parser.add_argument('--reps-topL', default='32', help='comma separated representative queue widths')
    parser.add_argument('--per-n', default='10,20,40', help='NodeANN per-attribute widths')
    parser.add_argument('--nodeann-ef', default='', help='optional explicit ef for NodeANN; default = per_n')
    parser.add_argument('--acorn-two-hop', default='1')
    parser.add_argument('--queries', type=int, default=200)
    parser.add_argument('--k', type=int, default=10)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--index-dir', default='', help='where to keep index files (default: <data>/index)')
    parser.add_argument('--nodeann-index-dir', default='', help='NodeANN per-attribute index directory (default: <data>/idx-nodeann)')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()

    data = Path(args.data).resolve()
    out = Path(args.out).resolve()
    (out / 'data').mkdir(parents=True, exist_ok=True)
    index_root = Path(args.index_dir).resolve() if args.index_dir else data / 'index'
    index_root.mkdir(parents=True, exist_ok=True)
    ef_list = [int(x) for x in args.efs.split(',') if x]
    reps_list = [int(x) for x in args.reps_topL.split(',') if x]
    per_n_list = [int(x) for x in args.per_n.split(',') if x]
    methods = [m.strip() for m in args.methods.split(',') if m.strip()]

    records_path = out / 'data/measurements.jsonl'
    with records_path.open('w') as sink:
        for method in methods:
            if method == 'nodeann':
                index_path = Path(args.nodeann_index_dir).resolve() if args.nodeann_index_dir else data / 'idx-nodeann'
            else:
                index_path = index_root / (method + '.bin')
            if not args.skip_build:
                state = build_index(method, data, index_path, [])
                print(f'[build] {method}: {state}', flush=True)

            if method == 'asi':
                for reps in reps_list:
                    options = ['--efs', args.efs, '--reps-topL', str(reps), '--k', str(args.k),
                               '--queries', str(args.queries), '--repeat', str(args.repeat)]
                    points, seconds, _ = search(method, data, index_path, options)
                    for point in points:
                        point.update(method='NestedAttrVectorRetrieval', wall_s=round(seconds, 2))
                        sink.write(json.dumps(point) + '\n')
                        print('asi', point['ef'], point['reps_topL'], f"recall={point['recall']:.4f}",
                              f"qps={point['qps']:.0f}", flush=True)
            elif method == 'nodeann':
                for per_n in per_n_list:
                    node_ef = per_n
                    for candidate in args.nodeann_ef.split(',') if args.nodeann_ef else []:
                        options = ['--per-n', str(per_n), '--ef', candidate,
                                   '--queries', str(args.queries), '--repeat', str(args.repeat)]
                        points, seconds, _ = search(method, data, index_path, options)
                        for point in points:
                            point.update(method='NodeANN', wall_s=round(seconds, 2))
                            sink.write(json.dumps(point) + '\n')
                            print('nodeann', per_n, candidate, f"recall={point['recall']:.4f}",
                                  f"qps={point['qps']:.0f}", flush=True)
                    if args.nodeann_ef:
                        continue
                    options = ['--per-n', str(per_n), '--ef', str(node_ef),
                               '--queries', str(args.queries), '--repeat', str(args.repeat)]
                    points, seconds, _ = search(method, data, index_path, options)
                    for point in points:
                        point.update(method='NodeANN', wall_s=round(seconds, 2))
                        sink.write(json.dumps(point) + '\n')
                        print('nodeann', per_n, node_ef, f"recall={point['recall']:.4f}",
                              f"qps={point['qps']:.0f}", flush=True)
            else:
                for two_hop in [int(x) for x in args.acorn_two_hop.split(',') if x != '']:
                    options = ['--efs', args.efs, '--two-hop', str(two_hop), '--k', str(args.k),
                               '--queries', str(args.queries), '--repeat', str(args.repeat)]
                    points, seconds, _ = search(method, data, index_path, options)
                    for point in points:
                        point.update(method='ACORN', wall_s=round(seconds, 2))
                        sink.write(json.dumps(point) + '\n')
                        print('acorn', point['ef'], two_hop, f"recall={point['recall']:.4f}",
                              f"qps={point['qps']:.0f}", flush=True)
            sink.flush()

    print(f'wrote {records_path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
