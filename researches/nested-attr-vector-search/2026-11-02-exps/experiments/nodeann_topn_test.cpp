// Concentrated global top-k must not be mistaken for n candidates per graph.
#define DAG_VALIDATION_FAMILY 1
#define DAG_COMPOSITION_LIBRARY
#include "dag_composition_bench.cpp"

int main() try {
    Workload w;
    w.members.resize(3);
    for (Id item = 0; item < 16; ++item) {
        const Id attr = item < 12 ? 0 : 1;
        w.owner.push_back(attr); w.members[attr].push_back(item);
    }
    Vectors base;
    base.n = 16; base.dim = 8; base.x.assign(base.n * base.dim, 0);
    for (Id item = 0; item < base.n; ++item)
        base.x[item * base.dim] = item < 12 ? float(item) : 100.0f + item;
    RawL2Space raw(base.dim);
    auto index = build_variant(base, w, raw, Knobs{});
    Query scope; scope.allowed = {1, 1, 0}; scope.count = 16;
    for (Id item = 10; item > 0; --item) scope.truth.push_back(item - 1);
    const auto* query = base.at(0);
    SearchStats small_work, full_work, unused;
    const auto small = search_nodeann_topn<true>(index, w, scope.allowed, query, 10, 1, 32, small_work);
    const auto full = search_nodeann_topn<true>(index, w, scope.allowed, query, 10, 10, 32, full_work);
    require(small == std::vector<Id>({12, 0}), "local n became a global output limit");
    require(checked_hits(10, small, scope, w, base, raw, query) == 1, "far local winner counted as global hit");
    require(full == scope.truth, "global top-k concentrated in one attribute was lost");
    require(full == search_nodeann_topn<false>(index, w, scope.allowed, query, 10, 10, 32, unused),
            "normal/count local quota results differ");
    // Larger global k on an underfull scope must return all 16, not ten.
    auto underfull = scope;
    underfull.truth.clear();
    for (Id item = 16; item > 0; --item) underfull.truth.push_back(item - 1);
    for (size_t k : {50u, 100u}) {
        SearchStats work;
        const auto result = search_nodeann_topn<true>(index, w, scope.allowed, query, k, k, k, work);
        require(result == underfull.truth && checked_hits(k, result, underfull, w, base, raw, query) == 16,
                "large k underfull scope uses a ten-result cap or wrong denominator");
    }
    scope.allowed = {1, 0, 0}; scope.count = 12;
    const auto direct = search_nodeann_topn<true>(index, w, scope.allowed, query, 10, 1, 32, unused);
    require(direct == std::vector<Id>{0}, "quota search entered an unselected attribute");
    require(small_work.rejected_distances == 0 && full_work.rejected_distances == 0 &&
            unused.rejected_distances == 0, "quota search computed unqualified distance");
    scope.allowed = {0, 0, 1};
    SearchStats empty_work;
    require(search_nodeann_topn<true>(index, w, scope.allowed, query, 10, 10, 32, empty_work).empty()
            && empty_work.distances == 0, "empty attribute was searched");
    std::cout << "PASS local n vs global k, concentrated top-k, qualification, empty graph, normal/count equivalence\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
}
