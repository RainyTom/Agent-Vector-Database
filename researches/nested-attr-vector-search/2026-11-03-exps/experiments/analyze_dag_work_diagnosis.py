#!/usr/bin/env python3
"""Audit direct-query exploration counts and the static owner-block experiment."""
import argparse
import csv
import json
import math
from pathlib import Path

CELLS = {(10,32),(100,128)}
GROUPS = ('all','one_owner','many_owners')
COUNTS = ('distances','distinct_distance_items','expansions','adjacency','upper_expansions',
          'lower_expansions','upper_adjacency','lower_adjacency','item_checks','owner_checks','skipped_entries')

def load(path):
    with path.open(newline='') as f:
        return list(csv.DictReader(f))

def selected(row, group):
    return int(row['eligible']) > 0 and (group == 'all' or
            (int(row['groups']) == 1) == (group == 'one_owner'))

def audit(root):
    result = {}
    nrows = nbatches = 0
    for method,arms in (('dag_merge',('dag_current','dag_owner_blocks')),('nodeann',('nodeann_nk',))):
        run = root/'direct'/method
        rows,batches,summaries = (load(run/name) for name in ('queries.csv','batches.csv','summary.csv'))
        nrows += len(rows); nbatches += len(batches)
        q = {(r['arm'],int(r['k']),int(r['ef']),int(r['query'])):r for r in rows}
        assert len(q) == len(rows) == len(arms)*2*120
        assert set(q) == {(a,k,e,i) for a in arms for k,e in CELLS for i in range(120)}
        s = {(r['arm'],int(r['k']),int(r['ef']),r['subset']):r for r in summaries}
        assert len(s) == len(summaries) == len(arms)*2*3
        for k,e in CELLS:
            for i in range(120):
                ref = q[arms[0],k,e,i]
                for a in arms:
                    r = q[a,k,e,i]
                    assert int(r['truth']) == min(k,int(r['eligible']))
                    assert 0 <= int(r['hits']) <= int(r['returned']) <= int(r['truth'])
                    assert int(r['rejected_distances']) == 0
                    assert int(r['distinct_distance_items']) <= min(int(r['eligible']),int(r['distances']))
                    assert int(r['expansions']) == int(r['upper_expansions'])+int(r['lower_expansions'])
                    assert int(r['adjacency']) == int(r['upper_adjacency'])+int(r['lower_adjacency'])
                    for field in ('groups','eligible','truth'):
                        assert r[field] == ref[field]
                    if method == 'dag_merge':
                        for field in ('hits','returned','distances','distinct_distance_items','expansions'):
                            assert r[field] == ref[field]
                        assert int(r['adjacency'])+int(r['skipped_entries']) == int(ref['adjacency'])
            for a in arms:
                for group in GROUPS:
                    relevant = [q[a,k,e,i] for i in range(120) if selected(q[a,k,e,i],group)]
                    b = [r for r in batches if r['arm']==a and int(r['k'])==k and int(r['ef'])==e and r['subset']==group]
                    assert len(b)==10 and {int(r['sample']) for r in b}==set(range(10))
                    assert {int(r['passes']) for r in b} == {16}
                    assert {int(r['queries']) for r in b} == {len(relevant)}
                    assert len({r['checksum'] for r in b}) == 1
                    elapsed = sorted(float(r['batch_us']) for r in b)[5]
                    assert elapsed > 0 and relevant
                    row = s[a,k,e,group]
                    values = dict(us_per_query=elapsed/16/len(relevant),qps=len(relevant)*16e6/elapsed,
                                  recall=sum(int(r['hits'])/int(r['truth']) for r in relevant)/len(relevant))
                    values.update({f:sum(int(r[f]) for r in relevant)/len(relevant) for f in COUNTS})
                    for field,value in values.items():
                        assert math.isclose(float(row[field]),value,rel_tol=1e-11,abs_tol=1e-11),(field,row,value)
        deps = (root/'build'/f'{method}.d').read_text()
        assert f'codes/dag-composition-methods/{method}/hnswlib/hnswlib/hnswalg.h' in deps
        other = 'nodeann' if method=='dag_merge' else 'dag_merge'
        assert f'codes/dag-composition-methods/{other}/hnswlib/' not in deps
        result[method] = (q,s)
    for k,e in CELLS:
        for i in range(120):
            for field in ('groups','eligible','truth'):
                assert result['dag_merge'][0]['dag_current',k,e,i][field] == result['nodeann'][0]['nodeann_nk',k,e,i][field]
    return result,nrows,nbatches

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--variant',type=Path,action='append',required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    comparisons=[]; signs=[]; total_rows=total_batches=0
    counts_reference=None
    for index,root in enumerate(args.variant,1):
        tables,nr,nb=audit(root); total_rows+=nr; total_batches+=nb
        dagq,dags=tables['dag_merge']; nativeq,natives=tables['nodeann']
        counts={key:tuple(r[f] for f in COUNTS) for key,r in dagq.items()}
        if counts_reference is None: counts_reference=counts
        else: assert counts==counts_reference, 'candidate revision changed query work counts'
        for k,e in sorted(CELLS):
            for group in GROUPS:
                d=dags['dag_current',k,e,group]; b=dags['dag_owner_blocks',k,e,group]; n=natives['nodeann_nk',k,e,group]
                row=dict(variant=index,k=k,ef=e,subset=group,queries=int(d['queries']),
                         current_us=float(d['us_per_query']),candidate_us=float(b['us_per_query']),nodeann_us=float(n['us_per_query']),
                         current_recall=float(d['recall']),nodeann_recall=float(n['recall']),
                         candidate_over_current=float(b['us_per_query'])/float(d['us_per_query']),
                         adjacency_reduction=1-float(b['adjacency'])/float(d['adjacency']),
                         first_touched_including_rejected=float(d['item_checks']),
                         nodeann_unique_scored=float(n['distinct_distance_items']))
                for field in ('distances','distinct_distance_items','expansions','adjacency','lower_expansions','lower_adjacency'):
                    row['dag_'+field]=float(d[field]); row['nodeann_'+field]=float(n[field])
                comparisons.append(row)
                if group=='all': print(json.dumps(row,ensure_ascii=False))
            # Per-query signs, not just means. Upper+lower expansions are row
            # reads; distinct_distance_items deduplicates actual scoring IDs.
            for field in ('distances','distinct_distance_items','expansions','adjacency'):
                diffs=[int(dagq['dag_current',k,e,i][field])-int(nativeq['nodeann_nk',k,e,i][field])
                       for i in range(120) if int(dagq['dag_current',k,e,i]['eligible'])]
                signs.append(dict(variant=index,k=k,ef=e,metric=field,more=sum(x>0 for x in diffs),
                                  equal=sum(x==0 for x in diffs),less=sum(x<0 for x in diffs)))
    for name,rows in (('comparison.csv',comparisons),('per-query-signs.csv',signs)):
        with (args.out/name).open('w',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    (args.out/'audit.json').write_text(json.dumps(dict(status='PASS',query_rows=total_rows,batch_rows=total_batches,
        cases=sorted(CELLS),variants=len(args.variant),semantics='direct',
        note='C++ enforces per-local-graph native ordered IDs and calls versus original native and old hook, '
             'candidate ordered IDs/D/expansions versus current, normal/count IDs, and every timed-pass checksum. '
             'Variants are implementation revisions, not independent repeated builds of one implementation.'),indent=2)+'\n')
    print(f'PASS rows={total_rows} batches={total_batches}')

if __name__=='__main__': main()
