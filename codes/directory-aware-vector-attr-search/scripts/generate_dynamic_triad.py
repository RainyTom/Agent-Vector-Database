#!/usr/bin/env python3
"""Generate real-vector, synthetic-hierarchy dynamic-triad inputs (no ANN or GT).

Examples (run from any directory)::
  python scripts/generate_dynamic_triad.py --dry-run
  python scripts/generate_dynamic_triad.py --smoke --suite screening
  python scripts/generate_dynamic_triad.py --suite crossover --n-live 12000

The factorized design is NOT full factorial. Primary crossover: depth3/mixed/
uniform, rates 0,.1,.2,.5,.8 and requested Q/U .25,1,4. Independent screening
sweeps cover all seven update types and depth3/7 x uniform/zipf/shift at .2/.8.
Equivalent zero-update Q-only configurations are coalesced. Filters select from
that plan explicitly; the manifest retains the complete planned factor coverage.
Mixed means an equal round-robin of scalar, tag, item move and delete/insert
units; B/R are isolated in their own update-type sweeps. D/I are two commits.
This prevents a few structural operations from collapsing the primary Q/U axis.

Rate means distinct INITIAL live items touched / initial live N, not event
fraction. Structural/batch operations can overshoot; their affected-item union,
operation budgets, achieved coverage and actual Q/U are recorded separately.
Query vectors are reused; --max-queries caps events, not distinct vectors.
All event decisions simulate the committed state. Only the C++ runner performs
indexing, predicate evaluation for GT, distance calculations, and timing.
"""

import argparse
import fnmatch
import hashlib
import json
import math
import os
from pathlib import Path
import random
import sys
from collections import Counter

ROOT = Path(__file__).resolve().parents[3]
EXPERIMENT = (ROOT / "researches/nested-attr-vector-search/"
              "08-dynamic-hybrid-vector-search/2026-10-29-exps")
DEFAULT_WORK = EXPERIMENT / "work/dynamic_triad"
DEFAULT_OUTPUT = EXPERIMENT / "data/dynamic_triad"
UPDATE_TYPES = ("single_scalar", "tag_edit", "item_move", "batch_move",
                "subtree_reparent", "delete_insert", "mixed")
LOCALITIES = ("uniform", "zipf", "shift")
RATES = (0.0, 0.1, 0.2, 0.5, 0.8)
RATIOS = (0.25, 1.0, 4.0)
FORMAT_VERSION = 1


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def json_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True,
                                     separators=(",", ":")).encode()).hexdigest()


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def number(value):
    return f"{value:g}".replace(".", "p")


def configuration_id(config):
    return (f"{config['dataset']}-d{config['depth']}-{config['locality']}-"
            f"{config['update_type']}-r{number(config['update_rate'])}-"
            f"qu{number(config['target_q_u'])}")


def planned_configs(suite="factorized", datasets=("sift", "wiki")):
    """Deduplicate configurations while retaining their originating sweeps."""
    configurations = {}

    def add(dataset, depth, locality, kind, rate, ratio, sweep):
        # A Q-only stream has no Q/U ratio; retain just one physical stream.
        ratio = 1.0 if rate == 0 else ratio
        values = (dataset, depth, locality, kind, rate, ratio)
        if values not in configurations:
            config = dict(zip(("dataset", "depth", "locality", "update_type",
                               "update_rate", "target_q_u"), values))
            config["id"] = configuration_id(config)
            config["sweeps"] = []
            configurations[values] = config
        if sweep not in configurations[values]["sweeps"]:
            configurations[values]["sweeps"].append(sweep)

    for dataset in datasets:
        if suite == "move-crossover":
            for ratio in (0.01, 0.1, 1.0):
                add(dataset, 3, "uniform", "item_move", 0.5, ratio,
                    "item_move_crossover")
            continue
        ratios = RATIOS if suite in ("factorized", "crossover") else (1.0,)
        for rate in RATES:
            for ratio in ratios:
                add(dataset, 3, "uniform", "mixed", rate, ratio,
                    "primary_crossover")
        if suite != "crossover":
            for kind in UPDATE_TYPES:
                add(dataset, 3, "uniform", kind, 0.5, 1.0, "update_type")
            for depth in (3, 7):
                for locality in LOCALITIES:
                    for rate in (0.2, 0.8):
                        add(dataset, depth, locality, "mixed", rate, 1.0,
                            "depth_locality")
    return list(configurations.values())


def select_configs(configs, patterns=(), limit=None):
    selected = [c for c in configs if not patterns or any(
        fnmatch.fnmatchcase(c["id"], pattern) or pattern in c["sweeps"]
        for pattern in patterns)]
    if limit is not None:
        if limit < 1:
            raise ValueError("--limit must be positive")
        selected = selected[:limit]
    if not selected:
        raise ValueError("configuration selection matched no configurations")
    return selected


def hierarchy(depth):
    parents = [-1] + [(node - 1) // 2 for node in range(1, 15)]
    leaves = list(range(7, 15))
    if depth == 7:
        for index, leaf in enumerate(leaves):
            for _ in range(4):
                parents.append(leaf)
                leaf = len(parents) - 1
            leaves[index] = leaf
    if depth not in (3, 7):
        raise ValueError("controlled hierarchy depth must be 3 or 7")
    return parents, leaves


def tag_mask(rng):
    return sum(1 << bit for bit in rng.sample(range(16), 3))


class Pool:
    """Deterministic random-access set, including O(1) removal."""
    def __init__(self, items=()):
        self.items = list(items)
        self.positions = {value: index for index, value in enumerate(self.items)}

    def __len__(self):
        return len(self.items)

    def add(self, value):
        if value not in self.positions:
            self.positions[value] = len(self.items)
            self.items.append(value)

    def remove(self, value):
        position = self.positions.pop(value)
        last = self.items.pop()
        if position < len(self.items):
            self.items[position] = last
            self.positions[last] = position

    def choose(self, rng):
        return self.items[rng.randrange(len(self.items))]


class State:
    def __init__(self, parents, bindings, attrs):
        self.parents = list(parents)
        self.bindings = list(bindings)
        self.attrs = [list(row) for row in attrs]
        self.nodes = [Pool() for _ in parents]
        self.live = Pool()
        self.dead = Pool()
        for item, (node, attr) in enumerate(zip(bindings, attrs)):
            (self.live if attr[3] else self.dead).add(item)
            if attr[3]:
                self.nodes[node].add(item)

    def subtree(self, anchor):
        return [node for node in range(len(self.parents))
                if self.is_descendant(node, anchor)]

    def is_descendant(self, node, anchor):
        while node != -1:
            if node == anchor:
                return True
            node = self.parents[node]
        return False

    def depth(self, node):
        result = 0
        while self.parents[node] != -1:
            result += 1
            node = self.parents[node]
        return result

    def apply(self, event):
        """Validate and commit an event; return exact affected live item IDs."""
        op, *values = event
        if op in ("V", "T", "M", "D"):
            item = values[0]
            if item not in self.live.positions:
                raise ValueError(f"{op} references dead item {item}")
            affected = [item]
        if op == "V":
            item, category, numeric = values
            self.attrs[item][0:2] = [category, numeric]
        elif op == "T":
            item, mask = values
            self.attrs[item][2] = mask
        elif op == "M":
            item, destination = values
            old = self.bindings[item]
            if old == destination:
                raise ValueError("item move must change node")
            self.nodes[old].remove(item)
            self.nodes[destination].add(item)
            self.bindings[item] = destination
        elif op == "B":
            source, destination = values
            subtree = self.subtree(source)
            if destination in subtree:
                raise ValueError("batch destination inside source subtree")
            affected = [item for node in subtree for item in self.nodes[node].items]
            for item in affected:
                self.nodes[self.bindings[item]].remove(item)
                self.nodes[destination].add(item)
                self.bindings[item] = destination
        elif op == "R":
            source, parent = values
            subtree = self.subtree(source)
            if source == 0 or parent in subtree or self.parents[source] == parent:
                raise ValueError("invalid/no-op subtree reparent")
            affected = [item for node in subtree for item in self.nodes[node].items]
            self.parents[source] = parent
        elif op == "D":
            item, = values
            self.live.remove(item)
            self.dead.add(item)
            self.nodes[self.bindings[item]].remove(item)
            self.attrs[item][3] = 0
        elif op == "I":
            item, node, category, numeric, mask = values
            if item not in self.dead.positions:
                raise ValueError("insert references live item")
            self.dead.remove(item)
            self.live.add(item)
            self.nodes[node].add(item)
            self.bindings[item] = node
            self.attrs[item] = [category, numeric, mask, 1]
            affected = [item]
        else:
            raise ValueError(f"unknown update {op}")
        return affected


def initial_metadata(n_live, reserve, depth, seed):
    rng = random.Random(seed + depth * 1009)
    parents, leaves = hierarchy(depth)
    # Node 1 is intentionally empty in DIRECT scope throughout the trace.
    # It is not an extra ninth leaf; descendants still hold live items.
    internal = [node for node in range(len(parents)) if node not in leaves and node != 1]
    leaf_items = round(n_live * 0.8)
    assignment = [leaves[i % len(leaves)] for i in range(leaf_items)]
    assignment += [internal[i % len(internal)] for i in range(n_live - leaf_items)]
    rng.shuffle(assignment)
    bindings = assignment + [leaves[i % len(leaves)] for i in range(reserve)]
    attrs = [[rng.randrange(8), rng.randrange(1000), tag_mask(rng), int(i < n_live)]
             for i in range(n_live + reserve)]
    return parents, leaves, bindings, attrs


def node_weights(nodes, config, progress, ranks):
    if config["locality"] == "zipf":
        return [1 / (ranks[node] + 1) ** 1.2 for node in nodes]
    if config["locality"] == "shift":
        hot_half = 0 if progress < 0.5 else 1
        return [12.0 if ranks[node] // 4 == hot_half else 1.0 for node in nodes]
    return [1.0] * len(nodes)


def leaf_ranks(parents, leaves):
    state = State(parents, [], [])
    return {node: next((rank for rank, leaf in enumerate(leaves)
                        if state.is_descendant(leaf, node)), node % 8)
            for node in range(len(parents))}


def update_stream(config, initial, n_live, max_ops, seed):
    parents, leaves, bindings, attrs = initial
    state = State(parents, bindings, attrs)
    rng = random.Random(seed)
    ranks = leaf_ranks(parents, leaves)
    remaining = Pool(range(n_live))
    remaining_nodes = [Pool() for _ in parents]
    for item in range(n_live):
        remaining_nodes[bindings[item]].add(item)
    covered_by_op = {op: set() for op in "VTMBRDI"}
    affected_by_op = {op: set() for op in "VTMBRDI"}
    all_affected = set()
    subtree_union = set()
    update_counts = Counter()
    events = []
    target = math.ceil(config["update_rate"] * n_live)
    max_depth = config["depth"]
    minimum_live = n_live
    # Structural operations have dedicated sweeps: mixing their broad item
    # coverage into primary crossover would make its Q/U axis meaningless.
    mixed_types = ("single_scalar", "tag_edit", "item_move", "delete_insert")
    minimum_units = len(mixed_types) if config["update_type"] == "mixed" and target else 0
    units = 0

    def choose_item(progress):
        if len(remaining):
            if config["locality"] == "uniform":
                return remaining.choose(rng)
            nodes = [node for node, pool in enumerate(remaining_nodes) if len(pool)]
            node = rng.choices(nodes, weights=node_weights(nodes, config, progress, ranks))[0]
            return remaining_nodes[node].choose(rng)
        nodes = [node for node, pool in enumerate(state.nodes) if len(pool)]
        node = rng.choices(nodes, weights=node_weights(nodes, config, progress, ranks))[0]
        return state.nodes[node].choose(rng)

    def destination(exclude, progress):
        nodes = [node for node in range(len(parents)) if node not in exclude and node != 1]
        return rng.choices(nodes, weights=node_weights(nodes, config, progress, ranks))[0]

    def commit(event):
        nonlocal max_depth, minimum_live
        affected = state.apply(event)
        minimum_live = min(minimum_live, len(state.live))
        op = event[0]
        events.append(event)
        update_counts[op] += 1
        affected_by_op[op].update(affected)
        all_affected.update(affected)
        if op in ("B", "R"):
            subtree_union.update(affected)
        for item in affected:
            if item < n_live:
                covered_by_op[op].add(item)
            if item in remaining.positions:
                remaining.remove(item)
                # Untouched initial IDs have never moved/deleted before this event.
                remaining_nodes[bindings[item]].remove(item)
        if op == "R":
            max_depth = max(max_depth, max(state.depth(node) for node in range(len(parents))))

    while target and (n_live - len(remaining) < target or units < minimum_units):
        kind = (mixed_types[units % len(mixed_types)]
                if config["update_type"] == "mixed" else config["update_type"])
        needed = 2 if kind == "delete_insert" else 1
        if len(events) + needed > max_ops:
            break
        progress = (n_live - len(remaining)) / max(target, 1)
        item = choose_item(progress)
        if kind == "single_scalar":
            old_cat, old_num = state.attrs[item][:2]
            commit(("V", item, (old_cat + rng.randrange(1, 8)) % 8,
                    (old_num + rng.randrange(1, 1000)) % 1000))
        elif kind == "tag_edit":
            old = state.attrs[item][2]
            new = tag_mask(rng)
            while new == old:
                new = tag_mask(rng)
            commit(("T", item, new))
        elif kind == "item_move":
            commit(("M", item, destination({state.bindings[item]}, progress)))
        elif kind in ("batch_move", "subtree_reparent"):
            source = state.bindings[item]
            # Root cannot be moved or have an outside batch destination.
            if source == 0:
                candidates = [node for node in range(1, len(parents))
                              if any(len(state.nodes[desc]) for desc in state.subtree(node))]
                source = rng.choice(candidates)
            parent = state.parents[source]
            if parent not in (-1, 0) and rng.random() < 0.5:
                source = parent
            excluded = set(state.subtree(source))
            if kind == "subtree_reparent":
                excluded.add(state.parents[source])
            # A nonroot source always has root as an outside batch destination;
            # reparenting also needs a destination distinct from the old parent.
            destinations = [node for node in range(len(parents))
                            if node not in excluded and node != 1]
            if not destinations:
                source = next(node for node in range(2, len(parents))
                              if len(state.subtree(node)) == 1)
                excluded = set(state.subtree(source))
                if kind == "subtree_reparent":
                    excluded.add(state.parents[source])
            commit(("B" if kind == "batch_move" else "R", source,
                    destination(excluded, progress)))
        elif kind == "delete_insert":
            commit(("D", item))
            # Consume reserve vectors first, then resurrect a dead original ID.
            reserve_dead = [i for i in state.dead.items if i >= n_live]
            inserted = rng.choice(reserve_dead) if reserve_dead else state.dead.choose(rng)
            commit(("I", inserted, destination(set(), progress), rng.randrange(8),
                    rng.randrange(1000), tag_mask(rng)))
        units += 1
    coverage = n_live - len(remaining)
    return events, {
        "coverage_definition": "unique initial live IDs affected by any update / initial live N",
        "target_unique_items": target,
        "actual_unique_items": coverage,
        "actual_unique_coverage": coverage / n_live,
        "coverage_target_met": coverage >= target,
        "coverage_overshoot_items": max(0, coverage - target),
        "unique_affected_all_ids": len(all_affected),
        "subtree_affected_union_items": len(subtree_union),
        "subtree_initial_affected_union_items": len(subtree_union.intersection(range(n_live))),
        "initial_coverage_by_op": {op: len(ids) for op, ids in covered_by_op.items()},
        "affected_union_by_op": {op: len(ids) for op, ids in affected_by_op.items()},
        "update_counts": dict(update_counts),
        "update_operation_budget": max_ops,
        "update_budget_exhausted": coverage < target and len(events) + 2 > max_ops,
        "mixed_all_types_exercised": (all(update_counts[op] > 0 for op in "VTMDI")
                                      if config["update_type"] == "mixed" and target else None),
        "operation_mixture": list(mixed_types) if config["update_type"] == "mixed" else [config["update_type"]],
        "initial_depth": config["depth"],
        "initial_live_count": n_live,
        "minimum_live_count": minimum_live,
        "final_live_count": len(state.live),
        "locality_shift_policy": "hot leaf ranks 0..3 then 4..7 at half target coverage; query phase uses trace progress",
        "max_observed_depth": max_depth,
        "final_depth": max(state.depth(node) for node in range(len(parents))),
    }


def query_event(state, config, index, n_vectors, progress, rng, ranks):
    scope_kind = index % 4
    if scope_kind == 0:
        anchor, recursive = 0, 1
    elif scope_kind == 1:
        children = [node for node, parent in enumerate(state.parents) if parent == 0]
        anchor, recursive = rng.choice(children), 1
    elif scope_kind == 2:
        parents = set(state.parents)
        leaves = [node for node in range(len(state.parents)) if node not in parents]
        anchor = rng.choices(leaves, weights=node_weights(leaves, config, progress, ranks))[0]
        recursive = 1
    else:
        anchor, recursive = 1, 0  # Permanently empty direct scope, including after R.
    scoped_nodes = state.subtree(anchor) if recursive else [anchor]
    nonempty = [node for node in scoped_nodes if len(state.nodes[node])]
    if nonempty:
        node = rng.choices(nonempty, weights=[len(state.nodes[n]) for n in nonempty])[0]
        item = state.nodes[node].choose(rng)
    else:
        item = state.live.choose(rng)
    category, numeric, mask, _ = state.attrs[item]
    # Independent cycles exercise AND/OR and every tag interpretation in each
    # scope family, rather than coupling one tag interpretation to one scope.
    tagmode = (index // 4) % 5
    combine = (index // 20) % 2
    if tagmode == 1:
        bits = [bit for bit in range(16) if mask & (1 << bit)]
        query_mask = 1 << rng.choice(bits)
    elif tagmode == 2:
        query_mask = mask | (1 << rng.randrange(16))
    elif tagmode == 3:
        query_mask = mask
    elif tagmode == 4:
        query_mask = mask | (1 << rng.randrange(16))
    else:
        query_mask = 0
    category = category if index % 3 else -1
    low, high = (max(0, numeric - 100), min(999, numeric + 100)) if index % 3 != 1 else (-1, -1)
    return ("Q", index % n_vectors, recursive, anchor, category, low, high,
            tagmode, query_mask, combine)


def write_trace(path, config, initial, updates, n_query_vectors, max_queries, min_queries, seed):
    n_updates = len(updates)
    desired = max(min_queries, round(n_updates * config["target_q_u"])) if n_updates else n_query_vectors
    desired = max(1, desired)
    n_queries = min(desired, max_queries) if max_queries else desired
    state = State(initial[0], initial[2], initial[3])
    ranks = leaf_ranks(initial[0], initial[1])
    rng = random.Random(seed ^ 0xA71B)
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    schedule = Counter()
    with path.open("w") as handle:
        handle.write(f"{n_updates + n_queries}\n")
        # Evenly distribute a capped query window across the ENTIRE update
        # stream, including a final-state query, not just its first operations.
        for gap in range(n_updates + 1):
            desired_so_far = (gap + 1) * n_queries // (n_updates + 1)
            while count < desired_so_far:
                event = query_event(state, config, count, n_query_vectors,
                                    gap / max(1, n_updates), rng, ranks)
                handle.write(" ".join(map(str, event)) + "\n")
                schedule[f"scope_{('root', 'wide', 'narrow', 'empty')[count % 4]}"] += 1
                schedule[f"tagmode_{event[7]}"] += 1
                schedule[f"combine_{event[9]}"] += 1
                count += 1
            if gap < n_updates:
                event = updates[gap]
                state.apply(event)
                handle.write(" ".join(map(str, event)) + "\n")
    return {
        "n_updates": n_updates, "n_queries": n_queries,
        "n_events": n_updates + n_queries,
        "requested_query_events": desired,
        "target_ratio_query_events": n_updates * config["target_q_u"] if n_updates else None,
        "rounded_ratio_query_events": round(n_updates * config["target_q_u"]) if n_updates else None,
        "query_vector_count": n_query_vectors,
        "query_vectors_reused": n_queries > n_query_vectors,
        "query_cap": max_queries or None,
        "query_cap_applied": n_queries < desired,
        "query_event_floor": min_queries,
        "query_floor_applied": n_updates > 0 and round(n_updates * config["target_q_u"]) < max(1, min_queries),
        "full_scope_tag_combine_schedule": n_queries >= 40,
        "actual_q_u": n_queries / n_updates if n_updates else None,
        "target_q_u_achieved": n_updates > 0 and abs(n_queries / n_updates - config["target_q_u"]) < 1e-12,
        "query_schedule_counts": dict(schedule),
        "visibility": "serialized synchronous commits; each Q observes all preceding updates",
    }


def sample_vectors(dataset, n_live, reserve, n_queries, seed, work, args):
    # Imports intentionally deferred: planning does not need vector dependencies.
    import numpy as np
    destination = work / "vectors" / f"{dataset}-n{n_live}-r{reserve}-q{n_queries}-s{seed}"
    destination.mkdir(parents=True, exist_ok=True)
    total = n_live + reserve
    rng = random.Random(seed)
    if dataset == "sift":
        import h5py
        source = Path(args.sift_source).resolve()
        with h5py.File(source, "r") as handle:
            base, test = handle["train"], handle["test"]
            if total > len(base) or n_queries > len(test):
                raise ValueError("SIFT source does not contain enough vectors")
            selected = rng.sample(range(len(base)), total)
            query_ids = rng.sample(range(len(test)), n_queries)
            sorted_base, sorted_query = sorted(selected), sorted(query_ids)
            # HDF5 requires increasing fancy indices; restore sample order so
            # live/reserve assignment is not biased toward low source IDs.
            sampled = np.asarray(base[sorted_base], dtype="<f4")
            sampled[np.searchsorted(sorted_base, selected)].tofile(destination / "base.f32")
            sampled_queries = np.asarray(test[sorted_query], dtype="<f4")
            sampled_queries[np.searchsorted(sorted_query, query_ids)].tofile(destination / "test.f32")
            dimension = int(base.shape[1])
            source_count = len(base)
        sources = [source]
    else:
        base_path = Path(args.wiki_base).resolve()
        test_path = Path(args.wiki_test).resolve()
        dimension = args.wiki_dim
        if base_path.stat().st_size % (4 * dimension) or test_path.stat().st_size % (4 * dimension):
            raise ValueError("WIKI raw vector byte length not divisible by dim*4")
        source_count = base_path.stat().st_size // (4 * dimension)
        query_count = test_path.stat().st_size // (4 * dimension)
        if total > source_count or n_queries > query_count:
            raise ValueError("WIKI source does not contain enough vectors")
        selected = rng.sample(range(source_count), total)
        query_ids = rng.sample(range(query_count), n_queries)
        base = np.memmap(base_path, mode="r", dtype="<f4", shape=(source_count, dimension))
        test = np.memmap(test_path, mode="r", dtype="<f4", shape=(query_count, dimension))
        np.asarray(base[selected], dtype="<f4").tofile(destination / "base.f32")
        np.asarray(test[query_ids], dtype="<f4").tofile(destination / "test.f32")
        sources = [base_path, test_path]
    # IDs retain provenance but live only in the ignored raw work directory.
    write_json(destination / "source_rows.json", {"base": selected, "queries": query_ids})
    return destination, {
        "dataset": dataset, "n_live": n_live, "reserve": reserve,
        "n_vectors": total, "n_query_vectors": n_queries, "dim": dimension,
        "vector_source_count": source_count,
        "vector_sources": [{"path": str(path), "bytes": path.stat().st_size} for path in sources],
        "source_selection_sha256": sha256_file(destination / "source_rows.json"),
        "vector_sampling": "deterministic uniform sampling without replacement; random sample order retained",
        "hierarchy_label": ("WIKI-vector subset + controlled SYNTHETIC hierarchy" if dataset == "wiki"
                            else "SIFT-vector subset + controlled SYNTHETIC hierarchy"),
    }


def fixture_directory(dataset, depth, initial, vectors, metadata, work, seed):
    directory = work / "datasets" / f"{dataset}-n{metadata['n_live']}-r{metadata['reserve']}-q{metadata['n_query_vectors']}-d{depth}-s{seed}"
    directory.mkdir(parents=True, exist_ok=True)
    for name in ("base.f32", "test.f32"):
        destination = directory / name
        # Hardlink immutable shared samples across depths; overwrite generation
        # is safe because the vector sampler writes the existing inode itself.
        if destination.exists() and not os.path.samefile(vectors / name, destination):
            destination.unlink()
        if not destination.exists():
            try:
                os.link(vectors / name, destination)
            except OSError:
                import shutil
                shutil.copyfile(vectors / name, destination)
    parents, leaves, bindings, attrs = initial
    with (directory / "tree.txt").open("w") as handle:
        handle.write(f"{len(parents)}\n")
        for node, parent in enumerate(parents):
            handle.write(f"{node} {parent}\n")
    (directory / "bindings.txt").write_text("".join(f"{node}\n" for node in bindings))
    (directory / "attrs.txt").write_text("".join(" ".join(map(str, row)) + "\n" for row in attrs))
    (directory / "meta.txt").write_text(f"{metadata['n_vectors']} {metadata['dim']} {metadata['n_query_vectors']}\n")
    file_hashes = {name: sha256_file(directory / name)
                   for name in ("base.f32", "test.f32", "tree.txt", "bindings.txt", "attrs.txt", "meta.txt")}
    details = dict(metadata, depth=depth, n_nodes=len(parents), n_leaves=len(leaves),
                   empty_direct_node=1, seed=seed, file_sha256=file_hashes,
                   data_sha256=json_hash(file_hashes),
                   live_leaf_items=round(metadata["n_live"] * 0.8),
                   live_internal_items=metadata["n_live"] - round(metadata["n_live"] * 0.8))
    write_json(directory / "meta.json", details)
    return directory, details


def parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--suite", choices=("factorized", "screening", "crossover", "move-crossover"), default="factorized")
    ap.add_argument("--datasets", nargs="+", choices=("sift", "wiki"), default=["sift", "wiki"])
    ap.add_argument("--n-live", type=int, default=12000)
    ap.add_argument("--reserve", type=int, default=1000)
    ap.add_argument("--query-vectors", type=int, default=64)
    ap.add_argument("--max-queries", type=int, default=50000,
                    help="trace query-event cap; 0 disables; actual Q/U always recorded")
    ap.add_argument("--min-queries", type=int, default=64,
                    help="minimum trace query events when U>0; 0 permits exact small Q/U windows")
    ap.add_argument("--max-update-ops", type=int, default=0, help="0 uses 2*N; D/I count separately")
    ap.add_argument("--smoke", action="store_true", help="N=2000 (all planned factor levels retained)")
    ap.add_argument("--seed", type=int, default=20261029)
    ap.add_argument("--select", action="append", default=[], help="config ID glob or sweep name (repeatable)")
    ap.add_argument("--limit", type=int, help="explicit prefix limit AFTER selection; recorded in manifest")
    ap.add_argument("--dry-run", action="store_true", help="print planned manifest, no input reads/writes")
    ap.add_argument("--work-dir", "--work-root", dest="work_dir", type=Path, default=DEFAULT_WORK)
    ap.add_argument("--output-dir", "--tracked-root", dest="output_dir", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--manifest", type=Path, help="default OUTPUT/manifest-SUITE-nN-sSEED.json")
    ap.add_argument("--sift-source", type=Path, default=ROOT / "datasets/sift-128-euclidean.hdf5")
    ap.add_argument("--wiki-base", type=Path, default=ROOT / "codes/directory-aware-vector-attr-search/data/wiki_full/base.f32")
    ap.add_argument("--wiki-test", type=Path, default=ROOT / "codes/directory-aware-vector-attr-search/data/wiki_full/test.f32")
    ap.add_argument("--wiki-dim", type=int, default=1024)
    return ap


def main(argv=None):
    args = parser().parse_args(argv)
    if args.smoke:
        args.n_live = 2000
    if args.n_live < 16 or args.reserve < 1 or args.query_vectors < 1:
        raise ValueError("require N>=16, reserve>=1, query-vectors>=1")
    if args.max_queries < 0 or args.min_queries < 0:
        raise ValueError("query cap/floor must be nonnegative; cap 0 disables")
    if args.max_update_ops < 0 or args.wiki_dim < 1:
        raise ValueError("invalid operation budget or dimension")
    configs = planned_configs(args.suite, args.datasets)
    selected = select_configs(configs, args.select, args.limit)
    work = args.work_dir.resolve()
    output = args.output_dir.resolve()
    manifest_path = (args.manifest or output / f"manifest-{args.suite}-n{args.n_live}-s{args.seed}.json").resolve()
    manifest = {
        "format_version": FORMAT_VERSION,
        "design": "factorized screening plus decisive interactions; NOT full factorial",
        "suite": args.suite, "seed": args.seed, "n_live": args.n_live,
        "reserve": args.reserve, "query_vectors": args.query_vectors,
        "max_queries": args.max_queries or None,
        "min_queries": args.min_queries,
        "max_update_ops": args.max_update_ops or 2 * args.n_live,
        "smoke": args.smoke, "work_dir": str(work), "output_dir": str(output),
        "manifest_path": str(manifest_path),
        "factor_levels": {"datasets": args.datasets, "depths": [3, 7],
                          "localities": list(LOCALITIES), "update_types": list(UPDATE_TYPES),
                          "update_rates": list(RATES), "target_q_u": list(RATIOS)},
        "planned_configs": configs,
        "suite_factor_levels": {key: sorted({config[field] for config in configs})
                               for key, field in (("datasets", "dataset"), ("depths", "depth"),
                                                  ("localities", "locality"), ("update_types", "update_type"),
                                                  ("update_rates", "update_rate"), ("target_q_u", "target_q_u"))},
        "factor_levels_note": "factor_levels is full factor universe; suite_factor_levels is actual planned suite coverage",
        "selection": {"patterns": args.select, "limit": args.limit,
                      "planned_count": len(configs), "selected_count": len(selected)},
        "zero_update_policy": "Q-only configs deduplicated across requested ratios; actual Q/U=null",
        "rate_definition": "unique initially-live item IDs affected / initial live N; not event fraction",
        "query_policy": "Q-only uses query_vectors; otherwise max(min_queries,round(updates*requested Q/U),1), explicit cap; evenly spaced",
        "mixed_policy": "equal round-robin scalar/tag/item_move/delete_insert units; B/R isolated; D/I two commits",
        "update_stream_policy": "independent update/query RNGs; Q/U-invariant updates; uniform/zipf rates are prefixes; shift changes phase at half coverage",
        "hierarchy_policy": "8 leaves; depth7 adds four unary levels per depth3 leaf; node1 empty direct scope",
        "execution": "generation/orchestration Python only; all GT, ANN and timing C++; serialized current-state commits",
        "generated": False, "configs": selected,
        "generation_command": [sys.executable, str(Path(__file__).resolve()), *(argv if argv is not None else sys.argv[1:])],
    }
    if args.dry_run:
        print(json.dumps(manifest, indent=2, sort_keys=True))
        return 0
    dataset_cache = {}
    fixture_cache = {}
    generated = []
    for config in selected:
        dataset, depth = config["dataset"], config["depth"]
        if dataset not in dataset_cache:
            dataset_cache[dataset] = sample_vectors(dataset, args.n_live, args.reserve,
                                                    args.query_vectors, args.seed, work, args)
        if (dataset, depth) not in fixture_cache:
            initial = initial_metadata(args.n_live, args.reserve, depth, args.seed)
            vectors, metadata = dataset_cache[dataset]
            directory, details = fixture_directory(dataset, depth, initial, vectors, metadata, work, args.seed)
            fixture_cache[dataset, depth] = (initial, directory, details)
        initial, directory, details = fixture_cache[dataset, depth]
        # Identical configuration and seed have the same trace regardless of
        # plan order, selection, dataset, or which other configurations ran.
        trace_seed = args.seed ^ int(json_hash({k: v for k, v in config.items()
                                              if k not in ("dataset", "id", "sweeps", "target_q_u", "update_rate")})[:12], 16)
        updates, update_details = update_stream(config, initial, args.n_live,
                                                args.max_update_ops or 2 * args.n_live, trace_seed)
        update_details["update_stream_sha256"] = json_hash(updates)
        trace_parameters = json_hash({"n": args.n_live, "reserve": args.reserve,
                                     "query_vectors": args.query_vectors, "min_queries": args.min_queries,
                                     "max_queries": args.max_queries, "max_ops": args.max_update_ops})[:12]
        trace = work / "traces" / f"n{args.n_live}-s{args.seed}-{trace_parameters}" / f"{config['id']}.txt"
        query_details = write_trace(trace, config, initial, updates, args.query_vectors,
                                    args.max_queries, args.min_queries, trace_seed)
        generated.append(dict(config, **update_details, **query_details,
                              n_live=args.n_live, n_vectors=args.n_live + args.reserve,
                              dim=details["dim"], n_nodes=details["n_nodes"],
                              hierarchy_label=details["hierarchy_label"],
                              data_dir=str(directory), trace=str(trace),
                              trace_sha256=sha256_file(trace), data_sha256=details["data_sha256"],
                              data_file_sha256=details["file_sha256"], seed=args.seed))
    manifest["configs"] = generated
    manifest["generated"] = True
    write_json(manifest_path, manifest)
    print(json.dumps({"manifest": str(manifest_path), "configs": len(generated),
                      "planned_configs": len(configs), "n_live": args.n_live}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
