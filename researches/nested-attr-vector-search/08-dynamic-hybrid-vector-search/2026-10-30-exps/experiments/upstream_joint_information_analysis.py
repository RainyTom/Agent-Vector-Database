#!/usr/bin/env python3
"""Analyze saved server controls and constructive geometry examples; no ANN run."""
import csv
import json
from pathlib import Path

EXP = Path(__file__).resolve().parents[1]
DATA = EXP / 'data' / 'upstream-review-7756064'


def exact_sq(points, query):
    return min(sum((x - q) ** 2 for x, q in zip(point, query)) for point in points)


def grouped_lower(points, query, groups):
    return sum(min(sum((point[j] - query[j]) ** 2 for j in group)
                   for point in points) for group in groups)


def main():
    with (DATA / 'root-scope-only-r64.csv').open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    # Exact full-distance oracle is independent of projection basis.
    unique = {}
    for row in rows:
        key = (row['dataset'], row['clusters'], row['qidx'])
        if key in unique:
            for field in ('eligible', 'tau', 'exact_oracle_skip_items'):
                assert row[field] == unique[key][field], (key, field)
        unique[key] = row
    controls = []
    for dataset in sorted({row['dataset'] for row in rows}):
        chosen = [row for key, row in unique.items() if key[0] == dataset]
        den = sum(int(row['eligible']) for row in chosen)
        num = sum(int(row['exact_oracle_skip_items']) for row in chosen)
        controls.append({
            'dataset': dataset, 'unique_query_vectors': len({r['qidx'] for r in chosen}),
            'query_partition_cases_after_basis_deduplication': len(chosen),
            'eligible_weight': den, 'exact_oracle_skip_weight': num,
            'exact_oracle_skip_fraction': num / den,
            'threshold': 'each unfiltered-root query own exact top-10 threshold',
        })

    axes = [(1, 0), (-1, 0), (0, 1), (0, -1)]
    origin = (0, 0)
    # Origin is a convex combination of these points; every convex outer
    # approximation therefore contains it. Norm shell alone recovers distance 1.
    shell = {
        'independent_coordinate_gap_sq': grouped_lower(axes, origin, [[0], [1]]),
        'coupled_two_coordinate_min_sq': grouped_lower(axes, origin, [[0, 1]]),
        'exact_min_sq': exact_sq(axes, origin),
        'convex_outer_distance_sq': 0,
        'radial_norm_lower_sq': 1,
    }
    assert shell['independent_coordinate_gap_sq'] == 0
    assert shell['coupled_two_coordinate_min_sq'] == shell['exact_min_sq'] == 1

    diagonal = [(1, 1), (-1, -1)]
    antidiagonal = [(1, -1), (-1, 1)]
    query = (1, 1)
    assert all(sorted(p[j] for p in diagonal) == sorted(p[j] for p in antidiagonal)
               for j in range(2))
    assert {sum(x*x for x in p) for p in diagonal + antidiagonal} == {2}
    indistinguishable = {
        'same_coordinate_multisets': True, 'same_norm_multiset': True,
        'diagonal_gap_sq': grouped_lower(diagonal, query, [[0], [1]]),
        'antidiagonal_gap_sq': grouped_lower(antidiagonal, query, [[0], [1]]),
        'diagonal_exact_min_sq': exact_sq(diagonal, query),
        'antidiagonal_exact_min_sq': exact_sq(antidiagonal, query),
    }
    assert indistinguishable['diagonal_exact_min_sq'] == 0
    assert indistinguishable['antidiagonal_exact_min_sq'] == 4

    # Same threshold, nonempty filtered set, fixed eligible weight 1:
    # the oracle can be perfect without any filtering-specific increment.
    attribution = []
    for whole, scope, filtered, threshold in [
        ([(3,), (4,)], [(3,), (4,)], [(4,)], 1),
        ([(0,), (1,), (2,)], [(1,), (2,)], [(2,)], 2),
    ]:
        d0, dh, df = (exact_sq(points, (0,)) for points in (whole, scope, filtered))
        assert d0 <= dh <= df
        attribution.append({
            'D_B': d0, 'D_H': dh, 'D_F': df, 'same_tau': threshold,
            'generic_exclusion': d0 > threshold,
            'hierarchy_increment': d0 <= threshold < dh,
            'ordinary_attribute_increment': dh <= threshold < df,
            'conditional_oracle_exclusion': df > threshold,
        })
    result = {
        'source_commit': '7756064', 'source_rows': len(rows),
        'controls_after_projection_basis_deduplication': controls,
        'constructive_examples': {'convex_hull_and_norm_shell': shell,
                                  'marginals_and_norms_are_insufficient': indistinguishable,
                                  'same_threshold_oracle_attribution': attribution},
        'limits': ['Descriptive reanalysis of existing CSV; no rerun of source experiment.',
                   'Root and filtered pooled thresholds differ; do not subtract their rates.',
                   'Integer toy examples are not new ANN performance or a floating-point certificate.',
                   'No proof that actual WIKI queries lie inside block convex hulls.',
                   'No new index, update benchmark, or novelty clearance.'],
    }
    destination = DATA / 'joint-information-analysis.json'
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
