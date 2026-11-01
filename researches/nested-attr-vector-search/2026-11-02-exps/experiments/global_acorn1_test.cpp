// Synthetic behavioral fixtures only; no SIFT performance claims.
#define QUERY_COST_PROFILE_TEST
#include "query_cost_profile.cpp"

static void links(Graph& graph, Id label, int level, std::initializer_list<Id> neighbors) {
    auto u = graph.label_lookup_.at(label);
    auto* list = level ? graph.get_linklist(u, level) : graph.get_linklist0(u);
    require(neighbors.size() <= (level ? graph.maxM_ : graph.maxM0_), "fixture degree overflow");
    graph.setListCount(list, neighbors.size());
    auto* ids = reinterpret_cast<hnswlib::tableint*>(list + 1);
    size_t i = 0;
    for (Id target : neighbors) {
        auto v = graph.label_lookup_.at(target);
        require(graph.element_levels_[v] >= level, "fixture edge at invalid layer");
        ids[i++] = v;
    }
}
static void clear_links(Graph& graph) {
    for (Id u = 0; u < graph.cur_element_count; ++u) {
        auto* lower = graph.get_linklist0(u); graph.setListCount(lower, 0);
        for (int level = 1; level <= graph.element_levels_[u]; ++level)
            graph.setListCount(graph.get_linklist(u, level), 0);
    }
}
int main() try {
    Workload w;
    w.children = {{1, 2}, {}, {}}; w.topo = {0, 1, 2};
    w.desc = {{0, 1, 2}, {1}, {2}}; w.scopes = {{1}};
    w.owner = {2, 1, 2, 1, 1, 2}; w.members = {{}, {1, 3, 4}, {0, 2, 5}};
    Vectors base, query;
    base.n = 6; base.dim = 2; base.x = {50, 50, 100, 0, .1f, 0, 10, 0, 0, 0, .2f, 0};
    query.n = 1; query.dim = 2; query.x = {0, 0};
    RawL2Space raw(2);
    Graph graph(&raw, 6, 2, 20, 100);
    const int levels[]{3, 2, 2, 2, 1, 1};
    for (Id u = 0; u < base.n; ++u)
        require(graph.addPointWithLevel(base.at(u), u, levels[u]).ok(), "fixture insertion failed");
    require(w.owner[graph.getExternalLabel(graph.enterpoint_node_)] == 2, "original entry must fail predicate");
    QualifiedOwnerEntries entries(graph, w);
    AcornScratch scratch(base.n);
    Scratch strict_scratch(base.n);
    cost::Probe probe(raw, base, w);
    std::vector<char> allowed{0, 1, 0};
    auto run = [&]() {
        probe.begin(cost::Mode::Trace, allowed);
        cost::NativeScope hook(graph, probe);
        return search_global_acorn1(graph, entries, query.at(0), 1, 1, scratch,
            [&](Id u) { return probe.item_allowed(u); }, [&](Id c) { return probe.owner_allowed(c); }, probe);
    };
    auto strict = [&]() {
        return search_global_prefilter(graph, entries, query.at(0), 1, 1, strict_scratch,
            [&](Id u) { return bool(allowed[w.owner[u]]); }, [&](Id c) { return bool(allowed[c]); });
    };
    clear_links(graph);
    links(graph, 1, 2, {2}); links(graph, 2, 2, {3});
    links(graph, 1, 0, {2}); links(graph, 2, 0, {3});
    links(graph, 3, 0, {5}); links(graph, 5, 0, {4});
    // Upper-level two-hop lookup recovers qualified3 through rejected2.
    probe.begin(cost::Mode::Count, allowed);
    auto qualifies = [&](Id u) { return probe.item_allowed(u); };
    acorn1_neighbors(graph, graph.label_lookup_.at(1), 2, scratch, qualifies, probe);
    require(scratch.neighborhood == std::vector<hnswlib::tableint>{graph.label_lookup_.at(3)},
            "upper-layer two-hop bridge not expanded");
    require(run() == std::vector<Id>{4}, "qualified nearest behind rejected bridges not recovered");
    require(probe.stats.rejected_distances == 0, "rejected entry or bridge was scored");
    require(strict() == std::vector<Id>{1}, "strict fixture unexpectedly has qualified-only path");

    // A qualified but geometrically worse intermediate must also be expanded.
    clear_links(graph);
    base.x[2] = 1; base.x[6] = 100;
    reinterpret_cast<float*>(graph.getDataByInternalId(graph.label_lookup_.at(1)))[0] = 1;
    reinterpret_cast<float*>(graph.getDataByInternalId(graph.label_lookup_.at(3)))[0] = 100;
    links(graph, 1, 0, {3}); links(graph, 3, 0, {4});
    require(strict() == std::vector<Id>{1}, "qualified-intermediate fixture does not exercise ef1 barrier");
    require(run() == std::vector<Id>{4}, "ACORN expanded only rejected intermediates");

    // Local dedup must precede predicate/cap, independently of query visited.
    clear_links(graph);
    links(graph, 1, 0, {2, 5}); links(graph, 2, 0, {1, 3}); links(graph, 5, 0, {3, 4});
    std::array<size_t, 6> checks{};
    probe.begin(cost::Mode::Count, allowed);
    auto counted_predicate = [&](Id u) { ++checks[u]; return probe.item_allowed(u); };
    acorn1_neighbors(graph, graph.label_lookup_.at(1), 0, scratch, counted_predicate, probe);
    require(scratch.neighborhood == std::vector<hnswlib::tableint>({graph.label_lookup_.at(3), graph.label_lookup_.at(4)}),
            "duplicate or cycle consumed qualified neighborhood cap");
    require(checks[3] == 1 && checks[4] == 1 && probe.stats.distances == 0,
            "lookup repeated predicate or scored before admission");
    require(probe.stats.adjacency_entries == 6 && probe.stats.bridge_expansions == 2 &&
            probe.stats.qualified_candidates == 2, "adjacency/bridge/candidate accounting incorrect");
    allowed.assign(3, 1); checks.fill(0); probe.begin(cost::Mode::Count, allowed);
    acorn1_neighbors(graph, graph.label_lookup_.at(1), 0, scratch, counted_predicate, probe);
    require(scratch.neighborhood == std::vector<hnswlib::tableint>({graph.label_lookup_.at(2), graph.label_lookup_.at(5)}) &&
            probe.stats.qualified_candidates == 4, "filter-before-first-M truncation changed");

    // A three-edge rejected-only chain is outside the documented two-hop lookup.
    allowed = {0, 1, 0}; clear_links(graph);
    links(graph, 1, 0, {2}); links(graph, 2, 0, {5}); links(graph, 5, 0, {4});
    require(run() == std::vector<Id>{1}, "arbitrary-depth or hidden scan fallback was introduced");
    allowed.assign(3, 0);
    require(run().empty() && probe.stats.distances == 0, "empty qualification performed a distance");
    std::cout << "PASS upper/base bridges, qualified intermediates, stable dedup/M cap, zero rejected distances, bounded hops\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
