#!/usr/bin/env python3
"""Audit real exact-G cohorts; plot QPS and recall, without inventing missing G."""
import argparse
import csv
import json
import math
from collections import Counter
from pathlib import Path


def load(path):
    with path.open(newline='') as f:
        return list(csv.DictReader(f))


def save(path, rows):
    with path.open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dir', type=Path, required=True)
    args = parser.parse_args()
    root = args.dir
    out = root/'analysis'
    out.mkdir(exist_ok=True)
    cells = ((10,32),(100,128))
    tables, query_tables = {}, {}
    combined, distribution = [], []
    query_rows = batch_rows = 0
    for sem in ('direct','recursive'):
        for method, arm in (('dag_merge','dag'),('nodeann','nodeann_nk')):
            directory = root/sem/method
            assert 'kernel=AVX dim=128' in (directory/'output.txt').read_text()
            rows, batches, summary = (load(directory/name) for name in ('queries.csv','batches.csv','summary.csv'))
            query_rows += len(rows)
            batch_rows += len(batches)
            query = {(int(r['k']),int(r['ef']),int(r['query'])):r for r in rows}
            assert len(query) == len(rows) == 240
            assert set(query) == {(k,e,i) for k,e in cells for i in range(120)}
            table = {(int(r['k']),int(r['ef']),int(r['groups'])):r for r in summary}
            assert len(table) == len(summary)
            cohorts = sorted({int(query[10,32,i]['groups']) for i in range(120)} - {0})
            assert set(table) == {(k,e,g) for k,e in cells for g in cohorts}
            assert len(batches) == len(table)*10
            for k,e in cells:
                for i in range(120):
                    r = query[k,e,i]
                    assert r['arm'] == arm and r['rejected_distances'] == '0'
                    assert int(r['truth']) == min(k,int(r['eligible']))
                    assert 0 <= int(r['hits']) <= int(r['returned']) <= int(r['truth'])
                    assert bool(int(r['groups'])) == bool(int(r['eligible']))
                    for key in ('groups','eligible'):
                        assert r[key] == query[10,32,i][key]
                for g in cohorts:
                    selected = [query[k,e,i] for i in range(120) if int(query[k,e,i]['groups']) == g]
                    measured = [r for r in batches if (int(r['k']),int(r['ef']),int(r['groups'])) == (k,e,g)]
                    assert len(measured) == 10 and {int(r['sample']) for r in measured} == set(range(10))
                    assert {int(r['passes']) for r in measured} == {16}
                    assert {int(r['queries']) for r in measured} == {len(selected)}
                    assert len({r['checksum'] for r in measured}) == 1
                    values = sorted(float(r['batch_us']) for r in measured)
                    assert values[0] > 0
                    expected_us = values[5]/16/len(selected)
                    expected_recall = sum(int(r['hits'])/int(r['truth']) for r in selected)/len(selected)
                    s = table[k,e,g]
                    for name,value in (
                        ('us_per_query',expected_us),('qps',1e6/expected_us),('recall',expected_recall),
                        ('distances_mean',sum(int(r['distances']) for r in selected)/len(selected)),
                        ('eligible_mean',sum(int(r['eligible']) for r in selected)/len(selected))):
                        assert math.isclose(float(s[name]),value,rel_tol=1e-11,abs_tol=1e-11), (sem,method,k,g,name)
                    combined.append(dict(semantics=sem,method=method,**s))
            tables[sem,method] = table
            query_tables[sem,method] = query
            deps = (root/'build'/f'{method}.d').read_text()
            assert f'codes/dag-composition-methods/{method}/hnswlib/hnswlib/hnswalg.h' in deps
            other = 'nodeann' if method == 'dag_merge' else 'dag_merge'
            assert f'codes/dag-composition-methods/{other}/' not in deps
        dag, native = (query_tables[sem,m] for m in ('dag_merge','nodeann'))
        for key in dag:
            for field in ('groups','eligible','truth'):
                assert dag[key][field] == native[key][field]
        counts = Counter(int(dag[10,32,i]['groups']) for i in range(120))
        for g,n in sorted(counts.items()):
            distribution.append(dict(semantics=sem,groups=g,queries=n,timed=int(g>0)))
    save(out/'summary.csv', combined)
    save(out/'scope-distribution.csv',distribution)
    ratios = []
    for sem in ('direct','recursive'):
        dag, native = (tables[sem,m] for m in ('dag_merge','nodeann'))
        for (k,e,g),d in sorted(dag.items()):
            n = native[k,e,g]
            ratios.append(dict(semantics=sem,k=k,ef=e,groups=g,queries=d['queries'],
                speedup=float(n['us_per_query'])/float(d['us_per_query']),
                dag_recall=d['recall'],nodeann_recall=n['recall'],
                equal_mean_recall=int(math.isclose(float(d['recall']),float(n['recall']),abs_tol=1e-12,rel_tol=0))))
    save(out/'ratios.csv',ratios)
    (out/'audit.json').write_text(json.dumps(dict(status='PASS',query_rows=query_rows,batch_rows=batch_rows,
        distance_kernel='AVX FP32 squared-L2',cases=cells,
        group_definition='Distinct eligible owners with nonempty direct item graphs; recursive includes deduplicated descendants. DAG executes one shared search, NodeANN launches G local searches.',
        timing='10 x 16 full-cohort passes; upper median; normal path without instrumentation; C++ validates ordered normal/count IDs and every timed-pass checksum.',
        limitations=['Fixed NodeANN local n=k, not Pareto','Same G does not fix eligible items, topology, difficulty or recall','Several cohorts have one query; repeated passes do not add independent queries','G=0 excluded from QPS','No direct G=5 in this workload']),ensure_ascii=False,indent=2)+'\n')

    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
    fig, axes = plt.subplots(2,2,figsize=(13,8.5),sharex='col',gridspec_kw={'height_ratios':[2,1]})
    for col,(k,e) in enumerate(cells):
        for sem,style,marker in (('direct','--','s'),('recursive','-','o')):
            for method,color,label in (('dag_merge','#1976d2','DAG'),('nodeann','#d55e00','NodeANN n=k')):
                table = tables[sem,method]
                points = sorted((g,r) for (kk,ee,g),r in table.items() if (kk,ee)==(k,e))
                x = [g for g,r in points]
                text = f'{label} / {sem.capitalize()}'
                for row,field in ((0,'qps'),(1,'recall')):
                    ax = axes[row,col]
                    ax.plot(x,[float(r[field]) for g,r in points],linestyle=style,marker=marker,
                        markersize=5,color=color,label=text,linewidth=1.5)
                    sparse = [(g,float(r[field])) for g,r in points if int(r['queries'])<3]
                    if sparse:
                        ax.scatter([p[0] for p in sparse],[p[1] for p in sparse],s=58,facecolors='none',edgecolors=color,zorder=4)
        axes[0,col].set_title(f'k={k}, efSearch={e} | M=64, efConstruction=200')
        axes[0,col].set_yscale('log')
        axes[0,col].yaxis.set_major_locator(LogLocator(base=10, subs=(1,2,5)))
        axes[0,col].yaxis.set_major_formatter(FuncFormatter(lambda y,pos:f'{y:,.0f}'))
        axes[0,col].yaxis.set_minor_formatter(NullFormatter())
        axes[0,col].set_ylabel('QPS (log scale; higher is better)')
        axes[0,col].legend(fontsize=8)
        axes[1,col].set_ylim(0,1.04)
        axes[1,col].set_ylabel('Query-average Recall')
        axes[1,col].set_xlabel('G: eligible nonempty attribute subgraphs')
        ticks = sorted({int(r['groups']) for r in distribution if int(r['groups'])>0})
        axes[1,col].set_xticks(ticks)
        axes[1,col].tick_params(axis='x',labelsize=8)
        for row in (0,1):
            axes[row,col].grid(True,alpha=.2)
    fig.suptitle('Direct / Recursive: exact-G cohorts, SIMD retained',fontsize=14)
    fig.text(.5,.015,'Hollow rings: fewer than 3 queries. Lines only connect measured G; no direct G=5 point. Different recall is not a Pareto win.',ha='center',fontsize=9)
    fig.tight_layout(rect=(0,.04,1,.96))
    fig.savefig(out/'subgraph-efficiency.png',dpi=160)
    plt.close(fig)
    print(f'PASS query_rows={query_rows} batch_rows={batch_rows}; plot={out/"subgraph-efficiency.png"}')
    for row in ratios:
        if row['groups'] in (1,2,3,5): print(json.dumps(row))

if __name__ == '__main__':
    main()
