#!/usr/bin/env python3
"""Audit fixed (k, efSearch) cells; compare measured QPS at an observed recall target."""
import argparse
import csv
import gzip
import json
import math
from collections import defaultdict
from pathlib import Path

METHODS = ('dag_merge', 'nodeann', 'acorn1', 'global_prefilter')
ARMS = {'dag_merge': 'merged', 'nodeann': 'nodeann_pergraph',
        'acorn1': 'global_acorn1', 'global_prefilter': 'global_prefilter'}
CASES = ((10, 32), (100, 128))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load(path):
    source = path if path.is_file() else Path(str(path) + '.gz')
    opener = gzip.open if source.suffix == '.gz' else open
    with opener(source, 'rt', newline='', encoding='utf-8') as f:
        return list(csv.DictReader(f))


def close(a, b):
    return math.isclose(a, b, rel_tol=1e-10, abs_tol=1e-11)


def key(row):
    return row['arm'], row['k'], row['ef_requested'], row['n']


def frontier(points):
    # Used only within one fixed k/ef cell, never to join different ef settings.
    return sorted([p for p in points if not any(
        q['recall_mean'] >= p['recall_mean'] and q['qps'] >= p['qps'] and
        (q['recall_mean'] > p['recall_mean'] or q['qps'] > p['qps']) for q in points)],
        key=lambda p: (p['recall_mean'], p['qps']))


def select_at_reference(points, reference):
    require(bool(points), 'no observed points for this comparison')
    cell = tuple(reference[name] for name in ('k', 'ef_requested', 'ef_effective'))
    require(all(tuple(p[name] for name in ('k', 'ef_requested', 'ef_effective')) == cell for p in points),
            'cannot compare different k/efSearch cells')
    feasible = [p for p in points if p['recall_mean'] + 1e-12 >= reference['recall_mean']]
    maximum = max(p['recall_mean'] for p in points)
    if not feasible:
        return {'status': 'recall_target_unmet', 'max_observed_recall': maximum, 'point': None,
                'qps_ratio_to_reference': None}
    selected = max(feasible, key=lambda p: (p['qps'], p['recall_mean'], -p['n']))
    return {'status': 'observed_recall_at_least_reference', 'max_observed_recall': maximum,
            'point': selected, 'recall_overshoot': selected['recall_mean'] - reference['recall_mean'],
            'qps_ratio_to_reference': selected['qps'] / reference['qps']}


def audit_run(root, semantics, nq):
    run = root / f'nq{nq}-{semantics}'
    manifest, all_points, all_rows, parameters = None, [], [], {}
    for method in METHODS:
        source = run / method
        manifest_rows = load(source / f'queries-{semantics}.csv')
        current = json.dumps(manifest_rows, sort_keys=True)
        require(manifest is None or current == manifest, 'per-method query/GT manifests differ')
        manifest = current
        params = json.loads((source / 'index-parameters.json').read_text())
        require((params['hnsw_M'], params['ef_construction'], params['maxM0']) == (64, 200, 128),
                f'{source}: native parameters differ')
        parameters[method] = params
        rows = load(source / 'pareto-queries.csv')
        batches = load(source / 'pareto-batches.csv')
        points = load(source / 'pareto-points.csv')
        grouped, sampled = defaultdict(list), defaultdict(list)
        unique_rows = set()
        for r in rows:
            require(r['arm'] == ARMS[method], 'wrong method arm')
            for name in ('k', 'ef_requested', 'ef_effective', 'n', 'query', 'eligible', 'truth', 'hits',
                         'returned', 'distances', 'rejected_distances', 'item_checks', 'owner_checks',
                         'eligible_owner_groups', 'normal_verified'):
                r[name] = int(r[name])
            r['adjacency_entries'] = int(r['adjacency_entries']) if r['adjacency_entries'] else None
            identity = key(r)
            require((*identity, r['query']) not in unique_rows, 'duplicate query row')
            unique_rows.add((*identity, r['query']))
            require(r['ef_effective'] == max(r['k'], r['ef_requested']), 'effective width differs')
            require(0 <= r['hits'] <= r['returned'] <= r['truth'] == min(r['k'], r['eligible']), 'invalid k/GT/return count')
            require(r['normal_verified'] == 1 and r['rejected_distances'] == 0, 'qualification or normal/count mismatch')
            if r['eligible'] == 0:
                require(r['distances'] == r['returned'] == 0, 'empty scope did work')
            require((r['adjacency_entries'] is None) == (method in ('nodeann', 'global_prefilter')),
                    'unknown adjacency count was fabricated')
            grouped[identity].append(r)
        for sample in batches:
            identity = sample['arm'], int(sample['k']), int(sample['ef_requested']), int(sample['n'])
            require(int(sample['ef_effective']) == max(identity[1], identity[2]), 'batch effective width differs')
            elapsed = float(sample['batch_us'])
            require(math.isfinite(elapsed) and elapsed > 0, 'invalid measured batch')
            sampled[identity].append((int(sample['sample']), elapsed))
        unique_points = set()
        for p in points:
            for name in ('k', 'ef_requested', 'ef_effective', 'n', 'hnsw_M', 'ef_construction',
                         'nonempty', 'empty', 'batch_passes', 'repetitions'):
                p[name] = int(p[name])
            for name in ('recall_mean', 'pair_recall', 'recall_min', 'returned_mean', 'qps', 'median_batch_us',
                         'distances_mean', 'item_checks_mean', 'owner_checks_mean', 'batch_min_us', 'batch_max_us'):
                p[name] = float(p[name]); require(math.isfinite(p[name]) and p[name] >= 0, f'invalid {name}')
            p['adjacency_entries_mean'] = float(p['adjacency_entries_mean']) if p['adjacency_entries_mean'] else None
            identity = key(p)
            require(identity not in unique_points, 'duplicate point')
            unique_points.add(identity)
            rr = grouped[identity]
            require({r['query'] for r in rr} == set(range(nq)) and len(rr) == nq, 'missing query rows')
            live = [r for r in rr if r['eligible']]
            count = len(live)
            p['underfilled_queries'] = sum(r['returned'] < r['truth'] for r in live)
            p['incomplete_recall_queries'] = sum(r['hits'] < r['truth'] for r in live)
            require((p['hnsw_M'], p['ef_construction'], p['nonempty'], p['empty'], p['batch_passes'], p['repetitions']) ==
                    (64, 200, count, nq - count, 16, 10), 'point protocol differs')
            require(p['ef_effective'] == max(p['k'], p['ef_requested']), 'point effective width differs')
            recalls = [r['hits'] / r['truth'] for r in live]
            expected = {'recall_mean': sum(recalls) / count, 'recall_min': min(recalls),
                        'pair_recall': sum(r['hits'] for r in live) / sum(r['truth'] for r in live),
                        'returned_mean': sum(r['returned'] for r in live) / count}
            for field in ('distances', 'item_checks', 'owner_checks'):
                expected[f'{field}_mean'] = sum(r[field] for r in live) / count
            for field, value in expected.items():
                require(close(p[field], value), f'{identity}: incorrect {field}')
            samples = sampled[identity]
            require(sorted(i for i, _ in samples) == list(range(10)), 'missing/duplicate batch samples')
            elapsed = sorted(v for _, v in samples)[5]
            require(close(p['median_batch_us'], elapsed) and close(p['qps'], count * 16e6 / elapsed), 'QPS not measured-batch throughput')
            require(close(p['batch_min_us'], min(v for _, v in samples)) and
                    close(p['batch_max_us'], max(v for _, v in samples)), 'batch range mismatch')
            if p['adjacency_entries_mean'] is not None:
                require(close(p['adjacency_entries_mean'], sum(r['adjacency_entries'] for r in live) / count), 'adjacency mean mismatch')
        expected_keys = {(ARMS[method], k, ef, n) for k, ef in CASES
                         for n in (range(1, k + 1) if method == 'nodeann' else (0,))}
        require(unique_points == set(grouped) == set(sampled) == expected_keys, 'requested fixed-case grid incomplete')
        all_points.extend(points); all_rows.extend(rows)
    by_query = {int(r['query']): r for r in manifest_rows}
    require(set(by_query) == set(range(nq)), 'incomplete input manifest')
    for r in all_rows:
        q = by_query[r['query']]
        require(r['eligible'] == int(q['eligible']) and int(q['truth']) == min(100, r['eligible']), 'cached top-100 GT metadata differs')
    node_rows = [r for r in all_rows if r['arm'] == 'nodeann_pergraph']
    distances = defaultdict(set)
    for r in node_rows:
        distances[r['k'], r['ef_requested'], r['query']].add(r['distances'])
    fanout_k, fanout_ef = CASES[0]
    fanout = [r['eligible_owner_groups'] for r in node_rows
              if r['k'] == fanout_k and r['ef_requested'] == fanout_ef and r['n'] == fanout_k and r['eligible']]
    eligible = [int(q['eligible']) for q in by_query.values() if int(q['eligible'])]
    k_workload = [{'k': k, 'nonempty': len(eligible), 'eligible_below_k': sum(n < k for n in eligible),
                   'gt_pairs': sum(min(k, n) for n in eligible),
                   'mean_truth': sum(min(k, n) for n in eligible) / len(eligible)} for k in (10, 50, 100)]
    comparisons = []
    for k, ef in CASES:
        cell = [p for p in all_points if (p['k'], p['ef_requested']) == (k, ef)]
        reference = next(p for p in cell if p['arm'] == 'merged')
        comparisons.append({'k': k, 'ef_requested': ef, 'ef_effective': max(k, ef), 'reference': reference,
            'nodeann': select_at_reference([p for p in cell if p['arm'] == 'nodeann_pergraph'], reference),
            'acorn1': select_at_reference([p for p in cell if p['arm'] == 'global_acorn1'], reference),
            'strict_control': select_at_reference([p for p in cell if p['arm'] == 'global_prefilter'], reference),
            'nodeann_within_cell_frontier': frontier([p for p in cell if p['arm'] == 'nodeann_pergraph'])})
    return {'parameters': parameters, 'points': all_points, 'comparisons': comparisons,
            'node_n_distances_invariant_at_fixed_k_and_ef': all(len(v) == 1 for v in distances.values()),
            'node_fanout': {'nonempty': len(fanout), 'one_graph': fanout.count(1),
                            'mean': sum(fanout) / len(fanout), 'max': max(fanout)},
            'query_rows': len(all_rows), 'k_workload': k_workload}


def draw_comparison(runs, out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch
    colors = ('#0072b2', '#d55e00', '#cc79a7')
    ks = sorted({c['k'] for c in runs['recursive']['comparisons']})
    fig, axes = plt.subplots(len(ks), 2, figsize=(11, 3.4 * len(ks)), layout='constrained', squeeze=False)
    for row, k in enumerate(ks):
        for col, semantics in enumerate(('recursive', 'direct')):
            ax = axes[row, col]
            cells = [c for c in runs[semantics]['comparisons'] if c['k'] == k]
            visible = [c['reference'] for c in cells] + [
                c[method]['point'] for c in cells for method in ('nodeann', 'acorn1') if c[method]['point']]
            ymax = max(p['nonempty'] * 16e3 / p['batch_min_us'] for p in visible)
            for i, cell in enumerate(cells):
                for method_index, method in enumerate(('reference', 'nodeann', 'acorn1')):
                    reference = method == 'reference'
                    selected = cell[method] if reference else cell[method]['point']
                    x = i + (method_index - 1) * .24
                    if selected is None:
                        ax.plot(x, .025 * ymax, 'x', color=colors[method_index], markersize=6)
                        ax.annotate('< R*', (x, .025 * ymax), xytext=(0, 5), textcoords='offset points',
                                    ha='center', fontsize=7, color=colors[method_index])
                        continue
                    y = selected['qps'] / 1000
                    # QPS / 1000 = queries*1000 / microseconds.
                    slow = selected['nonempty'] * 16e3 / selected['batch_max_us']
                    fast = selected['nonempty'] * 16e3 / selected['batch_min_us']
                    ax.bar(x, y, .22, color=colors[method_index], alpha=.9,
                           yerr=[[max(0, y - slow)], [max(0, fast - y)]], capsize=2, error_kw={'linewidth': .7})
                    if not reference:
                        text = f"n={selected['n']}\nR={selected['recall_mean']:.4f}"
                        ax.annotate(text, (x, fast), xytext=(0, 5), textcoords='offset points', ha='center', fontsize=7)
            labels = [str(c['ef_requested']) if c['ef_requested'] == c['ef_effective'] else
                      f"{c['ef_requested']} -> {c['ef_effective']}" for c in cells]
            labels = [label + f"\nR*={c['reference']['recall_mean']:.4f}" for label, c in zip(labels, cells)]
            ax.set_xticks(range(len(cells)), labels)
            ax.set_title(f'{semantics}, topk={k}')
            ax.set_xlabel('Fixed efSearch (requested -> effective if clamped)')
            ax.set_ylabel('Measured search QPS (thousands)')
            ax.grid(axis='y', alpha=.18); ax.set_axisbelow(True); ax.set_ylim(0, ymax * 1.30)
            ax.spines[['top', 'right']].set_visible(False)
    handles = [Patch(color=colors[0], label='DAG: fixed reference R*'),
               Patch(color=colors[1], label='NodeANN: fastest observed n with Recall >= R*'),
               Patch(color=colors[2], label='ACORN-1 search port'),
               Line2D([], [], color='black', marker='x', linestyle='none', label='Recall target unmet; no zero-QPS bar')]
    fig.legend(handles=handles, loc='outside lower center', ncol=2, fontsize=9)
    fig.suptitle('Fixed-case comparisons only — M=64, efConstruction=200\nRecall matching uses query-average recall; overshoot and tail quality remain in CSV')
    fig.savefig(out / 'fixed-k-ef-comparison.png', dpi=140)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--nq', type=int, default=120)
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    result = {'validation_passed': True, 'input_root': str(args.dir),
        'protocol': {'M': 64, 'efConstruction': 200, 'cases': [{'k': k, 'ef_requested': ef, 'ef_effective': max(k, ef)} for k, ef in CASES],
            'node_local_n': 'all integers 1..k; no ef sweep inside a cell', 'reference': 'one fixed DAG point per cell',
            'normalisation': 'all methods, including every NodeANN n, use max(requested_ef,k)',
            'qps_definition': 'nonempty_queries*16/upper_median_batch_seconds',
            'comparison': 'fastest measured point within the same k/ef cell with query-average recall >= DAG reference',
            'not_claimed': ['exact equal recall', 'equal minimum/pair recall', 'same resident bytes', 'deployable n selector', 'interpolation', 'end-to-end QPS', 'statistical optimality']}, 'runs': {}}
    for semantics in ('recursive', 'direct'):
        result['runs'][semantics] = audit_run(args.dir, semantics, args.nq)
    draw_comparison(result['runs'], args.out)
    (args.out / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    fields = ('semantics', 'k', 'ef_requested', 'ef_effective', 'method', 'status', 'n', 'reference_recall',
              'recall_mean', 'recall_overshoot', 'pair_recall', 'recall_min', 'qps', 'qps_ratio_to_reference',
              'max_observed_recall', 'distances_mean', 'item_checks_mean', 'owner_checks_mean', 'adjacency_entries_mean')
    with (args.out / 'matched-comparison.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore'); writer.writeheader()
        for semantics, run in result['runs'].items():
            for c in run['comparisons']:
                common = {name: c[name] for name in ('k', 'ef_requested', 'ef_effective')}
                for method in ('reference', 'nodeann', 'acorn1', 'strict_control'):
                    p = c['reference'] if method == 'reference' else c[method]['point']
                    row = {**common, 'semantics': semantics, 'method': method,
                           'reference_recall': c['reference']['recall_mean']}
                    if method == 'reference': row.update({'status': 'fixed_reference', 'qps_ratio_to_reference': 1, **p})
                    else:
                        row.update({name: value for name, value in c[method].items() if name != 'point'})
                        if p: row.update(p)
                    writer.writerow(row)
    print('PASS two fixed k/ef cells, effective widths, k-dependent GT, complete local-n grids, '
          'zero rejected distances, measured QPS; no cross-ef curves')


if __name__ == '__main__':
    main()
