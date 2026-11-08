#!/usr/bin/env python3
"""Measure matched ASI/NodeANN, attribute missed results, render paper SVGs.

All diagnostic references come from the C++ trace (RawL2Space full scans).
No parameter points are fabricated or interpolated for tabulated claims.
"""

# SVG labels must be paths: embedded source retains editable Chinese diagram text.
# The conversion helper also gives the matplotlib exports native title/desc nodes.
FIGURE_ACCESSIBILITY = {
    'pareto': (
        'NodeANN and Ours (ASI): measured recall-throughput frontier',
        'Measured non-dominated configurations at k=10, with recall against original FP32 and throughput in thousands of queries per second. Legend: NodeANN; Ours (ASI).'),
    'metric-overview': (
        'Recall, throughput and distance evaluations at k=10',
        'Three ASI configurations, NodeANN and exact pre-filter scan; each metric has an independent horizontal scale. Values come from retained top-10 measurement records.'),
    'miss-attribution': (
        'Operational attribution of missed FP32 top-10 results',
        'Stacked percentages of all FP32 ground-truth pairs by PQ rank displacement, attribute admission, navigation, queue admission and final output; precedence is operational, not a causal intervention.'),
    'ablation-ef2': (
        'ASI representative reserve ablation at k=10',
        'Measured recall and throughput against representative reserve ef2 with the other displayed parameters fixed.'),
    'ablation-p': (
        'ASI sampling denominator ablation at k=10',
        'Measured recall and throughput against sampling denominator p with the other displayed parameters fixed.'),
}
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
ROI = ROOT / 'researches/nested-attr-vector-search'
OUT = ROI / '2026-11-08-exps'
DATA = OUT / 'data'
FIG = OUT / 'figures'
WORK = ROOT / 'datasets/ccs-sift-dag/runs'
K = 10


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
    # Search and output-budget grids for the retained top-10 experiment.
    ef_values = [5,10,15,20,30,40,60,100,200,400]
    N_values = [4,6,8,10,15,20,30,40,60]
    sweep_full = [20,40,80]
    sweep_coupled = [2,4,6,8,10,15,20,30]
    base = ['taskset','-c','2','./codes/NestedAttrVectorRetrieval/build/attr_skeleton', '--dir','datasets/ccs-sift-dag',
            '--work','datasets/ccs-sift-dag','--vec','recon.bin',
            '--k',str(K),'--M','16','--efC','200','--queries','200', '--repeat','3']
    nb = ['taskset','-c','2','./codes/NestedAttrVectorRetrieval/build/nodeann_measured','--dir','datasets/ccs-sift-dag',
          '--work','datasets/ccs-sift-dag','--vec','recon.bin', '--mode','nodeann',
          '--k',str(K),'--M','16','--efC','200','--ef',str(K),'--queries','200','--repeat','3',
          '--index-dir','datasets/ccs-sift-dag/idx','--nosimd']
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
        ab_ef = 40
        for kind_main, values in [('ef2',[8,16,32,64]), ('p',[10,25,50,100])]:
            for value in values:
                cmd = base + ['--p',str(value if kind_main=='p' else 50),
                              '--ef2',str(value if kind_main=='ef2' else 16),'--efs',str(ab_ef)]
                rec = dict(method='ASI', outer=0, suite='ablation_'+kind_main, ef=ab_ef,
                           value=value, **run(cmd))
                runs.append(rec); f.write(json.dumps(rec)+'\n'); f.flush()
                print(kind_main,value,rec['result'],flush=True)
        # NodeANN search width: N is only an output budget, so ef is the real knob.
        # Coupled (ef=N<k) gives the capacity-limited curve; ef>N at N=k shows saturation.
        sweep = [(ef, K, 'full_output') for ef in sweep_full + [K]]
        sweep += [(x, x, 'coupled') for x in sweep_coupled]
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
        pf = ['taskset','-c','2','./codes/NestedAttrVectorRetrieval/build/nodeann_measured','--dir','datasets/ccs-sift-dag',
              '--work','datasets/ccs-sift-dag','--vec','recon.bin','--mode','exact',
              '--k',str(K),'--M','16','--efC','200','--ef',str(K),'--queries','200',
              '--repeat','3','--nosimd']
        with (DATA/'prefilter-matched.jsonl').open('w') as pf_out:
            for outer in range(args.runs):
                rec = dict(method='Pre-filter', outer=outer, suite='reference', **run(pf))
                pf_out.write(json.dumps(rec)+'\n'); pf_out.flush()
                print('Pre-filter', outer, rec['result'][0]['qps_avg_pass'], flush=True)
    paths = ['codes/NestedAttrVectorRetrieval/src/attr_skeleton_bench.cpp',
             'codes/NestedAttrVectorRetrieval/src/nodeann_bench.cpp',
             'codes/NestedAttrVectorRetrieval/src/raw_l2_space.h',
             'codes/NestedAttrVectorRetrieval/third_party/hnswlib/hnswalg.h',
             'codes/NestedAttrVectorRetrieval/third_party/hnswlib/space_l2.h',
             'codes/NestedAttrVectorRetrieval/build/attr_skeleton','codes/NestedAttrVectorRetrieval/build/nodeann_measured','datasets/ccs-sift-dag/orig.bin',
             'datasets/ccs-sift-dag/recon.bin','datasets/ccs-sift-dag/queries.bin',f'datasets/ccs-sift-dag/gt_{K}.txt',
             'datasets/ccs-sift-dag/objects.txt','datasets/ccs-sift-dag/edges.tsv',
             'datasets/ccs-sift-dag/queries.tsv']
    meta = dict(k=K, cpu=2, repeats_per_process=3, independent_process_runs=args.runs,
                flags='-std=c++17 -O3 -fno-tree-vectorize -fno-tree-slp-vectorize -DNO_MANUAL_VECTORIZATION',
                distance='RawL2Space for both methods; manual vectorization disabled',
                timing='sum per-query scope/search/materialized-result time; excludes recall evaluation, oracle scans and trace writes',
                note='Affinity is not CPU exclusivity; WSL clock not locked. Graph construction/loading excluded.',
                hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    meta['compiler'] = subprocess.check_output(['g++','--version'], text=True).splitlines()[0]
    meta['python'] = sys.version
    meta['ablation_process_runs'] = 1
    index_hash = hashlib.sha256()
    for path in sorted((ROOT/'datasets/ccs-sift-dag/idx').glob('*.bin')):
        index_hash.update(path.name.encode()); index_hash.update(path.read_bytes())
    meta['baseline_cached_index_digest'] = index_hash.hexdigest()
    save(DATA/'scheme-protocol.json',meta)


def measure_entry(args):
    """Append an entry-only control batch without replacing the original Pareto."""
    DATA.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    index = WORK / 'navigation-index'
    index.mkdir(exist_ok=True)
    base = ['taskset','-c','2','./codes/NestedAttrVectorRetrieval/build/attr_skeleton','--dir','datasets/ccs-sift-dag',
            '--work','datasets/ccs-sift-dag','--vec','recon.bin','--k',str(K),
            '--M','16','--efC','200','--queries','200','--repeat','100',
            '--p','50','--ef2','16','--efs','5,10,20,40,100,200,400']
    node = ['taskset','-c','2','./codes/NestedAttrVectorRetrieval/build/nodeann_measured','--dir','datasets/ccs-sift-dag',
            '--work','datasets/ccs-sift-dag','--vec','recon.bin','--k',str(K),
            '--mode','nodeann','--M','16','--efC','200','--queries','200',
            '--repeat','100','--nosimd','--index-dir',str(index)]
    jobs = [('representative',base+['--entry-mode','representative']),
            ('hnsw',base+['--entry-mode','hnsw'])]
    jobs += [('NodeANN',node+['--ef',str(n),'--N',str(n)]) for n in [5,10,15,20]]
    batch = time.time_ns()
    with (DATA/'navigation-measurements.jsonl').open('a') as output:
        for outer in range(args.runs):
            order = jobs[outer % len(jobs):] + jobs[:outer % len(jobs)]
            for variant, command in order:
                record = dict(batch=batch, outer=outer, variant=variant,
                              suite='entry_control', **run(command))
                output.write(json.dumps(record)+'\n')
                output.flush()
                print(batch,outer,variant,
                      [(r.get('ef'),r['recall'],r.get('qps',r.get('qps_avg_pass')))
                       for r in record['result']],flush=True)


def render_entry(args):
    """Render exactly two measured frontiers from one complete control batch."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from svg_labels import self_contained_svg
    records = [json.loads(line) for line in (DATA/'navigation-measurements.jsonl').read_text().splitlines() if line.strip()]
    batch = max(record['batch'] for record in records)
    groups = collections.defaultdict(list)
    for record in records:
        if record['batch'] != batch or record['variant'] not in ('hnsw','NodeANN'):
            continue
        for point in record['result']:
            groups[(record['variant'],point['ef'],point.get('perN'))].append(point)
    rows = []
    for (variant, ef, per_n), points in groups.items():
        if len(points) != 3 or len({point['recall'] for point in points}) != 1:
            raise ValueError('Entry frontier requires three matching process groups per point')
        qps = [point.get('qps',point.get('qps_avg_pass')) for point in points]
        rows.append(dict(method='NodeANN' if variant=='NodeANN' else 'Ours',
                         entry_mode='hnsw',ef=ef,perN=per_n,recall=points[0]['recall'],
                         qps=len(qps)/sum(1/value for value in qps)))
    if {row['method'] for row in rows} != {'NodeANN','Ours'}:
        raise ValueError('Both measured methods are required')
    fronts = {method:pareto([row for row in rows if row['method']==method])
              for method in ['NodeANN','Ours']}
    save(DATA/'navigation-pareto-selection.json',dict(
        batch=batch,entry_mode='hnsw',fronts=fronts,
        rule='Exact per-method measured dominance; the 2% practical tolerance is not used to move points or redefine dominance.'))
    matplotlib.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.labelsize':10,
                               'xtick.labelsize':8,'ytick.labelsize':8,'legend.fontsize':8,
                               'axes.spines.top':False,'axes.spines.right':False,
                               'svg.hashsalt':'asi-entry-control','svg.fonttype':'path'})
    fig, ax = plt.subplots(figsize=(7.1,3.7))
    for method, color, marker, label in [('NodeANN','#0072B2','o','NodeANN'),
                                       ('Ours','#009E73','D','Ours (ASI)')]:
        points = fronts[method]
        ax.plot([row['recall'] for row in points],[row['qps']/1000 for row in points],
                color=color,marker=marker,label=label,lw=1.5,ms=4)
    ax.set(xlabel=f'Recall@{K} against original FP32',
           ylabel='Throughput (k queries/s)',ylim=(0,None),
           title='Same native graphs; ASI entry_mode=hnsw')
    ax.grid(alpha=.2); ax.legend(frameon=False)
    fig.tight_layout()
    target = FIG/'entry-control'
    target.mkdir(parents=True,exist_ok=True); WORK.mkdir(parents=True,exist_ok=True)
    fig.savefig(target/'pareto.svg',bbox_inches='tight',pad_inches=.08)
    self_contained_svg(target/'pareto.svg',
                       'NodeANN and Ours (ASI): same-native-graph entry control',
                       'Exactly two measured top-10 recall-throughput frontiers. Ours uses HNSW upper-layer entry and shared DQS; NodeANN searches each local HNSW. Three process groups, 100 passes per point; lines are not interpolation evidence. The 2% tolerance applies only to separate tabular comparisons.')
    fig.savefig(WORK/'navigation-pareto.png',dpi=160,bbox_inches='tight',pad_inches=.08)
    plt.close(fig)
    print(json.dumps(dict(batch=batch,frontiers=fronts),indent=2))



def attribution(args):
    import numpy as np
    owner = np.loadtxt(ROOT/'datasets/ccs-sift-dag/objects.txt',dtype=np.int32)
    legacy = np.fromfile(ROOT/f'datasets/ccs-sift-dag/gt_{K}.txt',dtype=np.int32).reshape(-1,K)
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
                         k=K,ef=ef,perN=(perN or None),ef2=rr[0].get('ef2'),output_reserve=reserve,
                         fixed_k_capacity=reserve>=K,
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
    from svg_labels import self_contained_svg
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.labelsize':10,
                         'xtick.labelsize':8,'ytick.labelsize':8,'legend.fontsize':8,
                         'axes.spines.top':False,'axes.spines.right':False,'svg.hashsalt':'asi-evidence',
                         'svg.fonttype':'path'})
    FIG.mkdir(parents=True,exist_ok=True)
    WORK.mkdir(parents=True,exist_ok=True)
    rows,ablations=summaries()
    save(DATA/'scheme-summary.json',rows)
    full=[r for r in rows if r['fixed_k_capacity']]
    short=[r for r in rows if not r['fixed_k_capacity']]
    # One frontier per method over every measured point; capacity is annotated, not split.
    fronts={m:pareto([r for r in rows if r['method']==m]) for m in ['NodeANN','ASI']}
    save(DATA/'pareto-selection.json',dict(
        rule='Per-method exact dominance over all measured configurations (capacity-limited ones included); no smoothing or extrapolation. Points with output reserve < k are marked capacity_limited for annotation.',
        fronts=fronts,capacity_limited=[r for r in rows if not r['fixed_k_capacity']],
        dominated=[r for r in rows if r not in sum(fronts.values(),[])]))
    def export(fig,name):
        fig.savefig(FIG/(name+'.svg'),bbox_inches='tight',pad_inches=.08)
        self_contained_svg(FIG/(name+'.svg'), *FIGURE_ACCESSIBILITY[name])
        fig.savefig(WORK/(name+'.png'),dpi=160,bbox_inches='tight',pad_inches=.08)
        plt.close(fig)
    pre_runs=[rr for rec in (json.loads(line) for line in (DATA/'prefilter-matched.jsonl').read_text().splitlines() if line.strip())
              for rr in rec['result']]
    pre_qps=[r['qps_avg_pass'] for r in pre_runs]
    pre_ref=dict(method='Pre-filter',recall=pre_runs[0]['recall'],qps=len(pre_qps)/sum(1/x for x in pre_qps),distances=pre_runs[0]['dist_calls_per_q'])
    fig,ax=plt.subplots(figsize=(7.1,3.7))
    for method,color,marker,label in [('NodeANN','#0072B2','o','NodeANN'),('ASI','#009E73','D','Ours (ASI)')]:
        ff=fronts[method]
        ax.plot([r['recall'] for r in ff],[r['qps']/1000 for r in ff],marker=marker,color=color,
                lw=1.9,ms=4.5,label=label)
    ax.set(xlabel=f'Recall@{K} against original FP32',ylabel='Throughput (k queries/s)')
    ax.set_xlim(min(r['recall'] for r in rows)-.03,max(r['recall'] for r in rows)+.03)
    ax.set_ylim(0,max(r['qps'] for r in rows)/1000*1.08)
    ax.grid(axis='y',color='.90',lw=.6);ax.legend(frameon=False,loc='upper right')
    fig.tight_layout();export(fig,'pareto')
    # Dashboard: three ASI operating points plus NodeANN and exact scan.
    dash_efs = [10,40,200]
    selected=[next(r for r in rows if r['method']=='ASI' and r['param']==ef) for ef in dash_efs]
    selected += [max((r for r in full if r['method']=='NodeANN'),key=lambda r:r['recall']),pre_ref]
    fig,axes=plt.subplots(1,3,figsize=(7.1,2.65),sharey=True,gridspec_kw={'wspace':.42})
    names=[f'ASI ef={r["param"]}' if r['method']=='ASI' else (f'NodeANN N={r["param"]}' if r['method']=='NodeANN' else 'Pre-filter exact') for r in selected]
    for ax,key,title,fmt in zip(axes,['recall','qps','distances'],[f'Recall@{K} ↑','QPS ↑','Distances / query ↓'],['.4f',',.0f',',.0f']):
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
        for ax,y,title,color in zip(axes,['recall','qps'],[f'Recall@{K}','Throughput (k queries/s)'],['#009E73','#0072B2']):
            vals=[r[y]/(1000 if y=='qps' else 1) for r in aa]
            ax.plot([r['value'] for r in aa],vals,'o-',color=color,lw=1.5,ms=4)
            ax.set(xlabel=xlabel,ylabel=title);ax.set_xticks([r['value'] for r in aa]);ax.grid(alpha=.2)
        fig.suptitle(f'ef={aa[0].get("ef",K)}; '+('p=50' if key=='ef2' else 'ef2=16'),fontsize=9)
        fig.tight_layout();export(fig,'ablation-'+key)
    for name in ['asi-build', 'asi-merge', 'dqs-search', 'toy-dag']:
        self_contained_svg(FIG/(name+'.svg'))
    print(json.dumps(dict(frontiers=fronts,attribution=points),indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=['measure','measure-entry','attribute','render','render-entry'])
    ap.add_argument('--runs',type=int,default=3)
    ap.add_argument('--k',type=int,choices=[10],default=K)
    ap.add_argument('--out',default='2026-11-08-exps',help='experiment directory under researches/nested-attr-vector-search')
    ap.add_argument('--trace',default=None)
    args=ap.parse_args()
    K = args.k
    OUT = ROI / args.out
    DATA = OUT / 'data'
    FIG = OUT / 'figures'
    args.trace = args.trace or str(WORK/'trace-k10-ef-10-40-200.jsonl')
    {'measure':measure,'measure-entry':measure_entry,'attribute':attribution,'render':render,'render-entry':render_entry}[args.action](args)
