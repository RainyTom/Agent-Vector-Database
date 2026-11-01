#define QUERY_COST_PROFILE_TEST
#include "query_cost_profile.cpp"

int main() try {
    Workload w;
    w.children = {{1, 2}, {}, {}}; w.topo = {0, 1, 2};
    w.desc = {{0, 1, 2}, {1}, {2}}; w.scopes = {{1}};
    w.owner = {2, 1, 2, 1}; w.members = {{}, {1, 3}, {0, 2}};
    Vectors base, query;
    base.n = 4; base.dim = 2; base.x = {0, 0, 10, 0, 5, 0, 12, 0};
    query.n = 1; query.dim = 2; query.x = {5, 0};
    RawL2Space raw(2);
    Graph global(&raw, 4, 2, 20, 100);
    const int levels[]{3, 2, 2, 1};
    for (Id u = 0; u < 4; ++u)
        require(global.addPointWithLevel(base.at(u), u, levels[u]).ok(), "forced-level insertion failed");
    require(w.owner[global.getExternalLabel(global.enterpoint_node_)] == 2, "fixture global entry must be unqualified");
    QualifiedOwnerEntries entries(global, w);
    cost::Probe probe(raw, base, w);
    Scratch scratch(base.n);
    std::vector<char> allowed{0, 1, 0};
    auto run = [&]() {
        probe.begin(cost::Mode::Count, allowed);
        cost::NativeScope hook(global, probe);
        return search_global_prefilter(global, entries, query.at(0), 10, 32, scratch,
            [&](Id u) { return probe.item_allowed(u); }, [&](Id c) { return probe.owner_allowed(c); });
    };
    auto result = run();
    std::sort(result.begin(), result.end());
    require(result == std::vector<Id>({1, 3}), "qualified upper/base navigation lost connected results");
    require(probe.stats.rejected_distances == 0 && probe.stats.item_rejects > 0,
            "unqualified global entry/upper/base neighbor incurred distance");
    allowed.assign(3, 0);
    require(run().empty() && probe.stats.distances == 0, "empty scope performed distance or returned result");
    allowed.assign(3, 1);
    result = run(); std::sort(result.begin(), result.end());
    require(result == std::vector<Id>({0, 1, 2, 3}), "all-qualified search lost connected graph nodes");
    // Remove every qualified-only route from item1 to item3. Both still connect
    // through rejected item2; strict filtering must NOT sneak across that bridge.
    allowed = {0, 1, 0};
    for (Id u : {Id(1), Id(3)}) {
        auto internal = global.label_lookup_.at(u);
        auto* list = global.get_linklist0(internal);
        global.setListCount(list, 1);
        reinterpret_cast<hnswlib::tableint*>(list + 1)[0] = global.label_lookup_.at(2);
        for (int level = 1; level <= global.element_levels_[internal]; ++level)
            global.setListCount(global.get_linklist(internal, level), 0);
    }
    result = run();
    require(result == std::vector<Id>{1}, "prefilter crossed unqualified bridge or used hidden scan fallback");
    require(probe.stats.unique_distances == 1 && probe.stats.rejected_distances == 0,
            "disconnected qualified component performed forbidden distance");
    std::cout << "PASS qualified entry selection, upper/base prechecks, empty/all-qualified scopes, disconnected-component semantics\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
