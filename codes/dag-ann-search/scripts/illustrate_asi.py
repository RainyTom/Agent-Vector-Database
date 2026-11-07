#!/usr/bin/env python3
"""Measure matched ASI/NodeANN, attribute missed results, render paper SVGs.

All diagnostic references come from the C++ trace (RawL2Space full scans).
No parameter points are fabricated or interpolated for tabulated claims.
"""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'researches/nested-attr-vector-search/2026-11-07-exps'
DATA = OUT / 'data'
FIG = OUT / 'figures'
WORK = ROOT / 'work/asi-evidence'


def save(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def run(command):
    start = time.time()
    p = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=True)
    text = p.stdout.strip()
    try:
        results = [json.loads(text)]
    except json.JSONDecodeError:
        results = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
    if not results:
        raise RuntimeError(f'No JSON results: {command}\n{p.stderr}')
    return dict(command=command, wall_s=time.time()-start, stderr=p.stderr,
                result=results, utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()))


def measure(args):
    DATA.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    ef_values = [20,30,40,50,80,100,200,400,800]
    N_values = [10,15,20,25,30,40,50,60,80,120]
    base = ['taskset','-c','2','./work/attr_skeleton', '--dir','work/dag-ann',
            '--work','work/dag-scope/workload-run1','--vec','recon.bin',
            '--k','50','--M','16','--efC','200','--queries','200', '--repeat','3']
    nb = ['taskset','-c','2','./work/nodeann_measured','--dir','work/dag-ann',
          '--work','work/dag-scope/workload-run1','--vec','recon.bin', '--mode','nodeann',
          '--k','50','--M','16','--efC','200','--ef','50','--queries','200','--repeat','3',
          '--index-dir','work/dag-ann/idx','--nosimd']
    runs = []
    raw_path = DATA / 'scheme-measurements.jsonl'
    with raw_path.open('w') as f:
        for outer in range(args.runs):
            # Swap method order across runs to reduce consistent warmup bias.
            jobs = [('ASI', base + ['--p','50','--ef2','16','--efs',','.join(map(str,ef_values))])]
            jobs += [('NodeANN',nb+['--N',str(n)]) for n in N_values]
            if outer % 2:
                jobs.reverse()
            for method, cmd in jobs:
                rec = dict(method=method, outer=outer, suite='pareto', **run(cmd))
                runs.append(rec); f.write(json.dumps(rec)+'\n'); f.flush()
                print(method, outer, [(r['recall'],r.get('qps',r.get('qps_avg_pass'))) for r in rec['result']],flush=True)
        for kind_main, values in [('ef2',[8,16,32,64]), ('p',[10,25,50,100])]:
            for value in values:
                cmd = base + ['--p',str(value if kind_main=='p' else 50),
                              '--ef2',str(value if kind_main=='ef2' else 16),'--efs','200']
                rec = dict(method='ASI', outer=0, suite='ablation_'+kind_main,
                           value=value, **run(cmd))
                runs.append(rec); f.write(json.dumps(rec)+'\n'); f.flush()
                print(kind_main,value,rec['result'],flush=True)
        # NodeANN search width: N is only an output budget, so ef is the real knob.
        # Coupled (ef=N<k) gives the capacity-limited curve; ef>N at N=k shows saturation.
        sweep = [(ef, 50, 'full_output') for ef in (100, 200, 50)]
        sweep += [(x, x, 'coupled') for x in (5, 10, 15, 20, 25, 32, 40)]
        for outer in range(args.runs):
            order = list(sweep)
            if outer % 2:
                order.reverse()
            for ef, perN, kind_n in order:
                rec = dict(method='NodeANN', outer=outer, suite='ef_sweep', kind=kind_n,
                           ef=ef, perN=perN, **run(nb + ['--ef',str(ef),'--N',str(perN)]))
                runs.append(rec); f.write(json.dumps(rec)+'\n'); f.flush()
                print(kind_n, ef, perN, [(r['recall'], r['qps_avg_pass'], r['dist_calls_per_q'])
                                         for r in rec['result']], flush=True)
        pf = ['taskset','-c','2','./work/nodeann_measured','--dir','work/dag-ann',
              '--work','work/dag-scope/workload-run1','--vec','recon.bin','--mode','exact',
              '--k','50','--M','16','--efC','200','--ef','50','--queries','200',
              '--repeat','3','--nosimd']
        with (DATA/'prefilter-matched.jsonl').open('w') as pf_out:
            for outer in range(args.runs):
                rec = dict(method='Pre-filter', outer=outer, suite='reference', **run(pf))
                pf_out.write(json.dumps(rec)+'\n'); pf_out.flush()
                print('Pre-filter', outer, rec['result'][0]['qps_avg_pass'], flush=True)
    paths = ['codes/dag-ann-search/src/attr_skeleton_bench.cpp',
             'codes/dag-ann-search/src/nodeann_bench.cpp',
             'codes/directory-aware-vector-attr-search/src/raw_l2_space.h',
             'codes/dag-composition-methods/nodeann/hnswlib/hnswlib/hnswalg.h',
             'codes/dag-composition-methods/nodeann/hnswlib/hnswlib/space_l2.h',
             'work/attr_skeleton','work/nodeann_measured','work/dag-ann/orig.bin',
             'work/dag-ann/recon.bin','work/dag-ann/queries.bin','work/dag-ann/gt_50.txt',
             'work/dag-scope/workload-run1/objects.txt','work/dag-scope/workload-run1/edges.tsv',
             'work/dag-scope/workload-run1/queries.tsv']
    meta = dict(cpu=2, repeats_per_process=3, independent_process_runs=args.runs,
                flags='-std=c++17 -O3 -fno-tree-vectorize -fno-tree-slp-vectorize -DNO_MANUAL_VECTORIZATION',
                distance='RawL2Space for both methods; manual vectorization disabled',
                timing='sum per-query scope/search/materialized-result time; excludes recall evaluation, oracle scans and trace writes',
                note='Affinity is not CPU exclusivity; WSL clock not locked. Graph construction/loading excluded.',
                hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    meta['compiler'] = subprocess.check_output(['g++','--version'], text=True).splitlines()[0]
    meta['python'] = sys.version
    meta['ablation_process_runs'] = 1
    index_hash = hashlib.sha256()
    for path in sorted((ROOT/'work/dag-ann/idx').glob('*.bin')):
        index_hash.update(path.name.encode()); index_hash.update(path.read_bytes())
    meta['baseline_cached_index_digest'] = index_hash.hexdigest()
    save(DATA/'scheme-protocol.json',meta)


def attribution(args):
    import numpy as np
    owner = np.loadtxt(ROOT/'work/dag-scope/workload-run1/objects.txt',dtype=np.int32)
    legacy = np.fromfile(ROOT/'work/dag-ann/gt_50.txt',dtype=np.int32).reshape(-1,50)
    rows = []
    labels = ['quantization','attribute_unentered','not_navigated',
              'rejected','entry_score_only','lost_after_admission']
    aggregate = {}
    for line in Path(args.trace).open():
        t = json.loads(line)
        F,P,R,V,I,S = [set(t[key]) for key in ['fp_top','pq_top','returned','evaluated','admitted','seed_evaluated']]
        A = set(t['entered_attrs']); touched=set(t['touched_attrs'])
        c = collections.Counter()
        for x in F-R:
            if x not in P: why='quantization'
            elif int(owner[x]) not in A: why='attribute_unentered'
            elif x not in V and x not in S: why='not_navigated'
            elif x not in I: why='rejected' if x in V else 'entry_score_only'
            else: why='lost_after_admission'
            c[why]+=1
        assert sum(c.values()) == len(F-R)
        assert R <= I
        assert F == set(legacy[t['qi']][legacy[t['qi']]>=0]), 'stored GT differs from fresh FP32 oracle'
        row = dict(ef=t['ef'],ef2=t['ef2'],p=t['p'],qi=t['qi'],gt=len(F),
                   hits=len(F&R),pq_overlap=len(F&P),pq_recovered=len((F-P)&R),
                   pq_reference_missed=len(P-R),eligible=t['eligible'],
                   entered_attrs=len(A),touched_attrs=len(touched),
                   **{k:c[k] for k in labels})
        rows.append(row)
        a=aggregate.setdefault(t['ef'],collections.Counter())
        a.update({k:row[k] for k in ['gt','hits','pq_overlap','pq_recovered','pq_reference_missed','eligible']+labels})
        a['queries']+=1
    with (DATA/'miss-attribution-per-query.jsonl').open('w') as f:
        for row in rows:f.write(json.dumps(row)+'\n')
    summaries=[]
    for ef,a in sorted(aggregate.items()):
        total=a['gt']; misses=total-a['hits']
        summaries.append(dict(ef=ef,**a,recall=a['hits']/total,
                              exact_pq_recall=a['pq_overlap']/total,
                              all_gt_percent={k:100*a[k]/total for k in labels},
                              missed_only_percent={k:100*a[k]/max(1,misses) for k in labels}))
    save(DATA/'miss-attribution.json',dict(
        definition='Mutually exclusive operational attribution among FP32 GT misses: PQ-exact displacement first, then attribute admission, distance evaluation, queue admission, final output.',
        caveat='PQ displacement and navigation can overlap; precedence is stated, not an intervention-based causal proof. PQ exact recall is not an absolute upper bound.',
        trace_file=str(Path(args.trace).resolve().relative_to(ROOT)),
        trace_sha256=hashlib.sha256(Path(args.trace).read_bytes()).hexdigest(),
        labels=labels,points=summaries))
    print(json.dumps(summaries,indent=2))


def nodeann_sweep(args):
    """Sweep the knob NodeANN actually has: HNSW search width ef.

    The earlier grid varied N (per-attribute output budget) while ef stayed at 50,
    so distance calls were constant and recall saturated at the PQ representation
    ceiling. Here ef varies with N fixed at k (full output) and with ef = N < k
    (coupled, capacity-limited).
    """
    DATA.mkdir(parents=True, exist_ok=True)
    nb = ['taskset','-c','2','./work/nodeann_measured','--dir','work/dag-ann',
          '--work','work/dag-scope/workload-run1','--vec','recon.bin','--mode','nodeann',
          '--k','50','--M','16','--efC','200','--queries','200','--repeat','3',
          '--index-dir','work/dag-ann/idx','--nosimd']
    jobs = [(ef, 50, 'full_output') for ef in (50, 100, 200)]
    jobs += [(x, x, 'coupled') for x in (5, 10, 15, 20, 25, 32, 40)]
    with (DATA/'nodeann-ef-sweep.jsonl').open('w') as f:
        for outer in range(args.runs):
            order = list(jobs)
            if outer % 2:
                order.reverse()
            for ef, perN, kind in order:
                cmd = nb + ['--ef', str(ef), '--N', str(perN)]
                rec = dict(method='NodeANN', outer=outer, suite='ef_sweep',
                           kind=kind, ef=ef, perN=perN, **run(cmd))
                f.write(json.dumps(rec) + '\n'); f.flush()
                print(kind, ef, perN, [(r['recall'], r['qps_avg_pass'], r['dist_calls_per_q'])
                                       for r in rec['result']], flush=True)


def summaries():
    groups=collections.defaultdict(list)
    ablations=collections.defaultdict(list)
    for line in (DATA/'scheme-measurements.jsonl').open():
        rec=json.loads(line)
        for r in rec['result']:
            if rec['suite']=='pareto':
                param=r['ef'] if rec['method']=='ASI' else r['perN']
                groups[rec['method'],param,r['ef'],r.get('perN',0)].append(r)
            elif rec['suite']=='ef_sweep':
                groups['NodeANN',r['perN'],r['ef'],r['perN']].append(r)
            else:ablations[rec['suite']].append(dict(value=rec['value'],**r))
    rows=[]
    for (method,param,ef,perN),rr in groups.items():
        qps=[r.get('qps',r.get('qps_avg_pass')) for r in rr]
        assert len({r['recall'] for r in rr})==1, 'recall changed across process runs'
        # Distinct results are bounded by the union of both keep heaps: ef + ef2 for
        # ASI, and the per-attribute reserve N for NodeANN.
        reserve = (ef + rr[0].get('ef2', 0)) if method=='ASI' else perN
        rows.append(dict(method=method,param=param,recall=rr[0]['recall'],
                         qps=len(qps)/sum(1/x for x in qps), qps_min=min(qps),qps_max=max(qps),
                         runs=len(rr),distances=statistics.mean(r.get('dists_per_q',r.get('dist_calls_per_q',0)) for r in rr),
                         k=50,ef=ef,perN=(perN or None),ef2=rr[0].get('ef2'),output_reserve=reserve,
                         fixed_k_capacity=reserve>=50,
                         effective_ef=max(ef,perN) if method=='NodeANN' else ef))
    return rows,ablations


def pareto(rows):
    # Actual measured non-dominated points. Equal recall retains fastest only.
    return sorted([r for r in rows if not any(
        (s['recall']>=r['recall'] and s['qps']>=r['qps']) and
        (s['recall']>r['recall'] or s['qps']>r['qps']) for s in rows)],key=lambda r:r['recall'])


def render(args):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.labelsize':10,
                         'xtick.labelsize':8,'ytick.labelsize':8,'legend.fontsize':8,
                         'axes.spines.top':False,'axes.spines.right':False,'svg.hashsalt':'asi-evidence'})
    FIG.mkdir(parents=True,exist_ok=True)
    rows,ablations=summaries()
    save(DATA/'scheme-summary.json',rows)
    full=[r for r in rows if r['fixed_k_capacity']]
    short=[r for r in rows if not r['fixed_k_capacity']]
    fronts={m:pareto([r for r in full if r['method']==m]) for m in ['NodeANN','ASI']}
    short_fronts={m:pareto([r for r in short if r['method']==m]) for m in ['NodeANN','ASI']}
    save(DATA/'pareto-selection.json',dict(
        rule='Per-method exact dominance, applied separately to full-capacity points (ASI output reserve ef+ef2 >= k; NodeANN per-attribute reserve N >= k) and capacity-limited points (reserve < k). No smoothing or extrapolation.',
        fronts=fronts,capacity_limited_frontiers=short_fronts,
        capacity_excluded=short,
        dominated=[r for r in full if r not in sum(fronts.values(),[])]))
    def export(fig,name):
        fig.savefig(FIG/(name+'.svg'),bbox_inches='tight',pad_inches=.08)
        fig.savefig(WORK/(name+'.png'),dpi=160,bbox_inches='tight',pad_inches=.08)
        plt.close(fig)
    pre_runs=[rr for rec in (json.loads(line) for line in (DATA/'prefilter-matched.jsonl').read_text().splitlines() if line.strip())
              for rr in rec['result']]
    pre_qps=[r['qps_avg_pass'] for r in pre_runs]
    pre_ref=dict(method='Pre-filter',recall=pre_runs[0]['recall'],qps=len(pre_qps)/sum(1/x for x in pre_qps),distances=pre_runs[0]['dist_calls_per_q'])
    fig,ax=plt.subplots(figsize=(7.1,3.7))
    for method,color,marker in [('NodeANN','#0072B2','o'),('ASI','#009E73','D')]:
        rr=[r for r in full if r['method']==method];ff=fronts[method]
        ax.scatter([r['recall'] for r in rr],[r['qps']/1000 for r in rr],s=13,color=color,alpha=.22)
        ax.plot([r['recall'] for r in ff],[r['qps']/1000 for r in ff],marker=marker,color=color,
                lw=1.8,ms=4.5,label='ASI (full top-50)' if method=='ASI' else 'NodeANN (full top-50)')
    for method,color,marker in [('NodeANN','#0072B2','o'),('ASI','#009E73','D')]:
        ff=short_fronts[method]
        ax.plot([r['recall'] for r in ff],[r['qps']/1000 for r in ff],marker=marker,color=color,
                lw=1.2,ls='--',ms=4,mfc='white',label='ASI (reserve < k)' if method=='ASI' else 'NodeANN (reserve < k)')
    ax.scatter([pre_ref['recall']],[pre_ref['qps']/1000],marker='*',s=100,color='#555555',label='Pre-filter exact reference',zorder=5)
    ax.set(xlabel='Recall@50 against original FP32',ylabel='Throughput (k queries/s)',xlim=(.18,.925))
    ax.grid(axis='y',color='.90',lw=.6);ax.legend(frameon=False,loc='upper right',ncol=1)
    ax.text(.02,.03,'Solid: output reserve >= 50 per attribute. Dashed: reserve < 50, recall capped by shortfall.',
            transform=ax.transAxes,ha='left',va='bottom',fontsize=7.5,color='.35')
    fig.tight_layout();export(fig,'pareto')
    # Dashboard: three ASI operating points plus NodeANN and exact scan.
    selected=[next(r for r in rows if r['method']=='ASI' and r['param']==ef) for ef in [50,200,800]]
    selected += [max((r for r in full if r['method']=='NodeANN' and r['recall']>=.9),key=lambda r:r['qps']),pre_ref]
    fig,axes=plt.subplots(1,3,figsize=(7.1,2.65),sharey=True,gridspec_kw={'wspace':.42})
    names=[f'ASI ef={r["param"]}' if r['method']=='ASI' else (f'NodeANN N={r["param"]}' if r['method']=='NodeANN' else 'Pre-filter exact') for r in selected]
    for ax,key,title,fmt in zip(axes,['recall','qps','distances'],['Recall@50 ↑','QPS ↑','Distances / query ↓'],['.4f',',.0f',',.0f']):
        vals=[r[key] for r in selected];best=min(vals) if key=='distances' else max(vals)
        ax.barh(np.arange(len(vals)),vals,height=.57,color=['#0072B2' if v==best else '#9AA7AE' for v in vals])
        for y,v in enumerate(vals):ax.text(v+max(vals)*.025,y,format(v,fmt),va='center',fontsize=8,fontweight='bold' if v==best else 'normal')
        ax.set_title(title,fontsize=10);ax.set_xlim(0,max(vals)*1.34);ax.set_xticks([])
        for sp in ax.spines.values():sp.set_visible(False)
        ax.tick_params(axis='y',length=0)
    axes[0].set_yticks(range(len(names)),names);axes[0].invert_yaxis()
    fig.text(.12,.01,'Each metric uses an independent scale; exact scan is a reference point.',fontsize=8,color='.4')
    fig.subplots_adjust(bottom=.16,left=.2,right=.97,top=.84);export(fig,'metric-overview')
    att=json.loads((DATA/'miss-attribution.json').read_text());points=att['points']
    fig,ax=plt.subplots(figsize=(7.1,3.4));bottom=np.zeros(len(points))
    categories=[('quantization','PQ rank displacement','#0072B2'),('attribute_unentered','Attribute not admitted','#E69F00'),
                ('not_navigated','Item not navigated','#009E73'),('rejected','Queue rejection','#D55E00'),
                ('entry_score_only','Entry scoring only','#CC79A7'),('lost_after_admission','Lost after admission','#999999')]
    for key,label,color in categories:
        vals=np.array([p['all_gt_percent'][key] for p in points])
        ax.bar(range(len(points)),vals,bottom=bottom,width=.57,color=color,label=label)
        for j,v in enumerate(vals):
            if v>=.6:
                ax.text(j,bottom[j]+v/2,f'{v:.2f}%',ha='center',va='center',fontsize=8,color='white' if key=='quantization' else 'black')
            elif v>0:
                ax.annotate(f'{v:.2f}%',xy=(j+.285,bottom[j]+v/2),xytext=(9,0),textcoords='offset points',va='center',fontsize=8)
        bottom+=vals
    for j,v in enumerate(bottom):ax.text(j,v+.15,f'Total {v:.2f}%',ha='center',fontsize=8)
    ax.set_xticks(range(len(points)),[f'ef={p["ef"]}' for p in points]);ax.set_ylabel('Misses (% of all FP32 ground-truth pairs)')
    ax.set_ylim(0,max(bottom)*1.22);ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True)
    ax.legend(frameon=False,ncol=2,fontsize=7,loc='upper center',bbox_to_anchor=(.5,-.13))
    fig.tight_layout();export(fig,'miss-attribution')
    for key,xlabel in [('ef2','Representative reserve ef2'),('p','Sampling denominator p')]:
        aa=sorted(ablations['ablation_'+key],key=lambda r:r['value'])
        fig,axes=plt.subplots(1,2,figsize=(7.1,2.85))
        for ax,y,title,color in zip(axes,['recall','qps'],['Recall@50','Throughput (k queries/s)'],['#009E73','#0072B2']):
            vals=[r[y]/(1000 if y=='qps' else 1) for r in aa]
            ax.plot([r['value'] for r in aa],vals,'o-',color=color,lw=1.5,ms=4)
            ax.set(xlabel=xlabel,ylabel=title);ax.set_xticks([r['value'] for r in aa]);ax.grid(alpha=.2)
        fig.suptitle('ef=200; '+('p=50' if key=='ef2' else 'ef2=16'),fontsize=9)
        fig.tight_layout();export(fig,'ablation-'+key)
    print(json.dumps(dict(frontiers=fronts,attribution=points),indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=['measure','attribute','render','nodeann-sweep'])
    ap.add_argument('--runs',type=int,default=3)
    ap.add_argument('--trace',default=str(WORK/'trace.jsonl'))
    args=ap.parse_args()
    {'measure':measure,'attribute':attribution,'render':render,
     'nodeann-sweep':nodeann_sweep}[args.action](args)
