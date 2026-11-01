// Synthetic invariant test, not a SIFT performance experiment.
#define main merge_benchmark_main
#include "merge_arms.cpp"
#undef main

int main() try {
    Workload w;
    // Empty root 0 and empty internal concept 2; shared descendant 3.
    // 0 -> 1,2; 1 -> 3; 2 -> 3,4; 4 -> 5 (empty leaf).
    w.children = {{1, 2}, {3}, {3, 4}, {}, {5}, {}};
    w.topo = {0, 1, 2, 3, 4, 5};
    w.owner = {1, 1, 3, 3, 4, 4};
    w.members.resize(6); w.desc.resize(6);
    for (Id u = 0; u < w.owner.size(); ++u) w.members[w.owner[u]].push_back(u);
    w.desc = {{0, 1, 2, 3, 4, 5}, {1, 3}, {2, 3, 4, 5}, {3}, {4, 5}, {5}};
    Vectors base;
    base.n = 6; base.dim = 2;
    base.x = {0, 0, 1, 2, 6, 1, 5, 3, 20, 1, 21, 2};
    RawL2Space l2(2);
    Merged m = build(base, w, l2);
    Scratch scratch(base.n);
    for (Id c = 0; c < 6; ++c) {
        std::vector<char> allowed(6);
        for (Id d : w.desc[c]) allowed[d] = 1;
        for (Id q = 0; q < base.n; ++q) {
            size_t calls = 0;
            auto result = search(m, w, base, l2, base.at(q), allowed, {c}, base.n, scratch, calls);
            std::vector<Id> expected;
            for (Id u = 0; u < base.n; ++u) if (allowed[w.owner[u]]) expected.push_back(u);
            std::sort(result.begin(), result.end());
            require(result == expected, "empty-parent/shared-descendant scope lost reachable items");
            require(calls == expected.size(), "distance evaluation repeated or escaped scope");
        }
    }
    // Multi-root down-set: inherited overlap is visited and counted only once.
    std::vector<char> allowed{0, 1, 1, 1, 1, 1};
    size_t calls = 0;
    auto all = search(m, w, base, l2, base.at(0), allowed, {1, 2}, base.n, scratch, calls);
    require(all.size() == base.n && calls == base.n, "multi-root union duplicated or lost items");
    for (Id c = 0; c < w.members.size(); ++c) if (m.local[c]) {
        auto& graph = *m.local[c];
        for (size_t i = 0; i < graph.cur_element_count; ++i) {
            Id u = graph.getExternalLabel(i);
            auto* list = graph.get_linklist0(i);
            auto* ids = reinterpret_cast<hnswlib::tableint*>(list + 1);
            for (size_t j = 0; j < graph.getListCount(list); ++j) {
                Id v = graph.getExternalLabel(ids[j]);
                require(std::binary_search(m.edges[u].begin(), m.edges[u].end(), v), "merge deleted local edge");
            }
        }
    }
    std::cout << "PASS empty parents, shared descendants, empty scopes, scope isolation, deduplication, preserved edges\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
