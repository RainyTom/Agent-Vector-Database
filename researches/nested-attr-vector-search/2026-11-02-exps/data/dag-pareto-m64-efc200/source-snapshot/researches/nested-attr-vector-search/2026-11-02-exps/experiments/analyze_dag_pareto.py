#!/usr/bin/env python3
"""Audit measured batches and plot observed frontiers; never interpolate results."""
import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

METHODS = ('dag_merge', 'nodeann', 'acorn1', 'global_prefilter')
ARMS = {'dag_merge': ('merged', 'merged_flat_owner_dense'), 'nodeann': ('nodeann_pergraph',),
        'acorn1': ('global_acorn1',), 'global_prefilter': ('global_prefilter',)}
LABELS = {'merged': 'DAG level-0', 'merged_flat_owner_dense': 'DAG level-0: CSR + visited bitmap',
          'nodeann_pergraph': 'NodeANN: local n=1..10', 'global_acorn1': 'ACORN-1 search port',
          'global_prefilter': 'Strict-prefilter control'}
COLORS = dict(zip(LABELS, ('#0072b2', '#009e73', '#d55e00', '#cc79a7', '#777777')))
EFS = (10, 16, 32, 64, 128, 256, 512, 2048, 168937)


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load(path):
    with path.open(newline='') as f:
        return list(csv.DictReader(f))


def close(a, b):
    return math.isclose(a, b, rel_tol=1e-10, abs_tol=1e-11)


def frontier(points):
    # Exact observed-coordinate dominance, no convex hull or interpolation.
    result = [p for p in points if not any(
        q['recall_mean'] >= p['recall_mean'] and q['qps'] >= p['qps'] and
        (q['recall_mean'] > p['recall_mean'] or q['qps'] > p['qps']) for q in points)]
    return sorted(result, key=lambda p: (p['recall_mean'], p['qps']))


def point_key(p):
    return p['arm'], p['ef'], p['n']


def audit_run(root, semantics, nq):
    run = root / f'nq{nq}-{semantics}'
    manifest, all_points, all_rows, parameters = None, [], [], {}
    for method in METHODS:
        source = run / method
        current = (source / f'queries-{semantics}.csv').read_text()
        require(manifest is None or current == manifest, 'per-method query/GT manifests differ')
        manifest = current
        params = json.loads((source / 'index-parameters.json').read_text())
        require((params['hnsw_M'], params['ef_construction'], params['maxM0']) == (64, 200, 128),
                f'{source}: native parameters differ')
        parameters[method] = params
        rows, batches, points = load(source / 'pareto-queries.csv'), load(source / 'pareto-batches.csv'), load(source / 'pareto-points.csv')
        grouped, sampled = defaultdict(list), defaultdict(list)
        unique_rows = set()
        for r in rows:
            require(r['arm'] in ARMS[method], 'wrong method arm')
            for key in ('ef', 'n', 'query', 'eligible', 'truth', 'hits', 'returned', 'distances',
                        'rejected_distances', 'item_checks', 'owner_checks', 'eligible_owner_groups', 'normal_verified'):
                r[key] = int(r[key])
            r['adjacency_entries'] = int(r['adjacency_entries']) if r['adjacency_entries'] else None
            key = point_key(r)
            require((*key, r['query']) not in unique_rows, 'duplicate query row')
            unique_rows.add((*key, r['query']))
            require(0 <= r['hits'] <= r['returned'] <= r['truth'] == min(10, r['eligible']), 'invalid hits/denominator')
            require(r['normal_verified'] == 1 and r['rejected_distances'] == 0, 'qualification or normal/count mismatch')
            if r['eligible'] == 0:
                require(r['distances'] == r['returned'] == 0, 'empty scope did work')
            require((r['adjacency_entries'] is None) == (method in ('nodeann', 'global_prefilter')), 'unknown adjacency count was fabricated')
            grouped[key].append(r)
        for sample in batches:
            key = sample['arm'], int(sample['ef']), int(sample['n'])
            elapsed = float(sample['batch_us'])
            require(math.isfinite(elapsed) and elapsed > 0, 'invalid measured batch')
            sampled[key].append((int(sample['sample']), elapsed))
        unique_points = set()
        for p in points:
            for key in ('ef', 'n', 'k', 'hnsw_M', 'ef_construction', 'nonempty', 'empty', 'batch_passes', 'repetitions'):
                p[key] = int(p[key])
            for key in ('recall_mean', 'pair_recall', 'recall_min', 'returned_mean', 'qps', 'median_batch_us',
                        'distances_mean', 'item_checks_mean', 'owner_checks_mean', 'batch_min_us', 'batch_max_us'):
                p[key] = float(p[key]); require(math.isfinite(p[key]) and p[key] >= 0, f'invalid {key}')
            p['adjacency_entries_mean'] = float(p['adjacency_entries_mean']) if p['adjacency_entries_mean'] else None
            key = point_key(p)
            require(key not in unique_points, 'duplicate point')
            unique_points.add(key)
            rr = grouped[key]
            require({r['query'] for r in rr} == set(range(nq)) and len(rr) == nq, 'missing query rows')
            live = [r for r in rr if r['eligible']]
            count = len(live)
            require((p['k'], p['hnsw_M'], p['ef_construction'], p['nonempty'], p['empty'], p['batch_passes'], p['repetitions']) ==
                    (10, 64, 200, count, nq - count, 16, 10), 'point protocol differs')
            recalls = [r['hits'] / r['truth'] for r in live]
            expected = {'recall_mean': sum(recalls) / count, 'recall_min': min(recalls),
                        'pair_recall': sum(r['hits'] for r in live) / sum(r['truth'] for r in live),
                        'returned_mean': sum(r['returned'] for r in live) / count}
            for field in ('distances', 'item_checks', 'owner_checks'):
                expected[f'{field}_mean'] = sum(r[field] for r in live) / count
            for field, value in expected.items():
                require(close(p[field], value), f'{key}: incorrect {field}')
            samples = sampled[key]
            require(sorted(i for i, _ in samples) == list(range(10)), 'missing/duplicate batch samples')
            elapsed = sorted(v for _, v in samples)[5]  # Same upper median as the C++ helper.
            require(close(p['median_batch_us'], elapsed) and close(p['qps'], count * 16e6 / elapsed), 'QPS not measured-batch throughput')
            require(close(p['batch_min_us'], min(v for _, v in samples)) and close(p['batch_max_us'], max(v for _, v in samples)), 'batch range mismatch')
            if p['adjacency_entries_mean'] is not None:
                require(close(p['adjacency_entries_mean'], sum(r['adjacency_entries'] for r in live) / count), 'adjacency mean mismatch')
        expected_keys = {(arm, ef, n) for arm in ARMS[method] for ef in EFS
                         for n in (range(1, 11) if method == 'nodeann' else (0,))}
        require(unique_points == set(grouped) == set(sampled) == expected_keys, 'parameter grid incomplete')
        all_points.extend(points); all_rows.extend(rows)
    manifest_rows = load(run / 'dag_merge' / f'queries-{semantics}.csv')
    by_query = {int(r['query']): r for r in manifest_rows}
    for r in all_rows:
        q = by_query[r['query']]
        require((r['eligible'], r['truth']) == (int(q['eligible']), int(q['truth'])), 'query GT metadata differs')
    node_rows = [r for r in all_rows if r['arm'] == 'nodeann_pergraph']
    distances = defaultdict(set)
    for r in node_rows:
        distances[r['ef'], r['query']].add(r['distances'])
    fanout = [r['eligible_owner_groups'] for r in node_rows if r['ef'] == 32 and r['n'] == 10 and r['eligible']]
    frontiers = {arm: frontier([p for p in all_points if p['arm'] == arm]) for arm in LABELS}
    matched = {str(ef): {arm: frontier([p for p in all_points if p['arm'] == arm and p['ef'] == ef])
                        for arm in LABELS} for ef in EFS}
    grouped_fanout = []
    selected = {(r['arm'], r['query']): r for r in all_rows if r['ef'] == 512 and r['n'] in (0, 10) and r['eligible']}
    for name, predicate in (('G=1', lambda g: g == 1), ('G>1', lambda g: g > 1)):
        ids = [q for (arm, q), r in selected.items() if arm == 'nodeann_pergraph' and predicate(r['eligible_owner_groups'])]
        for arm in ('merged', 'merged_flat_owner_dense', 'nodeann_pergraph'):
            group = [selected[arm, q] for q in ids]
            grouped_fanout.append({'group': name, 'arm': arm, 'queries': len(group),
                'recall_mean': sum(r['hits'] / r['truth'] for r in group) / len(group) if group else None,
                'distances_mean': sum(r['distances'] for r in group) / len(group) if group else None,
                'item_checks_mean': sum(r['item_checks'] for r in group) / len(group) if group else None})
    return {'parameters': parameters, 'points': all_points, 'frontiers': frontiers,
            'global_frontier': frontier(all_points), 'matched_ef_frontiers': matched,
            'node_n_distances_invariant_at_fixed_ef': all(len(v) == 1 for v in distances.values()),
            'node_fanout': {'nonempty': len(fanout), 'one_graph': fanout.count(1), 'mean': sum(fanout) / len(fanout), 'max': max(fanout)},
            'ef512_fanout_work': grouped_fanout, 'query_rows': len(all_rows)}


def draw(run, semantics, out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
    fig, ax = plt.subplots(figsize=(9, 5.8), layout='constrained')
    for arm in LABELS:
        points = [p for p in run['points'] if p['arm'] == arm]
        front = run['frontiers'][arm]
        ax.scatter([p['recall_mean'] for p in points], [p['qps'] / 1000 for p in points],
                   color=COLORS[arm], alpha=.18, s=13)
        ax.plot([p['recall_mean'] for p in front], [p['qps'] / 1000 for p in front],
                '-o', color=COLORS[arm], markersize=4, label=LABELS[arm])
        # Label endpoints only; dense high-recall labels otherwise overlap.
        if arm != 'merged_flat_owner_dense':
            for i in sorted({0, len(front) - 1}):
                p = front[i]
                text = f"e{p['ef']}" + (f"/n{p['n']}" if p['n'] else '')
                offset = (-78, 12) if arm == 'merged' else (-86, -18) if arm == 'nodeann_pergraph' else (6, 8)
                ax.annotate(text, (p['recall_mean'], p['qps'] / 1000), xytext=offset,
                            textcoords='offset points', fontsize=8, color=COLORS[arm])
    best = run['global_frontier']
    ax.scatter([p['recall_mean'] for p in best], [p['qps'] / 1000 for p in best],
               marker='D', facecolors='none', edgecolors='black', linewidths=.8, s=45,
               label='Across-method non-dominated')
    ax.set(xlabel='Query-average Recall@10 (nonempty queries)', ylabel='Measured single-thread search QPS (thousands)',
           title=f'{semantics}: observed Pareto curves — M=64, efConstruction=200', xlim=(-.015, 1.035))
    ax.grid(alpha=.18); ax.legend(loc='best', fontsize=8)
    fig.savefig(out / f'pareto-{semantics}.png', dpi=140)
    plt.close(fig)
    fig, axes = plt.subplots(3, 3, figsize=(12, 9), layout='constrained')
    for ef, ax in zip(EFS, axes.flat):
        for arm in LABELS:
            points = sorted((p for p in run['points'] if p['arm'] == arm and p['ef'] == ef), key=lambda p: p['n'])
            ax.plot([p['recall_mean'] for p in points], [p['qps'] / 1000 for p in points],
                    '-o', markersize=3, color=COLORS[arm], label=LABELS[arm])
        ax.set_title(f'ef={ef}'); ax.set_xlim(-.015, 1.035); ax.grid(alpha=.18)
        ax.set_xlabel('Recall@10'); ax.set_ylabel('QPS (thousands)')
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='outside lower center', ncol=3, fontsize=8)
    fig.suptitle(f'{semantics}: same-ef comparisons; only NodeANN local n varies')
    fig.savefig(out / f'matched-ef-{semantics}.png', dpi=140)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--nq', type=int, default=120)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    result = {'validation_passed': True, 'input_root': str(args.dir),
              'protocol': {'M': 64, 'efConstruction': 200, 'k': 10, 'ef_grid': EFS,
                           'node_local_n': list(range(1, 11)), 'batch_passes': 16, 'timed_batches': 10,
                           'qps_definition': 'nonempty_queries*16/upper_median_batch_seconds',
                           'excluded': ['scope preparation', 'GT', 'build', 'instrumented count pass'],
                           'included': ['graph search', 'NodeANN local result merge', 'result checksum consumption'],
                           'frontier': 'observed nondominated points, not a deployment oracle or interpolation'}, 'runs': {}}
    for semantics in ('recursive', 'direct'):
        run = audit_run(args.dir, semantics, args.nq)
        result['runs'][semantics] = run
        draw(run, semantics, args.out)
    with (args.out / 'summary.json').open('w') as f:
        json.dump(result, f, ensure_ascii=False, indent=2); f.write('\n')
    fields = ('semantics', 'arm', 'ef', 'n', 'recall_mean', 'pair_recall', 'recall_min', 'qps',
              'distances_mean', 'item_checks_mean', 'owner_checks_mean', 'adjacency_entries_mean')
    with (args.out / 'frontiers.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore'); writer.writeheader()
        for semantics, run in result['runs'].items():
            for front in run['frontiers'].values():
                for p in front: writer.writerow({'semantics': semantics, **p})
    print('PASS actual M64/efC200, common ef and GT, local-n grid, zero rejected distances, measured QPS, observed frontiers')


if __name__ == '__main__':
    main()
