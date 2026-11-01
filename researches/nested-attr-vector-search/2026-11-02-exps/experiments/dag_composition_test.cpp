// Invariants for the parameterised DAG composition builder.
// Not a performance experiment: small synthetic graphs only.
#define DAG_COMPOSITION_LIBRARY
#define main merge_benchmark_main
#include "dag_composition_bench.cpp"
#undef main

static Workload synthetic(size_t owners, size_t per_owner) {
    // Concept down-sets hold CONCEPT ids, exactly like merge_arms' loader:
    // 0 -> 1,2; 1 -> 3; 2 -> 3,4; 4 -> 5  (empty internal groups, shared descendant)
    Workload w;
    require(owners == 6, "synthetic DAG has six concepts");
    w.children = {{1, 2}, {3}, {3, 4}, {}, {5}, {}};
    w.topo = {0, 1, 2, 3, 4, 5};
    w.owner.resize(owners * per_owner);
    for (size_t o = 0; o < owners; ++o)
        for (size_t j = 0; j < per_owner; ++j) w.owner[o * per_owner + j] = Id(o);
    w.members.resize(owners);
    for (Id u = 0; u < w.owner.size(); ++u) w.members[w.owner[u]].push_back(u);
    w.desc.resize(owners);
    for (Id c = 0; c < owners; ++c) w.desc[c] = {c};
    for (auto it = w.topo.rbegin(); it != w.topo.rend(); ++it) {
        Id c = *it;
        std::vector<Id> merged = w.desc[c];
        for (Id child : w.children[c])
            merged.insert(merged.end(), w.desc[child].begin(), w.desc[child].end());
        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        w.desc[c] = std::move(merged);
    }
    return w;
}

int main() try {
    Workload w = synthetic(6, 24);
    Vectors base;
    base.n = w.owner.size(); base.dim = 8;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> pick(0.f, 100.f);
    base.x.resize(base.n * base.dim);
    for (auto& x : base.x) x = pick(rng);
    RawL2Space l2(base.dim);

    // 1. Baseline knobs must reproduce the committed builder edge for edge.
    Merged reference = build(base, w, l2);
    Merged variant = build_variant(base, w, l2, Knobs{.name = "baseline"});
    require(variant.portals == reference.portals, "baseline portals differ from committed build");
    require(variant.edges == reference.edges, "baseline adjacency differs from committed build");
    require(variant.local_edges == reference.local_edges &&
            variant.connector_edges == reference.connector_edges &&
            variant.jump_edges == reference.jump_edges, "baseline edge accounting differs");

    // 2. Portal sets must not depend on composition order (children-first phase).
    Merged asc = build_variant(base, w, l2, Knobs{.name = "order1", .order = 1});
    Merged desc = build_variant(base, w, l2, Knobs{.name = "order2", .order = 2});
    require(asc.portals == reference.portals && desc.portals == reference.portals,
            "composition order changed portal selection");

    // 3. Highway modes are nested supersets.
    Merged j0 = build_variant(base, w, l2, Knobs{.name = "jump0", .jump_mode = 0});
    Merged j2 = build_variant(base, w, l2, Knobs{.name = "jump2", .jump_mode = 2});
    Merged j3 = build_variant(base, w, l2, Knobs{.name = "jump3", .jump_mode = 3});
    auto subset = [](const Merged& a, const Merged& b) {
        for (Id u = 0; u < a.edges.size(); ++u)
            for (Id v : a.edges[u])
                if (!std::binary_search(b.edges[u].begin(), b.edges[u].end(), v)) return false;
        return true;
    };
    require(subset(j0, reference) && subset(reference, j2) && subset(j2, j3),
            "highway modes are not nested supersets");
    require(j0.jump_edges == 0 && reference.jump_edges > 0 && j2.jump_edges > reference.jump_edges,
            "highway edge accounting wrong");

    // 4. Pruning contracts degree, keeps sorted/unique adjacency, never self-loops.
    for (size_t M : {8u, 16u}) {
        Merged pruned = build_variant(base, w, l2, Knobs{.name = "prune", .prune_m = M});
        for (Id u = 0; u < pruned.edges.size(); ++u) {
            const auto& row = pruned.edges[u];
            require(row.size() <= M, "pruning exceeded degree cap");
            require(std::is_sorted(row.begin(), row.end()), "pruned row unsorted");
            require(std::adjacent_find(row.begin(), row.end()) == row.end(), "pruned row duplicated");
            require(std::find(row.begin(), row.end(), u) == row.end(), "pruned row kept self loop");
        }
        // Every item is still reachable from its owner portal through kept edges.
        Scratch scratch(base.n);
        for (Id c = 0; c < w.members.size(); ++c) {
            std::vector<char> seen(base.n, 0);
            std::vector<Id> stack(pruned.portals[c].begin(), pruned.portals[c].end());
            for (Id u : stack) seen[u] = 1;
            while (!stack.empty()) {
                Id u = stack.back(); stack.pop_back();
                for (Id v : pruned.edges[u]) if (!seen[v]) { seen[v] = 1; stack.push_back(v); }
            }
            for (Id u = 0; u < base.n; ++u)
                if (std::binary_search(w.desc[c].begin(), w.desc[c].end(), w.owner[u]))
                    require(seen[u], "pruning disconnected an in-scope item");
        }
    }

    // 5. Counted search agrees with the committed search and never scores unqualified items.
    std::vector<char> allowed(w.members.size());
    for (Id d : w.desc[2]) allowed[d] = 1;
    std::vector<Id> roots{2};
    Scratch scratch(base.n);
    for (Id q = 0; q < 10; ++q) {
        size_t calls = 0;
        auto expected = search(reference, w, base, l2, base.at(q), allowed, roots, base.n, scratch, calls);
        SearchStats stats;
        auto counted = search_counted(reference, base, l2, base.at(q), roots, base.n, scratch,
                                      [&](Id u) { return bool(allowed[w.owner[u]]); }, stats);
        require(expected == counted, "counted search diverged from committed search");
        require(stats.distances == calls, "counted distance total diverged");
        for (Id u : counted) require(allowed[w.owner[u]], "counted search returned unqualified item");
    }

    // 6. First-come reverse admission is order-sensitive but keeps the invariants.
    Merged f_dag = build_variant(base, w, l2, Knobs{.name = "fcfs", .reverse_mode = 1});
    Merged f_asc = build_variant(base, w, l2, Knobs{.name = "fcfs1", .order = 1, .reverse_mode = 1});
    Merged f_desc = build_variant(base, w, l2, Knobs{.name = "fcfs2", .order = 2, .reverse_mode = 1});
    require(f_dag.portals == reference.portals && f_asc.portals == reference.portals,
            "first-come admission changed portal selection");
    require(f_asc.edges != f_dag.edges || f_desc.edges != f_dag.edges,
            "first-come admission is order-insensitive: composition order unobservable");
    for (const Merged* m : {&f_dag, &f_asc, &f_desc}) {
        size_t edges = 0;
        for (const auto& row : m->edges) {
            require(std::is_sorted(row.begin(), row.end()), "fcfs row unsorted");
            require(std::adjacent_find(row.begin(), row.end()) == row.end(), "fcfs row duplicated");
            edges += row.size();
        }
        require(edges > 0, "fcfs graph empty");
    }

    // 7. Dense visited set yields exactly the epoch-stamped results.
    DenseScratch dense(base.n);
    for (Id q = 0; q < 10; ++q) {
        SearchStats dense_stats, epoch_stats;
        VecEdges edges{&reference.edges, &w};
        auto dense_result = search_dense(edges, reference, base, l2, base.at(q), roots, base.n, dense,
                                         [&](Id, Id owner) { return bool(allowed[owner]); }, dense_stats);
        auto epoch_result = search_counted(reference, base, l2, base.at(q), roots, base.n, scratch,
                                           [&](Id u) { return bool(allowed[w.owner[u]]); }, epoch_stats);
        require(dense_result == epoch_result, "dense visited set changed the result");
        require(dense_stats.distances == epoch_stats.distances, "dense visited changed distance count");
        require(dense_stats.duplicate_edges == epoch_stats.duplicate_edges,
                "dense visited changed duplicate-edge count");
    }

    // 8. Qualification kernels agree on every owner and item.
    CharKernel bytes{allowed};
    OwnerBitmapKernel owners;
    owners.words.assign((allowed.size() + 63) / 64, 0);
    for (Id d = 0; d < allowed.size(); ++d)
        if (allowed[d]) owners.words[d >> 6] |= UINT64_C(1) << (d & 63);
    ItemBitmapKernel items;
    items.words.assign((base.n + 63) / 64, 0);
    for (Id u = 0; u < base.n; ++u)
        if (bytes(w.owner[u])) items.words[u >> 6] |= UINT64_C(1) << (u & 63);
    for (Id d = 0; d < allowed.size(); ++d) require(bytes(d) == owners(d), "owner bitmap mismatch");
    for (Id u = 0; u < base.n; ++u) require(bytes(w.owner[u]) == items(u), "item bitmap mismatch");

    std::cout << "PASS baseline equivalence, order-invariant portals, nested highway modes, "
                 "degree cap/connectivity, counted search, qualification kernels\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
