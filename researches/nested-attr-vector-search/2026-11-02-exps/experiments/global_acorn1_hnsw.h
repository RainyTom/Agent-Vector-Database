#pragma once

// ACORN arXiv:2403.04871v1 Algorithm2 / section5.3 SEARCH STRATEGY.
// Backend deviation: original diversified hnswlib graph is retained, not the
// author's unpruned FAISS construction. Entries remain qualified-owner seeds.
// Lookup order is native direct-list order, then each direct node's list order;
// locally deduplicate, filter, and retain first M (paper cap, including level0).
// No scan fallback or arbitrary-depth graph traversal is hidden in this kernel.
struct NoopAcornObserver {
    void on_adjacency(size_t) const noexcept {}
    void on_bridge() const noexcept {}
    void on_candidate() const noexcept {}
};
struct AcornScratch {
    using Internal = hnswlib::tableint;
    std::vector<uint32_t> visited, neighborhood_seen;
    uint32_t search_epoch = 0, lookup_epoch = 0;
    std::vector<Internal> neighborhood, seeds;
    explicit AcornScratch(size_t n) : visited(n), neighborhood_seen(n) {}
    void next_search() {
        if (++search_epoch == 0) { std::fill(visited.begin(), visited.end(), 0); search_epoch = 1; }
    }
    void next_lookup() {
        if (++lookup_epoch == 0) {
            std::fill(neighborhood_seen.begin(), neighborhood_seen.end(), 0); lookup_epoch = 1;
        }
        neighborhood.clear();
    }
};

template<class ItemQualification, class Observer>
static void acorn1_neighbors(Graph& graph, hnswlib::tableint current, int level,
    AcornScratch& scratch, ItemQualification& qualifies, Observer& observer) {
    using Internal = hnswlib::tableint;
    scratch.next_lookup();
    // A zero-hop self is not a neighbor; cycles still count as adjacency work.
    scratch.neighborhood_seen[current] = scratch.lookup_epoch;
    auto append = [&](Internal id) {
        if (scratch.neighborhood_seen[id] == scratch.lookup_epoch) return;
        scratch.neighborhood_seen[id] = scratch.lookup_epoch;
        scratch.neighborhood.push_back(id);
    };
    auto list = [&](Internal id) {
        return level == 0 ? graph.get_linklist0(id) : graph.get_linklist(id, level);
    };
    auto* direct = list(current);
    const auto* ids = reinterpret_cast<const Internal*>(direct + 1);
    size_t direct_count = graph.getListCount(direct);
    observer.on_adjacency(direct_count);
    for (size_t i = 0; i < direct_count; ++i) append(ids[i]);
    // Expand ALL intermediates, qualified or not, before predicate evaluation.
    for (size_t i = 0; i < direct_count; ++i) {
        observer.on_bridge();
        auto* second = list(ids[i]);
        const auto* next = reinterpret_cast<const Internal*>(second + 1);
        size_t count = graph.getListCount(second);
        observer.on_adjacency(count);
        for (size_t j = 0; j < count; ++j) append(next[j]);
    }
    size_t output = 0;
    const size_t expanded_count = scratch.neighborhood.size();
    for (size_t i = 0; i < expanded_count; ++i) {
        Internal id = scratch.neighborhood[i];
        if (!qualifies(Id(graph.getExternalLabel(id)))) continue;
        observer.on_candidate(); // unique qualified population BEFORE M truncation
        if (output < graph.M_) scratch.neighborhood[output++] = id;
    }
    scratch.neighborhood.resize(output);
}

template<class ItemQualification, class OwnerQualification, class Observer>
static std::vector<Id> search_global_acorn1(Graph& graph, const QualifiedOwnerEntries& entries,
    const float* query, size_t k, size_t ef, AcornScratch& scratch,
    ItemQualification qualifies, OwnerQualification owner_qualifies, Observer& observer) {
    using Internal = hnswlib::tableint;
    using Candidate = std::pair<float, Internal>;
    const size_t n = graph.cur_element_count;
    scratch.neighborhood.reserve(std::min(n, graph.maxM0_ * (graph.maxM0_ + 1)));
    scratch.seeds.clear(); scratch.seeds.reserve(entries.by_owner.size());
    auto distance = [&](Internal id) {
        return graph.fstdistfunc_(query, graph.getDataByInternalId(id), graph.dist_func_param_);
    };
    for (Id owner = 0; owner < entries.by_owner.size(); ++owner) {
        if (!owner_qualifies(owner)) continue;
        Internal current = entries.by_owner[owner];
        if (current == QualifiedOwnerEntries::missing || !qualifies(Id(graph.getExternalLabel(current)))) continue;
        float current_distance = distance(current);
        // ef=1 Algorithm2 reduces to greedy best-improving navigation. A level's
        // visited set persists across successive neighborhood expansions.
        for (int level = graph.element_levels_[current]; level > 0; --level) {
            scratch.next_search(); scratch.visited[current] = scratch.search_epoch;
            while (true) {
                acorn1_neighbors(graph, current, level, scratch, qualifies, observer);
                Internal nearest = current;
                for (Internal id : scratch.neighborhood) {
                    if (scratch.visited[id] == scratch.search_epoch) continue;
                    scratch.visited[id] = scratch.search_epoch;
                    float d = distance(id);
                    if (d < current_distance) { current_distance = d; nearest = id; }
                }
                if (nearest == current) break;
                current = nearest;
            }
        }
        scratch.seeds.push_back(current);
    }
    scratch.next_search();
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> frontier;
    std::priority_queue<Candidate> best;
    ef = std::max(ef, k);
    auto offer_qualified = [&](Internal id) {
        if (scratch.visited[id] == scratch.search_epoch) return;
        scratch.visited[id] = scratch.search_epoch;
        float d = distance(id);
        if (best.size() < ef || d < best.top().first) {
            frontier.emplace(d, id); best.emplace(d, id);
            if (best.size() > ef) best.pop();
        }
    };
    // Seeds were checked before their high-layer scores; no rejected seed is
    // scored even when the original global entry fails the query predicate.
    for (Internal seed : scratch.seeds) offer_qualified(seed);
    while (!frontier.empty()) {
        Candidate current = frontier.top();
        if (best.size() == ef && current.first > best.top().first) break;
        frontier.pop();
        acorn1_neighbors(graph, current.second, 0, scratch, qualifies, observer);
        for (Internal id : scratch.neighborhood) offer_qualified(id);
    }
    while (best.size() > k) best.pop();
    std::vector<Id> result;
    while (!best.empty()) { result.push_back(Id(graph.getExternalLabel(best.top().second))); best.pop(); }
    return result;
}
