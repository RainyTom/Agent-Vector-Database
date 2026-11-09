#!/usr/bin/env python3
"""Build the merge skeleton of the attribute-skeleton index with faiss k-means.

Merge order is the reverse topological sequence of the attribute DAG. Components
are folded pairwise: one attribute at a time is merged into the accumulated
component. Both sides are clustered separately into round(sqrt(size)) cells, each
cell contributes the original vector nearest to its centroid as a proxy, and the
proxies of the two sides are linked. Every attribute is clustered exactly once on
its own objects, and one of its own centres becomes its stored entry point.

Output (little endian):
    uint32 magic (0x41534D47)
    uint32 attribute_count
    per attribute: uint32 entry_node (0xffffffff when the attribute owns no object)
    uint32 link_count
    link_count x (uint32 from_node, uint32 to_node)   undirected, de-duplicated
"""
import argparse
import collections
import struct

import faiss
import numpy as np


def read_edges(path):
    parents = collections.defaultdict(list)
    children = collections.defaultdict(list)
    nodes = set()
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            child, parent = (int(x) for x in line.split())
            parents[child].append(parent)
            children[parent].append(child)
            nodes.add(child)
            nodes.add(parent)
    return parents, children, sorted(nodes)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--dim', type=int, default=128)
    parser.add_argument('--links-per-proxy', type=int, default=1)
    parser.add_argument('--seed', type=int, default=20261101)
    parser.add_argument('--entries-per-attribute', type=int, default=8,
                        help='entry points stored per attribute in the inverted index')
    parser.add_argument('--sample-cap', type=int, default=64,
                        help='k-means training sample size is max(4096, cap*k)')
    parser.add_argument('--iterations', type=int, default=10)
    args = parser.parse_args()

    objects = np.loadtxt(f'{args.data}/objects.txt', dtype=np.int64)
    base = np.memmap(f'{args.data}/base.bin', dtype=np.float32, mode='r').reshape(-1, args.dim)
    parents, children, attributes = read_edges(f'{args.data}/edges.tsv')

    # node id = position inside the attribute block, blocks in attribute order
    members = collections.defaultdict(list)
    for object_id, attribute in enumerate(objects):
        members[int(attribute)].append(object_id)
    ordered = [a for a in attributes if members[a]]
    block_start = {}
    cursor = 0
    for a in ordered:
        block_start[a] = cursor
        cursor += len(members[a])
    owners = {}
    for a in ordered:
        for local, object_id in enumerate(members[a]):
            owners[object_id] = block_start[a] + local

    roots = [a for a in attributes if not parents[a]]

    order = []
    state = set()

    def visit(a):
        if a in state:
            return
        state.add(a)
        for c in children[a]:
            visit(c)
        order.append(a)

    for r in roots:
        visit(r)
    for a in attributes:
        visit(a)

    cap_entries = max(1, args.entries_per_attribute)
    gpu = faiss.StandardGpuResources()

    def cluster(nodes, seed):
        """round(sqrt(n)) proxies: original vectors nearest to the k-means centres."""
        n = len(nodes)
        k = max(1, int(round(np.sqrt(n))))
        if k >= n:
            return np.asarray(nodes, dtype=np.int64)
        samples = max(4096, args.sample_cap * k)
        if n > samples:
            rng = np.random.default_rng(seed)
            pick = rng.choice(n, size=samples, replace=False)
            pick.sort()
            train = np.asarray(nodes, dtype=np.int64)[pick]
        else:
            train = np.asarray(nodes, dtype=np.int64)
        vectors = np.ascontiguousarray(base[train], dtype=np.float32)
        index = faiss.index_cpu_to_gpu(gpu, 0, faiss.IndexFlatL2(args.dim))
        kmeans = faiss.Clustering(args.dim, k)
        kmeans.niter = args.iterations
        kmeans.seed = int(seed % 2147483647)
        kmeans.max_points_per_centroid = 10 ** 9
        kmeans.train(vectors, index)
        centroids = faiss.vector_to_array(kmeans.centroids).reshape(k, args.dim)
        # nearest training vector for every centroid
        assign_index = faiss.index_cpu_to_gpu(gpu, 0, faiss.IndexFlatL2(args.dim))
        assign_index.add(centroids)
        _, nearest = assign_index.search(vectors, 1)
        proxies = train[nearest[:, 0]]
        return np.unique(proxies)

    entries = {}
    links = set()
    accumulated = []
    previous_proxies = np.empty(0, dtype=np.int64)

    for a in order:
        own = np.asarray([owners[o] for o in members[a]], dtype=np.int64)
        if own.size == 0:
            continue
        own_proxies = cluster(own, args.seed + 1000003 * (a + 1))
        if own_proxies.size:
            picks = np.linspace(0, own_proxies.size - 1, num=min(cap_entries, own_proxies.size))
            entries[a] = [int(own_proxies[int(round(p))]) for p in picks]
        else:
            entries[a] = [int(own[0])]
        if not accumulated:
            accumulated = own.tolist()
            previous_proxies = own_proxies
            continue

        acc_nodes = np.asarray(accumulated, dtype=np.int64)
        acc_proxies = cluster(acc_nodes, args.seed + 1000003 * (a + 1) * 31)
        if acc_proxies.size and own_proxies.size:
            vectors_from = base[acc_proxies]
            vectors_to = base[own_proxies]
            distances = ((vectors_from[:, None, :] - vectors_to[None, :, :]) ** 2).sum(axis=2)
            take = np.argsort(distances, axis=1)[:, :args.links_per_proxy]
            for row, source in enumerate(acc_proxies):
                for column in take[row]:
                    target = int(own_proxies[column])
                    if int(source) == target:
                        continue
                    links.add((min(int(source), target), max(int(source), target)))
        accumulated.extend(own.tolist())
        previous_proxies = acc_proxies

    with open(args.out, 'wb') as handle:
        handle.write(struct.pack('<III', 0x41534D48, len(attributes), cap_entries))
        for a in attributes:
            chosen = entries.get(a, [])
            handle.write(struct.pack('<I', len(chosen)))
            for node in chosen:
                handle.write(struct.pack('<I', int(node)))
        handle.write(struct.pack('<I', len(links)))
        for left, right in sorted(links):
            handle.write(struct.pack('<II', left, right))

    print(f'attributes {len(attributes)} with objects {len(ordered)} '
          f'links {len(links)} accumulated {len(accumulated)}')
    print(f'wrote {args.out}')


if __name__ == '__main__':
    main()
