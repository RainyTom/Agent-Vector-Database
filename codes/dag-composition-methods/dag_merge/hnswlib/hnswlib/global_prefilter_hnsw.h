#pragma once

// Search the original global HNSW adjacency/levels with strict qualification
// before every query-vector distance. No unqualified bridge traversal or scan fallback.
struct QualifiedOwnerEntries {
    std::vector<hnswlib::tableint> by_owner;
    static constexpr auto missing = std::numeric_limits<hnswlib::tableint>::max();
    QualifiedOwnerEntries(const Graph& graph, const Workload& w) : by_owner(w.members.size(), missing) {
        for (hnswlib::tableint i = 0; i < graph.cur_element_count; ++i) {
            Id label = Id(graph.getExternalLabel(i));
            auto& current = by_owner[w.owner[label]];
            if (current == missing || graph.element_levels_[i] > graph.element_levels_[current] ||
                (graph.element_levels_[i] == graph.element_levels_[current] && label < graph.getExternalLabel(current)))
                current = i;
        }
    }
};

template<class ItemQualification, class OwnerQualification>
static std::vector<Id> search_global_prefilter(Graph& graph, const QualifiedOwnerEntries& entries,
    const float* query, size_t k, size_t ef, Scratch& visited,
    ItemQualification qualifies, OwnerQualification owner_qualifies) {
    using Internal = hnswlib::tableint;
    using Candidate = std::pair<float, Internal>;
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> frontier;
    std::priority_queue<Candidate> best;
    ef = std::max(ef, k);
    ++visited.epoch;
    auto distance = [&](Internal id) {
        return graph.fstdistfunc_(query, graph.getDataByInternalId(id), graph.dist_func_param_);
    };
    auto offer = [&](Internal id) {
        if (visited.seen[id] == visited.epoch) return;
        visited.seen[id] = visited.epoch;
        if (!qualifies(Id(graph.getExternalLabel(id)))) return;
        float d = distance(id);
        if (best.size() < ef || d < best.top().first) {
            frontier.emplace(d, id); best.emplace(d, id);
            if (best.size() > ef) best.pop();
        }
    };
    // One static highest-level representative per qualified owner. Multiple
    // qualified seeds share ONE level-0 frontier/result heap, not per-owner top-k.
    // This avoids an unqualified global entry and is not a vector/GT scan.
    for (Id owner = 0; owner < entries.by_owner.size(); ++owner) {
        if (!owner_qualifies(owner)) continue;
        Internal current = entries.by_owner[owner];
        if (current == QualifiedOwnerEntries::missing) continue;
        if (!qualifies(Id(graph.getExternalLabel(current)))) continue;
        float current_distance = distance(current);
        for (int level = graph.element_levels_[current]; level > 0; --level) {
            bool changed = true;
            while (changed) {
                changed = false;
                auto* links = graph.get_linklist(current, level);
                auto* ids = reinterpret_cast<const Internal*>(links + 1);
                size_t count = graph.getListCount(links);
                for (size_t i = 0; i < count; ++i) {
                    Internal next = ids[i];
                    if (!qualifies(Id(graph.getExternalLabel(next)))) continue;
                    float d = distance(next);
                    if (d < current_distance) {
                        current_distance = d; current = next; changed = true;
                    }
                }
            }
        }
        offer(current);
    }
    while (!frontier.empty()) {
        auto current = frontier.top();
        if (best.size() == ef && current.first > best.top().first) break;
        frontier.pop();
        auto* links = graph.get_linklist0(current.second);
        auto* ids = reinterpret_cast<const Internal*>(links + 1);
        size_t count = graph.getListCount(links);
        for (size_t i = 0; i < count; ++i) offer(ids[i]);
    }
    while (best.size() > k) best.pop();
    std::vector<Id> result;
    while (!best.empty()) { result.push_back(Id(graph.getExternalLabel(best.top().second))); best.pop(); }
    return result;
}
