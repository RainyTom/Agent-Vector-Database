// Behavioral validation against exact geometry; uses the DAG method's own fork.
#define DAG_VALIDATION_FAMILY 0
#define DAG_COMPOSITION_LIBRARY
#include "dag_composition_bench.cpp"

static Workload fixture() {
    Workload w;
    w.children = {{1, 2}, {3}, {3, 4}, {}, {5}, {}, {}};
    w.topo = {0, 1, 2, 3, 4, 5, 6}; // attribute 6 has no items, even recursively
    w.members.resize(7); w.desc.resize(7);
    for (Id owner = 0; owner < 6; ++owner) for (Id j = 0; j < 24; ++j) {
        Id item = Id(w.owner.size()); w.owner.push_back(owner); w.members[owner].push_back(item);
    }
    for (auto it = w.topo.rbegin(); it != w.topo.rend(); ++it) {
        auto& row = w.desc[*it]; row.push_back(*it);
        for (Id child : w.children[*it]) row.insert(row.end(), w.desc[child].begin(), w.desc[child].end());
        std::sort(row.begin(), row.end()); row.erase(std::unique(row.begin(), row.end()), row.end());
    }
    return w;
}
static std::vector<Id> exact(const Workload& w, const Vectors& base, RawL2Space& raw,
                             const float* vector, const Query& scope) {
    std::vector<Pair> scored;
    for (Id u = 0; u < base.n; ++u) if (scope.allowed[w.owner[u]])
        scored.emplace_back(raw.dist(vector, base.at(u)), u);
    std::sort(scored.begin(), scored.end());
    if (scored.size() > 10) scored.resize(10);
    std::vector<Id> ids;
    for (auto it = scored.rbegin(); it != scored.rend(); ++it) ids.push_back(it->second);
    return ids;
}
int main() try {
    auto w = fixture();
    Vectors base; base.n = w.owner.size(); base.dim = 8; base.x.resize(base.n * base.dim);
    std::mt19937 rng(7); std::uniform_real_distribution<float> values(0, 100);
    for (float& x : base.x) x = values(rng);
    RawL2Space raw(base.dim);
    auto graph = build_variant(base, w, raw, Knobs{});
    const auto direct = resolve_scope(w, {1}, false), recursive = resolve_scope(w, {1}, true);
    require(direct.allowed[1] && !direct.allowed[3] && recursive.allowed[3], "direct/recursive predicate conflated");
    require(resolve_scope(w, {1, 3}, true).roots == std::vector<Id>{1}, "recursive ancestor not absorbed");
    require(resolve_scope(w, {1, 3}, false).roots == std::vector<Id>({1, 3}), "direct descendant incorrectly absorbed");
    // Force a subtree portal to an ineligible descendant: direct searches MUST
    // start from own representatives instead, not become an empty search.
    graph.portals[1] = {w.members[3].front()};
    Scratch scratch(base.n); DenseScratch dense(base.n); auto csr = flatten(graph, w);
    for (bool descend : {false, true}) for (Id attr = 0; attr < 7; ++attr) {
        auto q = resolve_scope(w, {attr}, descend);
        const auto& entries = query_entries(graph, descend);
        auto predicate = [&](Id u) { return bool(q.allowed[w.owner[u]]); };
        for (Id vector = 0; vector < 5; ++vector) {
            SearchStats counted, unused, flat, bits;
            auto result = search_counted(graph, base, raw, base.at(vector), q.roots, base.n,
                                         scratch, predicate, counted, &entries);
            auto normal = search_counted<false>(graph, base, raw, base.at(vector), q.roots, base.n,
                                                scratch, predicate, unused, &entries);
            auto contiguous = search_csr<true>(csr, graph, w, base, raw, base.at(vector), q.roots, base.n,
                scratch, [&](Id, Id owner) { return bool(q.allowed[owner]); }, flat, &entries);
            VecEdges edges{&graph.edges, &w};
            auto bitset = search_dense(edges, graph, base, raw, base.at(vector), q.roots, base.n,
                dense, [&](Id, Id owner) { return bool(q.allowed[owner]); }, bits, &entries);
            require(result == exact(w, base, raw, base.at(vector), q), "unpruned search disagrees with exact GT");
            require(result == normal && result == contiguous && result == bitset, "execution representation changed IDs");
            require(counted.distances == flat.distances && counted.distances == bits.distances &&
                    counted.adjacency_entries == bits.adjacency_entries, "execution representation changed work");
            if (attr == 6) require(result.empty() && counted.distances == 0, "empty scope performed distances");
        }
    }
    // Degree pruning is an approximation, NOT an induced-connectivity guarantee.
    for (size_t cap : {16u, 24u}) {
        auto pruned = build_variant(base, w, raw, Knobs{.name = "prune", .prune_m = cap});
        for (Id u = 0; u < base.n; ++u) {
            const auto& row = pruned.edges[u];
            require(row.size() <= cap && std::is_sorted(row.begin(), row.end()) &&
                std::adjacent_find(row.begin(), row.end()) == row.end() &&
                !std::binary_search(row.begin(), row.end(), u), "invalid degree-pruned adjacency");
        }
    }
    // A full-graph bridge through an ineligible vertex is not an induced path.
    Merged bridge; bridge.edges.resize(base.n); bridge.portals.resize(w.members.size());
    Id a = w.members[1][0], rejected = w.members[3][0], b = w.members[1][1];
    bridge.edges[a] = {rejected}; bridge.edges[rejected] = {b}; bridge.portals[1] = {a};
    SearchStats isolated;
    auto partial = search_counted(bridge, base, raw, base.at(a), {1}, base.n, scratch,
        [&](Id u) { return bool(direct.allowed[w.owner[u]]); }, isolated);
    require(partial == std::vector<Id>{a} && isolated.distances == 1 && isolated.rejected_edges == 1,
            "search secretly crossed an unqualified bridge");
    // Native hook uses the same squared-L2 kernel and never escapes its selected attribute.
    auto mask = direct.allowed;
    auto& local = *graph.local[1]; local.setEf(base.n);
    auto original_fn = local.fstdistfunc_; auto original_parameter = local.dist_func_param_;
    {
        NativeCounter counter(local, w, mask);
        auto result = labels(local.searchKnn(base.at(0), 10));
        require(result == exact(w, base, raw, base.at(0), direct) && counter.rejected == 0,
                "native local search or attribution differs");
    }
    require(local.fstdistfunc_ == original_fn && local.dist_func_param_ == original_parameter,
            "native distance hook was not restored");
    std::cout << "PASS direct/recursive GT, qualified entries, shared descendants, empty scope, "
                 "normal/count/CSR/visited equivalence, pruning bounds, induced-path boundary, native hook\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
}
