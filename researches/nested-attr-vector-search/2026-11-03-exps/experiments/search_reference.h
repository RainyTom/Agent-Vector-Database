#pragma once
// Reproduction snapshot: DAG search at commit 282eaaa. Not a serving path.
// MemoRejected=true isolates per-query caching of a rejected predicate result.
template<bool Count, bool MemoRejected = false, class Qualification>
static std::vector<Id> search_reference(size_t k, const Merged& m, const Vectors& base, RawL2Space& l2,
    const float* query, const std::vector<Id>& roots, size_t ef, Scratch& scratch,
    Qualification qualifies, SearchStats& stats, const std::vector<std::vector<Id>>* entries) {
    ef = std::max(ef, k);
    ++scratch.epoch;
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> frontier;
    std::priority_queue<Pair> best;
    auto offer = [&](Id u) {
        if (scratch.seen[u] == scratch.epoch) {
            if constexpr (Count) ++stats.duplicate_edges;
            return;
        }
        if constexpr (Count) {
            ++stats.item_checks;
            if (stats.trace_enabled) stats.trace.push_back(u);
        }
        if constexpr (MemoRejected) scratch.seen[u] = scratch.epoch;
        if (!qualifies(u)) { if constexpr (Count) ++stats.rejected_edges; return; }
        scratch.seen[u] = scratch.epoch;
        Pair p{l2.dist(query, base.at(u)), u};
        if constexpr (Count) {
            ++stats.distances;
            stats.rejected_distances += !qualifies(u);
        }
        if (best.size() < ef || p < best.top()) {
            frontier.push(p); best.push(p);
            if (best.size() > ef) best.pop();
        }
    };
    for (Id c : roots) for (Id u : (*entries)[c]) offer(u);
    while (!frontier.empty()) {
        Pair p = frontier.top(); frontier.pop();
        if (best.size() == ef && p > best.top()) break;
        if constexpr (Count) {
            ++stats.expansions;
            stats.adjacency_entries += m.edges[p.second].size();
        }
        for (Id v : m.edges[p.second]) offer(v);
    }
    while (best.size() > k) best.pop();
    std::vector<Id> result;
    while (!best.empty()) { result.push_back(best.top().second); best.pop(); }
    return result;
}
