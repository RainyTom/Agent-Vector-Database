#!/usr/bin/env python3
"""Small deterministic mathematical counterexamples, not ANN benchmarks.

Run from any cwd; writes computed JSON to stdout. Standard library only.
Never compare this Python runtime to C++ index timings.
"""
import json
import math


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def squared_l2(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b))


def normalize(a):
    norm = math.sqrt(dot(a, a))
    if norm == 0:
        raise ValueError('zero vector has no cosine normalization')
    return tuple(x / norm for x in a)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    q = (1.0, 0.0)
    vectors = [(10.0, 10.0), (1.0, 0.0)]
    ip = [dot(q, x) for x in vectors]
    cosine = [dot(normalize(q), normalize(x)) for x in vectors]
    norm_l2 = [squared_l2(normalize(q), normalize(x)) for x in vectors]
    require(ip.index(max(ip)) == 0 and cosine.index(max(cosine)) == 1,
            'MIPS/cosine counterexample vanished')
    require(all(math.isclose(d, 2 - 2 * c, abs_tol=1e-12) for d, c in zip(norm_l2, cosine)),
            'normalized metric identity failed')
    zero_rejected = False
    try:
        normalize((0, 0))
    except ValueError:
        zero_rejected = True
    require(zero_rejected, 'zero vector must be explicit')

    # Two objects collapse to the same retained coordinate, yet differ in source space.
    original = squared_l2((0, 0), (0, 10))
    projected = squared_l2((0,), (0,))
    require(original == 100 and projected == 0, 'projection-loss counterexample failed')

    # Same total selectivity, radically different rank-local eligibility.
    n, budget, k = 1000, 50, 5
    nearest_first = list(range(1, n + 1))
    near, far = set(range(1, 11)), set(range(991, 1001))
    def postfilter(allowed):
        return [i for i in nearest_first[:budget] if i in allowed][:k]
    near_result, far_result = postfilter(near), postfilter(far)
    require(len(near) == len(far) and len(near_result) == k and not far_result,
            'selectivity/correlation counterexample failed')

    # Marginal presence in a block does not imply same-object AND satisfaction.
    a, b = {1}, {2}
    require(bool(a) and bool(b) and not a & b, 'joint predicate counterexample failed')

    hits, gt_sizes = [0, 10], [1, 10]
    macro = sum(h / g for h, g in zip(hits, gt_sizes)) / len(hits)
    pair = sum(hits) / sum(gt_sizes)
    with_empty = (macro * len(hits) + 1) / (len(hits) + 1)
    require(macro == .5 and math.isclose(pair, 10 / 11) and math.isclose(with_empty, 2 / 3),
            'recall aggregation example failed')

    # Exact rerank sorts only available candidates; missing GT ID 1 remains absent.
    distances = {1: 1.0, 2: 2.0, 3: 3.0}
    truth = sorted(distances, key=lambda i: (distances[i], i))[:2]
    candidate_ids = [3, 2]
    reranked = sorted(candidate_ids, key=lambda i: (distances[i], i))[:2]
    rerank_recall = len(set(truth) & set(reranked)) / len(truth)
    require(rerank_recall == .5, 'rerank unexpectedly recovered an absent candidate')

    # Branches of a DAG can overlap without containment, unlike tree subtrees.
    left, right = {1, 3}, {2, 3}
    nonlaminar = bool(left & right) and not left <= right and not right <= left
    require(nonlaminar, 'DAG overlap counterexample failed')

    print(json.dumps(dict(
        complete=True, kind='mathematical_counterexamples_not_ANN_performance',
        metric=dict(inner_products=ip, cosine=cosine, normalized_squared_l2=norm_l2,
                    zero_normalization_rejected=zero_rejected),
        projection=dict(original_squared_l2=original, projected_squared_l2=projected),
        filtering=dict(n=n, eligible_each=len(near), budget=budget, k=k,
                       near_results=near_result, far_results=far_result,
                       independence_expected_matches=budget * len(near) / n,
                       independence_zero_probability=math.comb(n - len(near), budget) / math.comb(n, budget)),
        joint_predicate=dict(marginal_A=sorted(a), marginal_B=sorted(b), joint=sorted(a & b)),
        recall=dict(nonempty_macro=macro, pair=pair, macro_including_successful_empty=with_empty),
        rerank=dict(gt=truth, candidates=candidate_ids, reranked=reranked, recall=rerank_recall),
        dag=dict(left=sorted(left), right=sorted(right), nonlaminar=nonlaminar),
        limitations=['Constructed finite examples; no runtime comparison or product API validation.',
                     'Double-precision Python arithmetic is not a certified FP32 pruning kernel.']),
        ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
