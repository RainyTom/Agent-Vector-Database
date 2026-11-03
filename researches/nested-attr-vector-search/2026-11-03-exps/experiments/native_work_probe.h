#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <queue>
#include <vector>

// Include after dag_composition_fixture.h, with the NodeANN hnswlib selected.
// Diagnostic only: the distance hook and collect_metrics=true must never be
// installed in a timed serving pass. Requires exclusive access to this graph.
struct NativeWork {
    size_t upper_expansions = 0, lower_expansions = 0;
    size_t upper_adjacency = 0, lower_adjacency = 0;
    size_t distances = 0, distinct = 0, rejected_distances = 0;
};

namespace native_work_probe_detail {
struct DistanceHook {
    Graph& graph;
    const Workload& workload;
    const std::vector<char>& allowed;
    NativeWork& work;
    hnswlib::DISTFUNC<float> saved;
    void* saved_param;
    const char* origin;
    ptrdiff_t stride;
    std::vector<unsigned char> seen;

    DistanceHook(Graph& g, const Workload& w, const std::vector<char>& mask, NativeWork& out)
        : graph(g), workload(w), allowed(mask), work(out), saved(g.fstdistfunc_),
          saved_param(g.dist_func_param_), origin(g.getDataByInternalId(0)),
          stride(g.size_data_per_element_), seen(g.cur_element_count.load(), 0) {
        g.fstdistfunc_ = call;
        g.dist_func_param_ = this;
    }
    ~DistanceHook() {
        graph.fstdistfunc_ = saved;
        graph.dist_func_param_ = saved_param;
    }
    DistanceHook(const DistanceHook&) = delete;
    DistanceHook& operator=(const DistanceHook&) = delete;

    static float call(const void* query, const void* vector, const void* context) {
        auto& self = *static_cast<DistanceHook*>(const_cast<void*>(context));
        const ptrdiff_t delta = static_cast<const char*>(vector) - self.origin;
        require(delta >= 0 && delta % self.stride == 0, "invalid native vector address");
        const size_t internal = size_t(delta / self.stride);
        require(internal < self.seen.size(), "invalid native internal ID");
        ++self.work.distances;
        // Native labels are unique, so an internal-ID bitmap counts distinct
        // item IDs without allocating an item-universe bitmap for every graph.
        if (!self.seen[internal]) {
            self.seen[internal] = 1;
            ++self.work.distinct;
        }
        const auto item = self.graph.getExternalLabel(internal);
        self.work.rejected_distances += !self.allowed[self.workload.owner[item]];
        return self.saved(query, vector, self.saved_param);
    }
};
}  // namespace native_work_probe_detail

// Matches searchKnn(query, k) after setEf(ef), including its internal-ID
// comparator, upper descent, level-0 entry reevaluation, and external-label
// result heap. No deletion/filter callback path: owner-local graphs are all
// eligible. Returns the native result heap's pop order (worst to best).
//
// Counters ADD to work: initialize it once per query, then call for each allowed
// local graph. Distinct is deduplicated across both layers within each call;
// summing across disjoint owner-local graphs gives query-wide distinct items.
// Adjacency counts every inspected list entry, including repeats/visited IDs;
// expansions count list reads, not unique nodes. Distances count actual calls,
// including entry reevaluations and repeated upper-layer candidates. Thus this
// measures native multi-graph search work, not an executor-only comparison with
// a shared DAG graph: topology, entries, and per-graph search budgets differ.
static std::vector<Id> native_work_search(Graph& graph, const Workload& workload,
    const std::vector<char>& allowed, const float* query, size_t k, size_t ef,
    NativeWork& work) {
    if (graph.cur_element_count == 0) return {};
    require(graph.num_deleted_ == 0, "native work probe requires no deleted points");
    native_work_probe_detail::DistanceHook hook(graph, workload, allowed, work);

    hnswlib::tableint current = graph.enterpoint_node_;
    float current_distance = graph.fstdistfunc_(
        query, graph.getDataByInternalId(graph.enterpoint_node_), graph.dist_func_param_);
    for (int level = graph.maxlevel_; level > 0; --level) {
        bool changed = true;
        while (changed) {
            changed = false;
            auto* data = reinterpret_cast<unsigned int*>(graph.get_linklist(current, level));
            const int size = graph.getListCount(data);
            ++work.upper_expansions;
            work.upper_adjacency += size;
            auto* neighbors = reinterpret_cast<hnswlib::tableint*>(data + 1);
            for (int i = 0; i < size; ++i) {
                const hnswlib::tableint candidate = neighbors[i];
                require(candidate <= graph.max_elements_, "cand error");
                const float distance = graph.fstdistfunc_(
                    query, graph.getDataByInternalId(candidate), graph.dist_func_param_);
                if (distance < current_distance) {
                    current_distance = distance;
                    current = candidate;
                    changed = true;
                }
            }
        }
    }

    // Only level 0 runs the native metric increments; upper work was counted
    // explicitly above. These atomics count list expansions/entries, NOT calls.
    const auto hops_before = graph.metric_hops.load(std::memory_order_relaxed);
    const auto adjacency_before = graph.metric_distance_computations.load(std::memory_order_relaxed);
    auto candidates = graph.searchBaseLayerST<true, true>(current, query, std::max(ef, k));
    work.lower_expansions += size_t(graph.metric_hops.load(std::memory_order_relaxed) - hops_before);
    work.lower_adjacency += size_t(
        graph.metric_distance_computations.load(std::memory_order_relaxed) - adjacency_before);

    while (candidates.size() > k) candidates.pop();
    std::priority_queue<std::pair<float, hnswlib::labeltype>> result;
    while (!candidates.empty()) {
        const auto candidate = candidates.top();
        result.emplace(candidate.first, graph.getExternalLabel(candidate.second));
        candidates.pop();
    }
    std::vector<Id> ids;
    ids.reserve(result.size());
    while (!result.empty()) {
        ids.push_back(Id(result.top().second));
        result.pop();
    }
    return ids;
}
