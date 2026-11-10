#!/usr/bin/env python3
"""Run the SIFT1M-CCS comparison: one build step per scheme, one search step.

Schemes are invoked as separate processes; each one owns its own index format and
writes nothing but JSON lines to stdout. This script only orchestrates them and
turns the JSON into results/sift1m-ccs/<scheme>.csv.

    python3 experiments/run_sift1m_ccs.py [--skip-build] [--only NAME,...]
"""
import argparse
import csv
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
REC = ROOT / 'datasets/sift1m-ccs-concepts-simulate'
NONREC = ROOT / 'datasets/sift1m-ccs-concepts-simulate-nonrec'
OUT = ROOT / 'results/sift1m-ccs'
EFS = '10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800'
EFS_SHORT = '10,50,100,200'
THREADS = 8
PIN = '0-7'


def run(cmd, env_extra=None):
    import os
    env = dict(os.environ)
    env['OMP_NUM_THREADS'] = str(THREADS)
    if env_extra:
        env.update(env_extra)
    if PIN:
        cmd = ['taskset', '-c', PIN] + cmd
    completed = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=ROOT)
    if completed.returncode != 0:
        sys.stderr.write(completed.stderr[-2000:])
        raise SystemExit(f'command failed: {cmd[0]} ...')
    return completed


def records(stdout):
    return [json.loads(line) for line in stdout.splitlines() if line.startswith('{')]


def write_csv(name, rows):
    OUT.mkdir(parents=True, exist_ok=True)
    with open(OUT / name, 'w', newline='') as handle:
        writer = csv.writer(handle)
        writer.writerow(['ef', 'qps', 'recall'])
        for ef, qps, recall in sorted(rows):
            writer.writerow([ef, f'{qps:.1f}', f'{recall:.6f}'])
    print(f'  wrote {name} ({len(rows)} rows)')


def build_all(force):
    idx = REC / 'idx-directory-aware'
    if force or not (idx / 'graph.bin').exists():
        print('[build] directory-aware')
        print(run([str(ROOT / 'codes/DirectoryAware/build/directory_aware'), 'build',
                   '--data', str(REC), '--index', str(idx), '--M', '32', '--efC', '40']).stdout.strip())

    idx = REC / 'idx-global'
    if force or not (idx / 'graph.bin').exists():
        print('[build] global graph (postfilter)')
        print(run([str(ROOT / 'codes/Filters/build/filters'), 'build',
                   '--data', str(REC), '--index', str(idx), '--M', '32', '--efC', '40']).stdout.strip())

    idx = REC / 'idx-oracle-subtree'
    if force or not (idx / 'members.bin').exists():
        print('[build] oracle partition (subtree)')
        print(run([str(ROOT / 'codes/OraclePartition/build/oracle_subtree'), 'build',
                   '--data', str(REC), '--index', str(idx), '--M', '32', '--efC', '40']).stdout.strip())

    idx = REC / 'idx-oracle'
    if force or not idx.exists():
        print('[build] oracle partition (per-own)')
        print(run([str(ROOT / 'codes/OraclePartition/build/nodeann'), 'build',
                   '--data', str(REC), '--index-dir', str(idx),
                   '--M', '32', '--ef-construction', '40']).stdout.strip())

    idx = REC / 'idx-acorn.bin'
    if force or not idx.exists():
        print('[build] acorn')
        print(run([str(ROOT / 'codes/acorn-hnswlib/build/acorn_main'), 'build',
                   '--data', str(REC), '--index', str(idx),
                   '--M', '32', '--efC', '40', '--gamma', '12', '--M-beta', '32']).stdout.strip())


def search_directory_aware():
    print('[search] directory-aware')
    for mode, dataset, efs in (('recursive', REC, EFS), ('nonrecursive', NONREC, EFS_SHORT)):
        result = run([str(ROOT / 'codes/DirectoryAware/build/directory_aware'), 'search',
                      '--data', str(dataset), '--index', str(REC / 'idx-directory-aware'),
                      '--mode', mode, '--efs', efs, '--k', '10'])
        write_csv(f'directory-aware-{mode}.csv',
                  [(r['ef'], r['qps'], r['recall']) for r in records(result.stdout) if r.get('stage') == 'search'])


def search_acorn():
    print('[search] acorn')
    for mode, dataset in (('recursive', REC), ('nonrecursive', NONREC)):
        result = run([str(ROOT / 'codes/acorn-hnswlib/build/acorn_main'), 'search',
                      '--data', str(dataset), '--index', str(REC / 'idx-acorn.bin'),
                      '--mode', mode, '--efs', EFS, '--k', '10', '--queries', '200', '--repeat', '2'])
        write_csv(f'acorn-{mode}.csv',
                  [(r['ef'], r['qps'], r['recall']) for r in records(result.stdout) if r.get('method') == 'acorn'])


def search_filters():
    print('[search] prefilter / postfilter')
    result = run([str(ROOT / 'codes/Filters/build/filters'), 'search',
                  '--data', str(REC), '--index', str(REC / 'idx-global'),
                  '--mode', 'prefilter', '--k', '10'])
    rows = [(0, r['qps'], r['recall']) for r in records(result.stdout) if r.get('method') == 'prefilter']
    write_csv('prefilter.csv', rows)

    result = run([str(ROOT / 'codes/Filters/build/filters'), 'search',
                  '--data', str(REC), '--index', str(REC / 'idx-global'),
                  '--mode', 'postfilter', '--efs', EFS, '--k', '10'])
    write_csv('postfilter.csv',
              [(r['ef'], r['qps'], r['recall']) for r in records(result.stdout) if r.get('method') == 'postfilter'])


def search_oracle():
    print('[search] oracle partition')
    result = run([str(ROOT / 'codes/OraclePartition/build/oracle_subtree'), 'search',
                  '--data', str(REC), '--index', str(REC / 'idx-oracle-subtree'),
                  '--efs', EFS, '--k', '10'])
    write_csv('oracle-partition-subtree.csv',
              [(r['ef'], r['qps'], r['recall']) for r in records(result.stdout) if r.get('stage') == 'search'])

    result = run([str(ROOT / 'codes/OraclePartition/build/nodeann'), 'search',
                  '--data', str(REC), '--index-dir', str(REC / 'idx-oracle'),
                  '--per-n', '1,2,5,8,10,12,15,20', '--ef', EFS, '--queries', '200', '--repeat', '2'])
    rows = []
    for r in records(result.stdout):
        if 'per_n' in r:
            rows.append((r['per_n'], r['ef'], r['qps'], r['recall']))
    OUT.mkdir(parents=True, exist_ok=True)
    with open(OUT / 'oracle-partition-per-own.csv', 'w', newline='') as handle:
        writer = csv.writer(handle)
        writer.writerow(['per_n', 'ef', 'qps', 'recall'])
        for per_n, ef, qps, recall in sorted(rows):
            writer.writerow([per_n, ef, f'{qps:.1f}', f'{recall:.6f}'])
    print(f'  wrote oracle-partition-per-own.csv ({len(rows)} rows)')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--rebuild', action='store_true')
    parser.add_argument('--only', default='')
    args = parser.parse_args()

    if not args.skip_build:
        build_all(args.rebuild)

    wanted = {name.strip() for name in args.only.split(',') if name.strip()}
    def want(name):
        return not wanted or name in wanted

    if want('directory-aware'): search_directory_aware()
    if want('acorn'): search_acorn()
    if want('filters'): search_filters()
    if want('oracle'): search_oracle()


if __name__ == '__main__':
    sys.exit(main())
