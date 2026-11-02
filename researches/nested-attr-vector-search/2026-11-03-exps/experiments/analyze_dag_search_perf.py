#!/usr/bin/env python3
"""Audit fixed-cell timings against per-query work, then emit observed ratios."""
import argparse
import csv
import json
import math
from pathlib import Path

CELLS = {(10, 32), (100, 128)}
GROUPS = ('all', 'one_owner', 'many_owners')

def load(path):
    with path.open(newline='') as f:
        return list(csv.DictReader(f))

def audit(run):
    tables = {}
    count_rows = count_batches = 0
    for sem in ('recursive', 'direct'):
        for method, arms in (('dag_merge', ('before', 'memo_rejected', 'after')), ('nodeann', ('nodeann_nk',))):
            root = run / sem / method
            rows, batches, summary = (load(root / name) for name in ('queries.csv', 'batches.csv', 'summary.csv'))
            count_rows += len(rows)
            count_batches += len(batches)
            query = {(r['arm'], int(r['k']), int(r['ef']), int(r['query'])): r for r in rows}
            assert len(query) == len(rows) == len(arms) * len(CELLS) * 120
            assert set(query) == {(a,k,e,i) for a in arms for k,e in CELLS for i in range(120)}
            table = {(r['arm'], int(r['k']), int(r['ef']), r['subset']): r for r in summary}
            assert len(table) == len(summary) == len(arms) * len(CELLS) * len(GROUPS)
            for k,e in CELLS:
                for i in range(120):
                    ref = query[arms[0],k,e,i]
                    assert int(ref['truth']) == min(k, int(ref['eligible']))
                    for a in arms:
                        r = query[a,k,e,i]
                        assert r['rejected_distances'] == '0'
                        for field in ('groups','eligible','hits','truth','returned','distances','expansions','adjacency'):
                            assert r[field] == ref[field], (sem,k,e,i,a,field)
                        assert int(r['item_checks']) <= int(ref['item_checks'])
                        assert 0 <= int(r['hits']) <= int(r['returned']) <= int(r['truth'])
                for a in arms:
                    for group in GROUPS:
                        selected = [query[a,k,e,i] for i in range(120) if int(query[a,k,e,i]['eligible']) and
                                    (group == 'all' or (int(query[a,k,e,i]['groups']) == 1) == (group == 'one_owner'))]
                        measured = [r for r in batches if r['arm'] == a and int(r['k']) == k and int(r['ef']) == e and r['subset'] == group]
                        assert len(measured) == 10 and {int(r['sample']) for r in measured} == set(range(10))
                        assert {int(r['passes']) for r in measured} == {16}
                        assert {int(r['queries']) for r in measured} == {len(selected)}
                        assert len({r['checksum'] for r in measured}) == 1
                        values = sorted(float(r['batch_us']) for r in measured)
                        assert values[0] > 0
                        expected_us = values[5] / 16 / len(selected)
                        expected_recall = sum(int(r['hits']) / int(r['truth']) for r in selected) / len(selected)
                        s = table[a,k,e,group]
                        for field, value in (('us_per_query', expected_us), ('qps', 1e6/expected_us), ('recall', expected_recall),
                                             ('distances_mean', sum(int(r['distances']) for r in selected)/len(selected)),
                                             ('item_checks_mean', sum(int(r['item_checks']) for r in selected)/len(selected))):
                            assert math.isclose(float(s[field]), value, rel_tol=1e-11, abs_tol=1e-11), (field,s,value)
            tables[sem,method] = (query,table)
        dq = tables[sem,'dag_merge'][0]
        nq = tables[sem,'nodeann'][0]
        for k,e in CELLS:
            for i in range(120):
                for field in ('groups','eligible','truth'):
                    assert dq['before',k,e,i][field] == nq['nodeann_nk',k,e,i][field]
    return tables,count_rows,count_batches

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dir', type=Path, required=True)
    args = parser.parse_args()
    root = args.dir
    out = root / 'analysis'
    out.mkdir(exist_ok=True)
    observations = []
    total_rows = total_batches = 0
    runs = (('run1',root), ('run2',root/'repeat2')) if (root/'repeat2').exists() else (('run1',root),)
    for label,run in runs:
        tables,nrows,nbatches = audit(run)
        total_rows += nrows; total_batches += nbatches
        for sem in ('recursive','direct'):
            dag = tables[sem,'dag_merge'][1]; native = tables[sem,'nodeann'][1]
            for k,e in sorted(CELLS):
                for group in GROUPS:
                    b,a,n = dag['before',k,e,group],dag['after',k,e,group],native['nodeann_nk',k,e,group]
                    before,after,nt = (float(r['us_per_query']) for r in (b,a,n))
                    row = dict(run=label,semantics=sem,k=k,ef=e,subset=group,before_us=before,after_us=after,
                               speedup=before/after,latency_drop=1-after/before,recall=float(a['recall']),
                               nodeann_nk_us=nt,nodeann_recall=float(n['recall']),after_over_nodeann=after/nt,
                               before_checks=float(b['item_checks_mean']),after_checks=float(a['item_checks_mean']))
                    observations.append(row)
                    if group == 'all': print(json.dumps(row, ensure_ascii=False))
    for method in ('dag_merge','nodeann'):
        deps = (root/'build'/f'{method}.d').read_text()
        assert f'codes/dag-composition-methods/{method}/hnswlib/hnswlib/hnswalg.h' in deps
        other = 'nodeann' if method == 'dag_merge' else 'dag_merge'
        assert f'codes/dag-composition-methods/{other}/hnswlib/' not in deps
    with (out/'comparison.csv').open('w',newline='') as f:
        writer = csv.DictWriter(f,fieldnames=list(observations[0]))
        writer.writeheader(); writer.writerows(observations)
    (out/'audit.json').write_text(json.dumps(dict(status='PASS',query_rows=total_rows,batch_rows=total_batches,
        cells=sorted(CELLS),runs=len(runs),note='NodeANN reference is fixed n=k, not a full n sweep. '
        'Ordered-ID and timed-checksum equality are enforced in C++; this audit recomputes CSV aggregates.'),indent=2)+'\n')
    print(f'PASS query_rows={total_rows} batch_rows={total_batches} independent_hnswlib=1')

if __name__ == '__main__':
    main()
