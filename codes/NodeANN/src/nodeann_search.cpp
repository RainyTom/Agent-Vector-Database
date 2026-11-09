#include "nodeann_search.h"

#include <algorithm>
#include <chrono>
#include <queue>
#include <unordered_set>

std::vector<int> expand_attribute_closure(const Dataset& dataset,
                                          const std::vector<int>& query_attributes) {
    std::unordered_set<int> closure;
    std::vector<int> pending = query_attributes;
    while (!pending.empty()) {
        const int attribute = pending.back();
        pending.pop_back();
        if (!closure.insert(attribute).second) continue;
        const auto children = dataset.children_of.find(attribute);
        if (children != dataset.children_of.end())
            pending.insert(pending.end(), children->second.begin(), children->second.end());
    }
    std::vector<int> closure_attributes(closure.begin(), closure.end());
    std::sort(closure_attributes.begin(), closure_attributes.end());
    return closure_attributes;
}

namespace {

struct QueryResult {
    std::vector<int> object_ids;   // ascending by (distance, object id)
    int eligible = 0;
    unsigned long long graph_searches = 0;
};

QueryResult search_one(const Dataset& dataset, NodeANNIndex& index, size_t query_id, int per_n) {
    QueryResult result;
    const std::vector<int> scope =
        expand_attribute_closure(dataset, dataset.query_attributes[query_id]);
    const float* query_vector = dataset.query_vector(query_id);

    // Max-heap keyed by (distance, object id): the top entry is the worst kept.
    std::priority_queue<std::pair<float, int>> nearest;
    for (int attribute : scope) {
        const auto members = index.attribute_members.find(attribute);
        if (members == index.attribute_members.end() || members->second.empty()) continue;
        result.eligible += static_cast<int>(members->second.size());

        const auto graph = index.attribute_graphs.find(attribute);
        if (graph == index.attribute_graphs.end()) continue;
        ++result.graph_searches;

        auto candidates = graph->second->searchKnn(query_vector, size_t(per_n));
        while (!candidates.empty()) {
            const float distance = candidates.top().first;
            const int object_id = members->second[size_t(candidates.top().second)];
            candidates.pop();
            if (int(nearest.size()) < dataset.k) {
                nearest.push({distance, object_id});
            } else if (distance < nearest.top().first) {
                nearest.pop();
                nearest.push({distance, object_id});
            }
        }
    }
    result.object_ids.reserve(nearest.size());
    while (!nearest.empty()) {
        result.object_ids.push_back(nearest.top().second);
        nearest.pop();
    }
    std::reverse(result.object_ids.begin(), result.object_ids.end());
    return result;
}

}  // namespace

SearchStats run_search(const Dataset& dataset, NodeANNIndex& index, int per_n, int ef, int repeat,
                       int query_limit) {
    index.set_ef(ef);
    const size_t query_count =
        std::min(dataset.num_queries(), size_t(std::max(query_limit, 0)));

    SearchStats stats;
    stats.per_n = per_n;
    stats.ef = ef;

    unsigned long long hits = 0;
    unsigned long long relevant = 0;
    unsigned long long distance_calls = 0;
    unsigned long long graph_searches = 0;
    unsigned long long eligible_total = 0;
    double fastest_pass_ms = 0.0;

    for (int pass = 0; pass < std::max(repeat, 1); ++pass) {
        const auto pass_start = std::chrono::steady_clock::now();
        const unsigned long long distance_calls_before = index.space.distance_calls;

        for (size_t query_id = 0; query_id < query_count; ++query_id) {
            const QueryResult result = search_one(dataset, index, query_id, per_n);
            graph_searches += result.graph_searches;
            eligible_total += static_cast<unsigned long long>(result.eligible);

            if (pass != 0) continue;
            const std::unordered_set<int> returned(result.object_ids.begin(), result.object_ids.end());
            int relevant_for_query = 0;
            int hits_for_query = 0;
            for (int rank = 0; rank < dataset.k; ++rank) {
                const int32_t object_id = dataset.ground_truth[query_id * size_t(dataset.k) + size_t(rank)];
                if (object_id < 0) continue;
                ++relevant_for_query;
                if (returned.count(int(object_id))) ++hits_for_query;
            }
            hits += static_cast<unsigned long long>(hits_for_query);
            relevant += static_cast<unsigned long long>(relevant_for_query);
        }

        const double pass_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pass_start).count();
        distance_calls += index.space.distance_calls - distance_calls_before;
        if (pass == 0 || pass_ms < fastest_pass_ms) fastest_pass_ms = pass_ms;
    }

    const double passes = double(std::max(repeat, 1));
    const double queries = double(std::max(size_t(1), query_count));
    stats.recall = double(hits) / double(std::max<unsigned long long>(relevant, 1));
    stats.qps = queries * 1000.0 / fastest_pass_ms;
    stats.dists_per_query = double(distance_calls) / (queries * passes);
    stats.graph_searches_per_query = double(graph_searches) / (queries * passes);
    stats.eligible_per_query = double(eligible_total) / (queries * passes);
    return stats;
}