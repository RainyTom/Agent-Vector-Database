#!/usr/bin/env python3
"""Generate complete original-WIKI subtree / occupancy-matched topology pairs.

The default matrix has 12 configurations: roots 410 and 3626, native and
balanced parent edges, and requested Q/U .01, .1, 1. This is NOT full WIKI.
Only the C++ dynamic-triad runner performs search, GT, and benchmark timing.
Dry-run prints the plan without reading source files or creating any files.
Existing generation directories/manifests are never overwritten.
"""

import argparse
from array import array
from collections import Counter
import json
import math
import os
from pathlib import Path
import random
import shutil
import sys

# Keep the no-writes dry-run promise even when helper bytecode is not cached.
sys.dont_write_bytecode = True

from generate_dynamic_triad import (DEFAULT_OUTPUT, DEFAULT_WORK, FORMAT_VERSION,
                                    ROOT, Pool, State, json_hash, number,
                                    sha256_file, tag_mask, write_json)

SOURCE_ROOT = ROOT / "codes/directory-aware-vector-attr-search/data/wiki_full"
DEFAULT_COHORTS = (410, 3626)
RATIOS = (.01, .1, 1.)
THRESHOLD = 512
AUDITED = {
    410: {"n_live": 11964, "initial_hnsw_node_count": 0, "density_stratum": "sparse"},
    3626: {"n_live": 11869, "initial_hnsw_node_count": 2, "density_stratum": "graph_present"},
    828: {"n_live": 48403, "initial_hnsw_node_count": 11, "density_stratum": "optional_scale"},
}
FAMILIES = ("root_recursive", "broad_recursive", "narrow_recursive",
            "nonempty_nonrecursive", "empty_nonrecursive")
DATA_FILES = ("base.f32", "test.f32", "tree.txt", "bindings.txt", "attrs.txt", "meta.txt")
CLAIM = "complete original rooted-subtree cohort, not full WIKI"


def rng_for(seed, root, purpose):
    """Purpose-separated seeds do not depend on plan order or requested Q/U."""
    return random.Random(int(json_hash([seed, root, purpose]), 16))


def read_source(tree_path, binding_path, base_path, test_path, dim):
    """Validate every source edge and binding, without loading vector values."""
    row_bytes = 4 * dim
    if base_path.stat().st_size % row_bytes or test_path.stat().st_size % row_bytes:
        raise ValueError("WIKI raw vector byte length must be divisible by dim*4")
    vector_count = base_path.stat().st_size // row_bytes
    query_count = test_path.stat().st_size // row_bytes
    if not vector_count or not query_count:
        raise ValueError("empty source vector file")
    parents = array("i")
    with tree_path.open() as handle:
        node_count = int(handle.readline())
        if node_count < 1:
            raise ValueError("source tree must contain a root")
        for line_number, line in enumerate(handle, 2):
            fields = line.split()
            if len(fields) != 2:
                raise ValueError(f"tree line {line_number}: expected node parent")
            node, parent = map(int, fields)
            if node != len(parents) or node >= node_count:
                raise ValueError("source tree IDs must be contiguous in increasing order")
            if (node == 0 and parent != -1) or (node > 0 and not 0 <= parent < node):
                raise ValueError("source tree requires root 0/-1 and parent < child")
            parents.append(parent)
    if len(parents) != node_count:
        raise ValueError("source tree header/body node counts differ")
    bindings = array("i")
    direct = array("q", [0]) * node_count
    with binding_path.open() as handle:
        for line_number, line in enumerate(handle, 1):
            fields = line.split()
            if len(fields) != 1:
                raise ValueError(f"binding row {line_number}: expected one directory ID")
            node = int(fields[0])
            if not 0 <= node < node_count:
                raise ValueError(f"binding row {line_number}: unknown directory {node}")
            bindings.append(node)
            direct[node] += 1
    if len(bindings) != vector_count:
        raise ValueError(f"source binding rows {len(bindings)} != vector rows {vector_count}")
    subtree_items = array("q", direct)
    subtree_nodes = array("q", [1]) * node_count
    for node in range(node_count - 1, 0, -1):
        parent = parents[node]
        subtree_items[parent] += subtree_items[node]
        subtree_nodes[parent] += subtree_nodes[node]
    return {"parents": parents, "bindings": bindings, "direct": direct,
            "subtree_items": subtree_items, "subtree_nodes": subtree_nodes,
            "vector_count": vector_count, "query_count": query_count}


def extract_cohorts(source, roots):
    """Assign subtree membership once, then collect vector rows in one pass."""
    parents = source["parents"]
    for root in roots:
        if not 0 <= root < len(parents):
            raise ValueError(f"cohort root {root} is absent from source tree")
    # A bit mask also handles explicitly requested nested roots without repeatedly
    # scanning the full source binding table or climbing each item's ancestors.
    root_bits = {root: 1 << index for index, root in enumerate(roots)}
    memberships = [0] * len(parents)
    nodes = {root: [] for root in roots}
    for node, parent in enumerate(parents):
        mask = (memberships[parent] if parent >= 0 else 0) | root_bits.get(node, 0)
        memberships[node] = mask
        while mask:
            bit = mask & -mask
            nodes[roots[bit.bit_length() - 1]].append(node)
            mask ^= bit
    rows = {root: [] for root in roots}
    for row, node in enumerate(source["bindings"]):
        mask = memberships[node]
        while mask:
            bit = mask & -mask
            rows[roots[bit.bit_length() - 1]].append(row)
            mask ^= bit
    cohorts = {}
    for root in roots:
        original_nodes = nodes[root]
        mapping = {node: local for local, node in enumerate(original_nodes)}
        native = [-1] + [mapping[parents[node]] for node in original_nodes[1:]]
        bindings = [mapping[source["bindings"][row]] for row in rows[root]]
        if len(bindings) != source["subtree_items"][root] or len(native) != source["subtree_nodes"][root]:
            raise ValueError(f"root {root}: complete-subtree cardinality mismatch")
        counts = [source["direct"][node] for node in original_nodes]
        graph_nodes = sum(count > THRESHOLD for count in counts)
        if root in AUDITED:
            expected = AUDITED[root]
            if len(bindings) != expected["n_live"] or graph_nodes != expected["initial_hnsw_node_count"]:
                raise ValueError(f"root {root}: source does not match preregistered cohort {expected}")
        if len(bindings) < 2 or sum(count > 0 for count in counts) < 2 or not any(count == 0 for count in counts):
            raise ValueError(f"root {root}: need items in two direct nodes and an untouched empty direct node")
        cohorts[root] = {"root": root, "original_nodes": original_nodes,
                         "source_rows": rows[root], "parents": native,
                         "bindings": bindings, "direct_counts": counts,
                         "initial_hnsw_node_count": graph_nodes}
    return cohorts


class ScopeIndex:
    """Preorder intervals and a Fenwick tree over current direct occupancies.

    This is scope sampling only, not predicate evaluation or ground truth.
    Construction is O(nodes), and moves/draws cost O(log(nodes)).
    """
    def __init__(self, parents, direct):
        children = [[] for _ in parents]
        for node in range(1, len(parents)):
            children[parents[node]].append(node)
        self.order = []
        self.start = [0] * len(parents)
        self.stop = [0] * len(parents)
        stack = [(0, False)]
        while stack:
            node, closing = stack.pop()
            if closing:
                self.stop[node] = len(self.order)
            else:
                self.start[node] = len(self.order)
                self.order.append(node)
                stack.append((node, True))
                stack.extend((child, False) for child in reversed(children[node]))
        self.tree = [0] + [direct[node] for node in self.order]
        for index in range(1, len(self.tree)):
            parent = index + (index & -index)
            if parent < len(self.tree):
                self.tree[parent] += self.tree[index]

    def prefix(self, end):
        total = 0
        while end:
            total += self.tree[end]
            end -= end & -end
        return total

    def count(self, node):
        return self.prefix(self.stop[node]) - self.prefix(self.start[node])

    def add(self, node, delta):
        index = self.start[node] + 1
        while index < len(self.tree):
            self.tree[index] += delta
            index += index & -index

    def sample_node(self, anchor, rng):
        count = self.count(anchor)
        if not count:
            return None
        rank = self.prefix(self.start[anchor]) + rng.randrange(count)
        index = 0
        step = 1 << (len(self.order).bit_length() - 1)
        while step:
            candidate = index + step
            if candidate < len(self.tree) and self.tree[candidate] <= rank:
                index = candidate
                rank -= self.tree[candidate]
            step >>= 1
        return self.order[index]


def topology_stats(parents, direct):
    depths = [0] * len(parents)
    scopes = list(direct)
    child_counts = [0] * len(parents)
    for node in range(1, len(parents)):
        depths[node] = depths[parents[node]] + 1
        child_counts[parents[node]] += 1
    for node in range(len(parents) - 1, 0, -1):
        scopes[parents[node]] += scopes[node]
    return {"depth": max(depths), "n_leaves": sum(count == 0 for count in child_counts),
            "subtree_items": scopes}


def select_anchors(cohort, seed):
    counts = cohort["direct_counts"]
    sizes = topology_stats(cohort["parents"], counts)["subtree_items"]
    n_live = len(cohort["bindings"])
    candidates = [node for node in range(1, len(counts)) if sizes[node] > 0]
    # Eight nearest nonroot anchors define stable target buckets, selected on
    # initial native cardinality only, never on observed search performance.
    broad = sorted(candidates, key=lambda node: (abs(sizes[node] - .1 * n_live), node))[:8]
    narrow = sorted(candidates, key=lambda node: (abs(sizes[node] - .01 * n_live), node))[:8]
    empty = rng_for(seed, cohort["root"], "empty-anchor").choice(
        [node for node, count in enumerate(counts) if not count])
    return {"broad": broad, "narrow": narrow, "empty": empty,
            "policy": "up to 8 nearest positive nonroot native subtree counts to 10% / 1% of cohort N",
            "native_initial_items": {str(node): sizes[node] for node in sorted({0, empty, *broad, *narrow})},
            "broad_target_items": .1 * n_live, "narrow_target_items": .01 * n_live}


def move_stream(cohort, seed):
    bindings = cohort["bindings"]
    destinations = [node for node, count in enumerate(cohort["direct_counts"]) if count]
    position = {node: index for index, node in enumerate(destinations)}
    rng = rng_for(seed, cohort["root"], "updates")
    selected = rng.sample(range(len(bindings)), math.ceil(.5 * len(bindings)))
    updates = []
    for item in selected:
        source_position = position[bindings[item]]
        choice = rng.randrange(len(destinations) - 1)
        destination = destinations[choice + (choice >= source_position)]
        updates.append(("M", item, destination))
    return updates


def query_event(state, index, query_vectors, anchors, scopes, nonempty, rng):
    family = index % 5
    if family == 0:
        anchor, recursive = 0, 1
    elif family in (1, 2):
        anchor, recursive = rng.choice(anchors["broad" if family == 1 else "narrow"]), 1
    elif family == 3:
        anchor, recursive = nonempty.choose(rng), 0
    else:
        anchor, recursive = anchors["empty"], 0
    node = scopes.sample_node(anchor, rng) if recursive else (anchor if len(state.nodes[anchor]) else None)
    item = state.nodes[node].choose(rng) if node is not None else state.live.choose(rng)
    category, numeric, mask, _ = state.attrs[item]
    # Preserve generate_dynamic_triad.query_event's scalar/tag interpretation
    # and body format. Five scope families x five tagmodes x two combines give
    # an independent 50-query cycle (tagmode0 is no tag filter).
    tagmode = (index // 5) % 5
    combine = (index // 25) % 2
    if tagmode == 1:
        query_mask = 1 << rng.choice([bit for bit in range(16) if mask & (1 << bit)])
    elif tagmode in (2, 4):
        query_mask = mask | (1 << rng.randrange(16))
    elif tagmode == 3:
        query_mask = mask
    else:
        query_mask = 0
    category = category if index % 3 else -1
    low, high = (max(0, numeric - 100), min(999, numeric + 100)) if index % 3 != 1 else (-1, -1)
    if family == 3 and not len(state.nodes[anchor]):
        raise ValueError("nonempty direct query anchor became empty")
    if family == 4 and len(state.nodes[anchor]):
        raise ValueError("untouched empty query anchor is no longer empty")
    return ("Q", index % query_vectors, recursive, anchor, category, low, high,
            tagmode, query_mask, combine)


def write_paired_trace(path, cohort, attrs, updates, anchors, ratio, query_vectors, seed):
    n_updates = len(updates)
    n_queries = max(1, round(n_updates * ratio))
    state = State(cohort["parents"], cohort["bindings"], attrs)
    scopes = ScopeIndex(cohort["parents"], cohort["direct_counts"])
    nonempty = Pool(node for node, count in enumerate(cohort["direct_counts"]) if count)
    rng = rng_for(seed, cohort["root"], "queries")
    schedule = Counter()
    anchors_used = {family: Counter() for family in FAMILIES}
    count = 0
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x") as handle:
        handle.write(f"{n_updates + n_queries}\n")
        for gap in range(n_updates + 1):
            desired = (gap + 1) * n_queries // (n_updates + 1)
            while count < desired:
                event = query_event(state, count, query_vectors, anchors, scopes, nonempty, rng)
                handle.write(" ".join(map(str, event)) + "\n")
                family = FAMILIES[count % 5]
                schedule[family] += 1
                schedule[f"tagmode_{event[7]}"] += 1
                schedule[f"combine_{event[9]}"] += 1
                anchors_used[family][event[3]] += 1
                count += 1
            if gap < n_updates:
                event = updates[gap]
                _, item, destination = event
                source = state.bindings[item]
                state.apply(event)
                scopes.add(source, -1)
                scopes.add(destination, 1)
                if not len(state.nodes[source]):
                    nonempty.remove(source)
                nonempty.add(destination)
                handle.write(" ".join(map(str, event)) + "\n")
    return {
        "n_updates": n_updates, "n_queries": n_queries, "n_events": n_updates + n_queries,
        "requested_query_events": n_queries, "target_ratio_query_events": n_updates * ratio,
        "rounded_ratio_query_events": round(n_updates * ratio), "actual_q_u": n_queries / n_updates,
        "query_vector_count": query_vectors, "actual_distinct_query_ids": min(n_queries, query_vectors),
        "actual_query_vector_ids_used": list(range(min(n_queries, query_vectors))),
        "query_vectors_reused": n_queries > query_vectors,
        "query_cap": None, "query_cap_applied": False, "query_event_floor": 1,
        "query_floor_applied": round(n_updates * ratio) < 1,
        "full_scope_tag_combine_schedule": n_queries >= 50,
        "query_family_counts": {family: schedule[family] for family in FAMILIES},
        "query_schedule_counts": dict(schedule),
        "query_anchor_counts": {family: {str(node): value for node, value in counts.items()}
                                for family, counts in anchors_used.items()},
        "visibility": "serialized synchronous commits; each Q observes all preceding updates",
    }


def link_file(source, destination):
    try:
        os.link(source, destination)
    except OSError:
        shutil.copyfile(source, destination)


def write_cohort(cohort, source, source_paths, source_hashes, args, run_dir):
    # Source vector access is deliberately delayed until after planning,
    # complete binding validation, and preregistered-cohort checks.
    import numpy as np

    root = cohort["root"]
    shared = run_dir / f"root-{root}" / "shared"
    shared.mkdir(parents=True)
    n_live = len(cohort["bindings"])
    base = np.memmap(source_paths["base"], mode="r", dtype="<f4",
                     shape=(source["vector_count"], args.wiki_dim))
    test = np.memmap(source_paths["test"], mode="r", dtype="<f4",
                     shape=(source["query_count"], args.wiki_dim))
    # Bounded copies preserve original row order and avoid a second full cohort
    # allocation. Every selected row is live; pure M needs no reserve vectors.
    with (shared / "base.f32").open("xb") as handle:
        for offset in range(0, n_live, 1024):
            np.asarray(base[cohort["source_rows"][offset:offset + 1024]], dtype="<f4").tofile(handle)
    np.asarray(test[:args.query_vectors], dtype="<f4").tofile(shared / "test.f32")
    del base, test
    rng = rng_for(args.seed, root, "attributes")
    attrs = [[rng.randrange(8), rng.randrange(1000), tag_mask(rng), 1] for _ in range(n_live)]
    (shared / "bindings.txt").write_text("".join(f"{node}\n" for node in cohort["bindings"]))
    (shared / "attrs.txt").write_text("".join(" ".join(map(str, row)) + "\n" for row in attrs))
    (shared / "meta.txt").write_text(f"{n_live} {args.wiki_dim} {args.query_vectors}\n")
    selection_path = shared / "source_rows.json"
    write_json(selection_path, {"base": cohort["source_rows"], "queries": list(range(args.query_vectors))})
    mapping_path = shared / "original_to_local.json"
    write_json(mapping_path, {"source_root": root, "nodes": [
        {"original": original, "local": local,
         "original_parent": source["parents"][original], "local_parent": cohort["parents"][local]}
        for local, original in enumerate(cohort["original_nodes"])]})
    counts = cohort["direct_counts"]
    metadata = {
        "claim_boundary": CLAIM, "cohort_root": root, "cohort": f"root-{root}",
        "density_stratum": AUDITED.get(root, {}).get("density_stratum", "explicit_root"),
        "cohort_selection": "preregistered nearest-to-12000 complete-subtree density strata; no performance-based selection"
                            if root in DEFAULT_COHORTS else "explicit optional complete rooted subtree",
        "n_live": n_live, "n_vectors": n_live, "reserve": 0, "n_query_vectors": args.query_vectors,
        "dim": args.wiki_dim, "n_nodes": len(counts), "seed": args.seed,
        "initial_hnsw_node_count": cohort["initial_hnsw_node_count"],
        "hnsw_count_definition": "direct live occupancy > 512; local per-node graphs only, not B/C global graphs",
        "density_stats": {"scan_threshold": THRESHOLD, "nonempty_direct_nodes": sum(count > 0 for count in counts),
                          "empty_direct_nodes": counts.count(0), "max_direct_items": max(counts),
                          "direct_items_above_threshold": sum(count for count in counts if count > THRESHOLD)},
        "original_node_mapping": {"path": str(mapping_path), "sha256": sha256_file(mapping_path)},
        "source_selection_path": str(selection_path), "source_selection_sha256": sha256_file(selection_path),
        "vector_sampling": "ALL source rows bound in original rooted subtree, retained in original vector-row order",
        "query_vector_selection": "original test rows 0..query_vectors-1, used in order cyclically",
        "provenance": {"source_root": str(args.source_root.resolve()), "source_vector_rows": source["vector_count"],
                       "source_binding_rows": len(source["bindings"]), "source_nodes": len(source["parents"]),
                       "source_query_rows": source["query_count"], "source_dim": args.wiki_dim,
                       "source_files": {key: {"path": str(path), "bytes": path.stat().st_size,
                                              "sha256": source_hashes[key]} for key, path in source_paths.items()},
                       "node_mapping_sha256": sha256_file(mapping_path),
                       "source_selection_sha256": sha256_file(selection_path)},
    }
    fixture_details = {}
    for topology in ("native", "balanced"):
        parents = cohort["parents"] if topology == "native" else [-1] + [(node - 1) // 4 for node in range(1, len(counts))]
        stats = topology_stats(parents, counts)
        directory = run_dir / f"root-{root}" / topology
        directory.mkdir()
        for name in DATA_FILES:
            if name != "tree.txt":
                link_file(shared / name, directory / name)
        with (directory / "tree.txt").open("x") as handle:
            handle.write(f"{len(parents)}\n")
            for node, parent in enumerate(parents):
                handle.write(f"{node} {parent}\n")
        hashes = {name: sha256_file(directory / name) for name in DATA_FILES}
        details = dict(metadata, topology=topology, depth=stats["depth"], n_leaves=stats["n_leaves"],
                       hierarchy_label=("original WIKI subtree hierarchy" if topology == "native" else
                                        "occupancy-matched balanced 4-ary counterfactual; not original hierarchy"),
                       file_sha256=hashes, data_sha256=json_hash(hashes))
        write_json(directory / "meta.json", details)
        fixture_details[topology] = (directory, details, stats["subtree_items"])
    return attrs, fixture_details


def planned_configs(args):
    return [{"id": f"wiki-root{root}-{topology}-item_move-r0p5-qu{number(ratio)}",
             "dataset": "wiki", "cohort": f"root-{root}", "cohort_root": root, "topology": topology,
             "locality": "uniform", "update_type": "item_move", "update_rate": .5,
             "target_q_u": ratio, "sweeps": ["wiki_topology", "item_move_crossover"], "seed": args.seed}
            for root in args.cohorts for topology in ("native", "balanced") for ratio in args.ratios]


def parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cohorts", nargs="+", type=int, default=list(DEFAULT_COHORTS),
                    help="explicit original subtree roots; defaults 410 3626; 828 is optional scale")
    ap.add_argument("--ratios", nargs="+", type=float, default=list(RATIOS))
    ap.add_argument("--seed", type=int, default=20261029)
    ap.add_argument("--query-vectors", type=int, default=456, help="original test row prefix, 1..456 (default all 456)")
    ap.add_argument("--work-root", type=Path, default=DEFAULT_WORK / "wiki-topology")
    ap.add_argument("--tracked-root", type=Path, default=DEFAULT_OUTPUT / "wiki-topology")
    ap.add_argument("--source-root", type=Path, default=SOURCE_ROOT)
    ap.add_argument("--wiki-base", type=Path)
    ap.add_argument("--wiki-test", type=Path)
    ap.add_argument("--wiki-tree", type=Path)
    ap.add_argument("--wiki-bindings", type=Path)
    ap.add_argument("--wiki-dim", type=int, default=1024)
    ap.add_argument("--dry-run", action="store_true", help="print plan; no source reads, vector loads, or writes")
    return ap


def main(argv=None):
    args = parser().parse_args(argv)
    if not 1 <= args.query_vectors <= 456 or args.wiki_dim < 1:
        raise ValueError("require query-vectors in 1..456 and positive wiki-dim")
    if len(set(args.cohorts)) != len(args.cohorts) or any(root < 0 for root in args.cohorts):
        raise ValueError("cohort roots must be distinct nonnegative original IDs")
    if len(set(args.ratios)) != len(args.ratios) or any(not math.isfinite(ratio) or ratio <= 0 for ratio in args.ratios):
        raise ValueError("ratios must be distinct finite positive values")
    source_paths = {key: (getattr(args, "wiki_" + key) or args.source_root / name).resolve()
                    for key, name in (("base", "base.f32"), ("test", "test.f32"),
                                      ("tree", "wiki_tree.txt"), ("bindings", "wiki_bindings.txt"))}
    configs = planned_configs(args)
    identity = {"cohorts": args.cohorts, "ratios": args.ratios, "seed": args.seed,
                "query_vectors": args.query_vectors, "dim": args.wiki_dim,
                "sources": {key: str(path) for key, path in source_paths.items()}}
    label = f"wiki-topology-s{args.seed}-q{args.query_vectors}-{json_hash(identity)[:12]}"
    work = args.work_root.resolve()
    output = args.tracked_root.resolve()
    run_dir = work / label
    manifest_path = output / f"manifest-{label}.json"
    manifest = {
        "format_version": FORMAT_VERSION, "generated": False, "suite": "wiki-topology",
        "design": "paired complete original-WIKI subtrees and occupancy-matched balanced counterfactuals",
        "claim_boundary": CLAIM, "seed": args.seed, "reserve": 0, "query_vectors": args.query_vectors,
        "work_dir": str(work), "output_dir": str(output), "generation_dir": str(run_dir),
        "manifest_path": str(manifest_path), "source_paths": identity["sources"],
        "factor_levels": {"datasets": ["wiki"], "cohort_roots": args.cohorts,
                          "topologies": ["native", "balanced"], "localities": ["uniform"],
                          "update_types": ["item_move"], "update_rates": [.5], "target_q_u": args.ratios},
        "selection": {"planned_count": len(configs), "selected_count": len(configs)},
        "planned_configs": configs, "configs": configs,
        "preregistered_cohorts": {str(root): AUDITED[root] for root in args.cohorts if root in AUDITED},
        "rate_definition": "ceil(.5*N) unique initially live items, one nonnoop M each, without replacement",
        "query_policy": "max(1, round(number_of_moves * requested_Q/U)); no cap; 5 scopes x 5 tagmodes x 2 combines cycle50",
        "query_vector_policy": "all 456 original query vectors by default; original-row prefix pooled and used cyclically; actual used IDs disclosed",
        "update_stream_policy": "uniform initial-nonempty direct destination excluding source; independent attribute/update/query RNGs; same full M list at every ratio",
        "topology_policy": "same original-to-local IDs, direct occupancies, vectors, attrs, bindings and literal M+Q; only parent edges differ; balanced parent(i)=(i-1)//4",
        "scope_policy": "anchors selected on initial native scopes; current nonempty direct queries; untouched initial empty direct anchor never a destination",
        "scope_comparability": "recursive anchor legal sets need NOT match across topologies; C++ computes exact current-state GT separately",
        "execution": "generation only; all search/GT/timing in unchanged C++ runner; serialized committed-state queries",
        "generation_command": [sys.executable, str(Path(__file__).resolve()), *(argv if argv is not None else sys.argv[1:])],
    }
    if args.dry_run:
        print(json.dumps(manifest, indent=2, sort_keys=True))
        return 0
    if run_dir.exists() or manifest_path.exists():
        raise FileExistsError(f"refusing to overwrite existing generation: {run_dir} or {manifest_path}")
    source = read_source(source_paths["tree"], source_paths["bindings"], source_paths["base"], source_paths["test"], args.wiki_dim)
    if args.query_vectors > source["query_count"]:
        raise ValueError("source test vector file does not contain requested original query rows")
    cohorts = extract_cohorts(source, args.cohorts)
    source_hashes = {key: sha256_file(path) for key, path in source_paths.items()}
    run_dir.mkdir(parents=True, exist_ok=False)
    generated = []
    for root in args.cohorts:
        cohort = cohorts[root]
        attrs, fixtures = write_cohort(cohort, source, source_paths, source_hashes, args, run_dir)
        anchors = select_anchors(cohort, args.seed)
        anchors["balanced_initial_items"] = {str(node): fixtures["balanced"][2][node]
                                                for node in map(int, anchors["native_initial_items"])}
        updates = move_stream(cohort, args.seed)
        n_live = len(cohort["bindings"])
        coverage = len(updates) / n_live
        update_details = {
            "target_unique_items": math.ceil(.5 * n_live), "actual_unique_items": len(updates),
            "actual_unique_coverage": coverage, "coverage_target_met": True, "coverage_overshoot_items": 0,
            "coverage_definition": "unique initially-live item IDs affected / initial live N",
            "unique_affected_all_ids": len(updates), "initial_coverage_by_op": {"M": len(updates)},
            "affected_union_by_op": {"M": len(updates)}, "update_counts": {"M": len(updates)},
            "update_stream_sha256": json_hash(updates), "operation_mixture": ["item_move"],
            "initial_live_count": n_live, "minimum_live_count": n_live, "final_live_count": n_live,
        }
        traces = {}
        for ratio in args.ratios:
            trace = run_dir / f"root-{root}" / "traces" / f"qu{number(ratio)}.txt"
            trace_details = write_paired_trace(trace, cohort, attrs, updates, anchors, ratio, args.query_vectors, args.seed)
            traces[ratio] = (trace, trace_details, sha256_file(trace))
        for config in (config for config in configs if config["cohort_root"] == root):
            directory, details, _ = fixtures[config["topology"]]
            trace, query_details, trace_hash = traces[config["target_q_u"]]
            generated.append(dict(config, **update_details, **query_details,
                                  depth=details["depth"], initial_depth=details["depth"],
                                  final_depth=details["depth"], max_observed_depth=details["depth"],
                                  n_live=n_live, n_vectors=n_live, dim=args.wiki_dim, n_nodes=details["n_nodes"],
                                  data_dir=str(directory), trace=str(trace), trace_sha256=trace_hash,
                                  data_sha256=details["data_sha256"], data_file_sha256=details["file_sha256"],
                                  provenance=details["provenance"], original_node_mapping=details["original_node_mapping"],
                                  source_selection_sha256=details["source_selection_sha256"],
                                  initial_hnsw_node_count=details["initial_hnsw_node_count"],
                                  initial_local_hnsw_node_count=details["initial_hnsw_node_count"],
                                  density_stratum=details["density_stratum"], density_stats=details["density_stats"],
                                  query_anchors=anchors, empty_direct_node=anchors["empty"],
                                  hierarchy_label=details["hierarchy_label"], claim_boundary=CLAIM))
    manifest["configs"] = generated
    manifest["generated"] = True
    manifest["source_sha256"] = source_hashes
    write_json(manifest_path, manifest)
    print(json.dumps({"manifest": str(manifest_path), "configs": len(generated),
                      "cohort_items": {str(root): len(cohorts[root]["bindings"]) for root in args.cohorts},
                      "claim_boundary": CLAIM}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
