#!/usr/bin/env python3
"""Prepare genuine SIFT fvecs with explicitly synthetic hierarchy/attributes.

Streaming FP32 extraction; no ANN, GT, query-aware layout, or claimed natural
hierarchy. Fixed legal object counts across random/coordinate ownership controls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

from generate_dynamic_triad import sha256_file, write_json
from run_closure import put_trace


def read_shape(path):
    with path.open('rb') as f:
        header = f.read(4)
    if len(header) != 4:
        raise ValueError('missing fvecs header')
    d = int(np.frombuffer(header, dtype='<i4')[0])
    if d <= 0 or path.stat().st_size % (4 * (d + 1)):
        raise ValueError('malformed fvecs shape')
    return path.stat().st_size // (4 * (d + 1)), d


def extract(path, target, start, n, dim):
    count, d = read_shape(path)
    if d != dim or start < 0 or start + n > count:
        raise ValueError('fvecs dimension or requested range mismatch')
    source = np.memmap(path, dtype='<i4', mode='r', shape=(count, d + 1))
    scores = np.empty(n, dtype=np.float32)
    with target.open('wb') as out:
        for first in range(0, n, 16384):
            rows = source[start + first:start + min(first + 16384, n)]
            if not np.all(rows[:, 0] == d):
                raise ValueError('inconsistent per-vector fvecs header')
            vectors = rows[:, 1:].view('<f4')
            if not np.isfinite(vectors).all():
                raise ValueError('nonfinite input vector')
            vectors.tofile(out)
            # Fixed projection depends only on base vectors, never query answers.
            take = min(d, 8)
            scores[first:first + len(rows)] = vectors[:, :take] @ np.arange(1, take + 1, dtype=np.float32)
    return scores


def make_tree(leaves):
    levels = [leaves]
    while levels[-1] > 1:
        levels.append((levels[-1] + 7) // 8)
    levels.reverse()
    parent, level_ids = [-1], [[0]]
    for count in levels[1:]:
        first = len(parent)
        parent.extend(level_ids[-1][i // 8] for i in range(count))
        level_ids.append(list(range(first, first + count)))
    return parent, level_ids


def run(args):
    if args.n < 16 or args.leaves < 2 or args.queries < 16 or args.seed < 0:
        raise ValueError('require n>=16, leaves>=2, queries>=16, nonnegative seed')
    base, query = args.base_fvecs.resolve(strict=True), args.query_fvecs.resolve(strict=True)
    _, dim = read_shape(base)
    if dim != 128:
        raise ValueError('this SIFT protocol requires 128-dimensional original vectors')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    manifest = dict(complete=False, dataset_kind='provided_fvecs_synthetic_metadata',
                    n=args.n, dim=dim, queries=args.queries, query_offset=args.query_offset,
                    leaves=args.leaves, ownership=args.ownership, seed=args.seed,
                    source_base=str(base), source_query=str(query),
                    source_base_sha256=sha256_file(base), source_query_sha256=sha256_file(query),
                    generator_sha256=sha256_file(Path(__file__)),
                    provenance_note='Source hashes identify provided files, not an automatic certification that they are SIFT.',
                    normalization='none; original FP32 squared-L2', synthetic_hierarchy=True,
                    note='Controlled hierarchy, not real SIFT directory metadata; initial live count N, no reserved vectors.')
    write_json(out / 'preparation.json', manifest)
    try:
        scores = extract(base, out / 'base.f32', 0, args.n, dim)
        extract(query, out / 'test.f32', args.query_offset, args.queries, dim)
        parent, levels = make_tree(args.leaves)
        rng = np.random.default_rng(args.seed)
        order = rng.permutation(args.n) if args.ownership == 'random' else np.lexsort((np.arange(args.n), scores))
        nonleaf = list(range(levels[-1][0]))
        template = np.empty(args.n, dtype=np.int64)
        internal_n = args.n // 8
        template[:internal_n] = np.resize(np.asarray(nonleaf), internal_n)
        template[internal_n:] = np.resize(np.asarray(levels[-1]), args.n - internal_n)
        # Group template by directory: both controls have exactly identical owner counts.
        template.sort()
        owners = np.empty_like(template)
        owners[order] = template
        # Separate RNG: identical attributes for a given ID across ownership controls.
        attrs_rng = np.random.default_rng(args.seed + 1)
        category = attrs_rng.integers(0, 256, size=args.n, dtype=np.int64)
        numeric = attrs_rng.integers(-500000, 500001, size=args.n, dtype=np.int64)
        tags = attrs_rng.integers(0, 256, size=args.n, dtype=np.uint64)
        (out / 'meta.txt').write_text(f'{args.n} {dim} {args.queries}\n')
        (out / 'tree.txt').write_text(str(len(parent)) + '\n' + ''.join(f'{i} {p}\n' for i, p in enumerate(parent)))
        with (out / 'bindings.txt').open('w') as f:
            for value in owners: f.write(str(int(value)) + '\n')
        with (out / 'attrs.txt').open('w') as f:
            for c, v, t in zip(category, numeric, tags): f.write(f'{int(c)} {int(v)} {int(t)} 1\n')
        events = []
        for qidx in range(args.queries):
            level = (qidx // 20) % len(levels)
            anchor = levels[level][(qidx * 17) % len(levels[level])]
            mode = (qidx // 2) % 10
            recursive = 0 if mode == 7 else 1
            c, lo, hi, tagmode, mask, combine = -1, -1, -1, 0, 0, 0
            if mode == 1: c = qidx % 256
            elif mode == 2: lo, hi = -50000, 50000
            elif mode == 3: tagmode, mask = 1, 3
            elif mode == 4: c, lo, hi = qidx % 256, -50000, 50000
            elif mode == 5: c, lo, hi, combine = qidx % 256, -50000, 50000, 1
            elif mode == 6: lo, hi, tagmode, mask = -100000, 100000, 2, 3
            elif mode in (8, 9):
                # Fixed expected GLOBAL eligible count, not a GT-derived predicate.
                # Actual E is measured; arbitrary scopes use the fixed-selectivity families above.
                target = 128 if mode == 8 else 512
                anchor, recursive = 0, 1
                lo, hi = -500000, -500000 + min(1000001, max(1, round(1000001 * target / args.n))) - 1
            events.append(list(map(str, ['Q', qidx, recursive, anchor, c, lo, hi, tagmode, mask, combine])))
            # Persistent metadata changes; later Qs see changed eligibility.
            # Static replay deliberately extracts Qs against the initial snapshot.
            if qidx % 16 == 15:
                ident = (qidx * 31) % args.n
                dest = levels[-1][(qidx + 1) % args.leaves]
                category[ident] ^= 1; numeric[ident] ^= 1; tags[ident] = int(tags[ident]) ^ 1
                owners[ident] = dest
                events += [['V', str(ident), str(int(category[ident])), str(int(numeric[ident]))],
                           ['T', str(ident), str(int(tags[ident]))],
                           ['M', str(ident), str(dest)],
                           ['D', str(ident)],
                           ['I', str(ident), str(dest), str(int(category[ident])), str(int(numeric[ident])), str(int(tags[ident]))]]
            # Two fixed 1%-N bursts expose placement drift, not just scalar-update speed.
            if qidx in (63, 127):
                start = 0 if qidx == 63 else args.n // 100
                for ident in range(start, min(start + max(1, args.n // 100), args.n)):
                    dest = levels[-1][(ident * 37 + 1) % args.leaves]
                    if int(owners[ident]) != dest:
                        events.append(['M', str(ident), str(dest)])
                        owners[ident] = dest
        trace = out / 'mixed.trace'
        put_trace(trace, events)
        manifest.update(complete=True, nodes=len(parent), initial_live=args.n,
                        query_split='qidx parity; within-run disjoint, no claim of no overlap with all historical experiments',
                        files={name: sha256_file(out / name) for name in
                               ('base.f32', 'test.f32', 'meta.txt', 'tree.txt', 'bindings.txt', 'attrs.txt', 'mixed.trace')})
        write_json(out / 'preparation.json', manifest)
        print(json.dumps(dict(complete=True, data=str(out), trace=str(trace), n=args.n, leaves=args.leaves)))
    except Exception as error:
        manifest['error'] = str(error); write_json(out / 'preparation.json', manifest); raise


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base-fvecs', type=Path, required=True)
    p.add_argument('--query-fvecs', type=Path, required=True)
    p.add_argument('--n', type=int, required=True)
    p.add_argument('--leaves', type=int, choices=(64, 2048), required=True)
    p.add_argument('--queries', type=int, default=256)
    p.add_argument('--query-offset', type=int, default=1024)
    p.add_argument('--ownership', choices=('random', 'coordinate'), default='random')
    p.add_argument('--seed', type=int, default=20261031)
    p.add_argument('--out', type=Path, required=True)
    run(p.parse_args())


if __name__ == '__main__':
    main()
