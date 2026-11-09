#include "asi_search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <queue>
#include <unordered_set>

#include "raw_l2_space.h"

namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

}  // namespace

QueryStats run_queries(const Dataset& dataset, const AsiIndex& index, const QueryOptions& options) {
    const int dim = dataset.dim;
    const int k = dataset.k;
    const int ef = options.ef;
    RawL2Space space{size_t(dim)};

    const size_t query_count = options.max_queries > 0
        ? std::min<size_t>(size_t(options.max_queries), dataset.num_queries())
        : dataset.num_queries();
    const size_t node_count = index.num_objects();

    // per-query scratch, reused across queries through monotonically growing stamps
    std::vector<uint32_t> visited_stamp(node_count, 0);
    std::vector<uint32_t> expanded_stamp(node_count, 0);
    std::vector<uint32_t> seed_score_stamp(node_count, 0);
    std::vector<float> seed_score(node_count, 0.0f);
    uint32_t query_token = 0;
    std::vector<uint64_t> predicate_bitmap(size_t(index.closure_words), 0);
    std::vector<uint64_t> navigable_bitmap(size_t(index.closure_words), 0);
    std::vector<uint64_t> ancestor_bitmap(size_t(index.closure_words), 0);

    // attribute position lookup for walking up the DAG
    std::unordered_map<int, int> attribute_position;
    for (int a = 0; a < index.attribute_count; ++a)
        attribute_position.emplace(index.attribute_ids[size_t(a)], a);

    // root entries: attributes with no parent in the DAG
    std::vector<int> root_attributes;
    for (int a = 0; a < index.attribute_count; ++a) {
        int external = index.attribute_ids[size_t(a)];
        const auto found = dataset.parents_of.find(external);
        if (found == dataset.parents_of.end() || found->second.empty())
            root_attributes.push_back(a);
    }

    FILE* trace = nullptr;
    if (!options.trace_path.empty()) {
        trace = std::fopen(options.trace_path.c_str(), "w");
        if (!trace) std::perror(options.trace_path.c_str());
    }

    long long hits = 0, ground_truth_entries = 0;
    long long distances = 0, cache_hits = 0, expansions = 0, adjacency_slots = 0;
    long long representative_scores = 0, rerank_calls = 0;
    long long eligible_sum = 0, attributes_in_scope_sum = 0, attributes_entered_sum = 0;
    double seconds = 0.0;
    const size_t timed_queries = query_count * size_t(options.repeats);

    for (int repeat_index = 0; repeat_index < options.repeats; ++repeat_index) {
        for (size_t qi = 0; qi < query_count; ++qi) {
            const auto query_started = std::chrono::steady_clock::now();
            const long long distances_before = distances;

            std::vector<uint32_t> evaluated_objects, admitted_objects, seed_scored_objects,
                expanded_objects, seed_nodes;
            long long query_expansions = 0, query_slots = 0, query_cache_hits = 0;
            long long query_representative_scores = 0;
            long long query_rerank_calls = 0;

            // 1. predicate = OR of the descendant closures of the query attributes
            std::fill(predicate_bitmap.begin(), predicate_bitmap.end(), 0ull);
            for (int attribute_id : dataset.query_attributes[qi]) {
                int position = -1;
                {
                    const auto found = std::lower_bound(index.attribute_ids.begin(),
                                                         index.attribute_ids.end(), attribute_id);
                    if (found != index.attribute_ids.end() && *found == attribute_id)
                        position = int(found - index.attribute_ids.begin());
                }
                if (position < 0) continue;
                const uint64_t* closure =
                    &index.closure_bits[size_t(position) * size_t(index.closure_words)];
                for (int w = 0; w < index.closure_words; ++w) predicate_bitmap[size_t(w)] |= closure[w];
            }
            auto in_scope_attribute = [&](uint32_t attribute) {
                return ((predicate_bitmap[attribute / 64] >> (attribute % 64)) & 1ull) != 0;
            };

            // 2. navigable set A = Desc+(U) u Anc+(U): every node on a root-to-target
            //    path may be traversed, but only D members may be returned.
            std::fill(ancestor_bitmap.begin(), ancestor_bitmap.end(), 0ull);
            {
                std::vector<int> frontier;
                for (int attribute_id : dataset.query_attributes[qi]) {
                    const auto found = attribute_position.find(attribute_id);
                    if (found != attribute_position.end()) frontier.push_back(found->second);
                }
                std::unordered_set<int> seen(frontier.begin(), frontier.end());
                while (!frontier.empty()) {
                    std::vector<int> next;
                    for (int a : frontier) {
                        ancestor_bitmap[size_t(a) / 64] |= 1ull << (size_t(a) % 64);
                        const auto up = dataset.parents_of.find(index.attribute_ids[size_t(a)]);
                        if (up == dataset.parents_of.end()) continue;
                        for (int parent : up->second) {
                            const auto position = attribute_position.find(parent);
                            if (position == attribute_position.end()) continue;
                            if (seen.insert(position->second).second) next.push_back(position->second);
                        }
                    }
                    frontier.swap(next);
                }
            }
            for (int w = 0; w < index.closure_words; ++w)
                navigable_bitmap[size_t(w)] = predicate_bitmap[size_t(w)] | ancestor_bitmap[size_t(w)];
            auto navigable_attribute = [&](uint32_t attribute) {
                return ((navigable_bitmap[attribute / 64] >> (attribute % 64)) & 1ull) != 0;
            };

            std::vector<int> in_scope_attributes;
            for (int w = 0; w < index.closure_words; ++w) {
                uint64_t bits = predicate_bitmap[size_t(w)];
                while (bits) {
                    const int b = __builtin_ctzll(bits);
                    bits &= bits - 1;
                    const int attribute = w * 64 + b;
                    if (attribute < index.attribute_count &&
                        index.node_begin[size_t(attribute) + 1] > index.node_begin[size_t(attribute)])
                        in_scope_attributes.push_back(attribute);
                }
            }
            if (in_scope_attributes.empty()) continue;

            ++query_token;
            attributes_in_scope_sum += (long long)in_scope_attributes.size();
            long long eligible = 0;
            for (int attribute : in_scope_attributes)
                eligible += (long long)(index.node_begin[size_t(attribute) + 1] -
                                        index.node_begin[size_t(attribute)]);
            eligible_sum += eligible;

            const float* query_vector = dataset.query_vector(qi);
            auto distance_to_node = [&](uint32_t node) {
                return space.dist(query_vector, dataset.object_vector(index.node_object[node]));
            };

            std::priority_queue<std::pair<float, uint32_t>> keep_heap;       // max-heap of kept results
            std::unordered_set<uint32_t> entered_attributes, touched_attributes;

            // cached score: seeds may be scored several times per query
            auto cached_distance = [&](uint32_t node) {
                if (seed_score_stamp[node] == query_token) {
                    ++query_cache_hits;
                    return seed_score[node];
                }
                const float d = distance_to_node(node);
                ++distances;
                seed_score_stamp[node] = query_token;
                seed_score[node] = d;
                return d;
            };

            // Two bounded sets with distinct roles:
            //   keep_heap  : capacity ef, holds ONLY returnable (D) nodes -> the result set
            //   frontier   : capacity ef, holds every traversable (A) node -> the beam
            // Ancestors therefore never evict a candidate result.
            std::priority_queue<std::pair<float, uint32_t>> frontier;  // max-heap on (-d, node)
            auto offer = [&](uint32_t node) {
                if (visited_stamp[node] == query_token) return;   // first touch, once per query
                visited_stamp[node] = query_token;
                if (!navigable_attribute(index.node_attribute[node])) return;
                const bool returnable = in_scope_attribute(index.node_attribute[node]);
                if (returnable) touched_attributes.insert(index.node_attribute[node]);
                float d;
                if (seed_score_stamp[node] == query_token) {
                    d = seed_score[node];
                    ++query_cache_hits;
                } else {
                    d = distance_to_node(node);
                    ++distances;
                }
                if (trace && repeat_index == 0) evaluated_objects.push_back(index.node_object[node]);
                if (returnable) {
                    bool admitted = false;
                    if (int(keep_heap.size()) < ef) {
                        keep_heap.push({d, node});
                        admitted = true;
                    } else if (d < keep_heap.top().first) {
                        keep_heap.pop();
                        keep_heap.push({d, node});
                        admitted = true;
                    }
                    if (admitted) {
                        entered_attributes.insert(index.node_attribute[node]);
                        if (trace && repeat_index == 0) admitted_objects.push_back(index.node_object[node]);
                    }
                }
                // the beam is bounded by ef as well and accepts ancestors
                const float worst = frontier.empty() ? kInfinity : -frontier.top().first;
                if (int(frontier.size()) < ef || d < worst) {
                    if (int(frontier.size()) >= ef) frontier.pop();
                    frontier.push({-d, node});
                }
            };

            auto expand = [&](uint32_t node) {
                if (expanded_stamp[node] == query_token) return;   // expand each node at most once
                expanded_stamp[node] = query_token;
                ++query_expansions;
                query_slots += (long long)(index.local_edge_begin[node + 1] - index.local_edge_begin[node]);
                query_slots += (long long)(index.bridge_edge_begin[node + 1] - index.bridge_edge_begin[node]);
                if (trace && repeat_index == 0) expanded_objects.push_back(index.node_object[node]);
                for (uint32_t j = index.local_edge_begin[node]; j < index.local_edge_begin[node + 1]; ++j)
                    offer(index.local_edge_targets[j]);
                for (uint32_t j = index.bridge_edge_begin[node]; j < index.bridge_edge_begin[node + 1]; ++j)
                    offer(index.bridge_edge_targets[j]);
            };

            // entry: every in-scope attribute contributes its stored entry points,
            // and each of them is expanded once regardless of the keep threshold.
            // This is what guarantees the beam touches the whole closure; the
            // inverted index makes the lookup O(entries) instead of scoring all
            // representatives of the attribute.
            auto seed_entry = [&](uint32_t node) {
                if (expanded_stamp[node] == query_token) return;   // already expanded once
                if (!navigable_attribute(index.node_attribute[node])) return;
                const bool fresh = visited_stamp[node] != query_token;
                if (!fresh) { expand(node); return; }               // scored already, still expand
                visited_stamp[node] = query_token;
                const float d = distance_to_node(node);
                ++distances;
                if (trace && repeat_index == 0) evaluated_objects.push_back(index.node_object[node]);
                if (in_scope_attribute(index.node_attribute[node])) {
                    touched_attributes.insert(index.node_attribute[node]);
                    if (int(keep_heap.size()) < ef) {
                        keep_heap.push({d, node});
                    } else if (d < keep_heap.top().first) {
                        keep_heap.pop();
                        keep_heap.push({d, node});
                    }
                    entered_attributes.insert(index.node_attribute[node]);
                    if (trace && repeat_index == 0) admitted_objects.push_back(index.node_object[node]);
                }
                expand(node);
            };

            const int entry_expand = std::max(1, options.entry_expand_per_attribute);
            for (int attribute : in_scope_attributes) {
                if (size_t(attribute) >= index.attribute_entries.size()) continue;
                const auto& candidates = index.attribute_entries[size_t(attribute)];
                if (candidates.empty()) continue;
                // score every stored entry of this attribute, expand only the nearest t
                std::vector<std::pair<float, uint32_t>> scored;
                scored.reserve(candidates.size());
                for (uint32_t node : candidates) {
                    if (!navigable_attribute(index.node_attribute[node])) continue;
                    float d;
                    if (seed_score_stamp[node] == query_token) {
                        d = seed_score[node];
                        ++query_cache_hits;
                    } else {
                        d = distance_to_node(node);
                        ++distances;
                        seed_score_stamp[node] = query_token;
                        seed_score[node] = d;
                    }
                    if (trace && repeat_index == 0) evaluated_objects.push_back(index.node_object[node]);
                    scored.emplace_back(d, node);
                }
                std::sort(scored.begin(), scored.end());
                const size_t take = std::min<size_t>(size_t(entry_expand), scored.size());
                for (size_t j = 0; j < take; ++j) {
                    if (trace && repeat_index == 0) seed_nodes.push_back(index.node_object[scored[j].second]);
                    seed_entry(scored[j].second);
                }
            }

            // single shared beam search; stop once the nearest unexplored candidate
            // is worse than the worst kept result (hnswlib termination rule)
            while (!frontier.empty()) {
                const std::pair<float, uint32_t>& closest = frontier.top();
                if (int(keep_heap.size()) >= ef && -closest.first > keep_heap.top().first) break;
                const uint32_t node = closest.second;
                frontier.pop();
                expand(node);
            }

            // results: the single keep heap, deduplicated, ordered by distance
            std::vector<std::pair<float, int>> results;
            {
                auto drain = [&](std::priority_queue<std::pair<float, uint32_t>> heap) {
                    while (!heap.empty()) {
                        const auto entry = heap.top();
                        heap.pop();
                        if (in_scope_attribute(index.node_attribute[entry.second]))
                            results.push_back({entry.first, int(index.node_object[entry.second])});
                    }
                };
                drain(keep_heap);
            }
            std::sort(results.begin(), results.end());
            results.erase(std::unique(results.begin(), results.end(),
                                      [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                                          return a.second == b.second;
                                      }),
                          results.end());

            if (options.rerank_fp32) {
                if (options.rerank_pool > 0 && results.size() > size_t(options.rerank_pool))
                    results.resize(size_t(options.rerank_pool));
                for (auto& entry : results) {
                    entry.first = space.dist(query_vector, dataset.object_vector(size_t(entry.second)));
                    ++distances;
                    ++query_rerank_calls;
                }
                std::sort(results.begin(), results.end());
            }

            const size_t returned = std::min<size_t>(size_t(k), results.size());
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - query_started).count();

            // recall against the stored ground truth
            size_t valid_gt = 0, query_hits = 0;
            for (int slot = 0; slot < k; ++slot) {
                const int32_t gt_object = dataset.ground_truth[qi * size_t(k) + size_t(slot)];
                if (gt_object < 0) continue;
                ++valid_gt;
                for (size_t r = 0; r < returned; ++r)
                    if (results[r].second == int(gt_object)) { ++query_hits; break; }
            }
            ground_truth_entries += (long long)std::min<size_t>(size_t(valid_gt), size_t(k));
            hits += (long long)query_hits;
            distances += 0;
            cache_hits += query_cache_hits;
            expansions += query_expansions;
            adjacency_slots += query_slots;
            representative_scores += query_representative_scores;
            rerank_calls += query_rerank_calls;
            attributes_entered_sum += (long long)entered_attributes.size();
            (void)distances_before;

            if (trace && repeat_index == 0) {
                auto write_ids = [&](const char* key, const std::vector<uint32_t>& objects) {
                    std::fprintf(trace, ",\"%s\":[", key);
                    for (size_t j = 0; j < objects.size(); ++j)
                        std::fprintf(trace, "%s%u", j ? "," : "", objects[j]);
                    std::fprintf(trace, "]");
                };
                std::vector<uint32_t> returned_objects;
                for (size_t r = 0; r < returned; ++r) returned_objects.push_back(uint32_t(results[r].second));
                std::vector<uint32_t> entered_ids, touched_ids;
                for (uint32_t attribute : entered_attributes)
                    entered_ids.push_back(uint32_t(index.attribute_ids[attribute]));
                for (uint32_t attribute : touched_attributes)
                    touched_ids.push_back(uint32_t(index.attribute_ids[attribute]));
                std::fprintf(trace, "{\"ef\":%d,\"qi\":%zu,\"eligible\":%lld"
                                    ",\"distance_calls\":%lld,\"distance_cache_hits\":%lld"
                                    ",\"adjacency_slots\":%lld,\"expanded\":%lld"
                                    ",\"reps_scored\":%lld",
                             ef, qi, eligible,
                             distances - distances_before, query_cache_hits, query_slots,
                             query_expansions, query_representative_scores);
                write_ids("returned", returned_objects);
                write_ids("evaluated", evaluated_objects);
                write_ids("admitted", admitted_objects);
                write_ids("seed_evaluated", seed_scored_objects);
                write_ids("seed_nodes", seed_nodes);
                write_ids("expanded_objects", expanded_objects);
                write_ids("entered_attrs", entered_ids);
                write_ids("touched_attrs", touched_ids);
                std::fprintf(trace, "}\n");
            }
        }
    }
    if (trace) std::fclose(trace);

    QueryStats stats;
    stats.ef = ef;
    stats.ground_truth_entries = ground_truth_entries;
    stats.hits = hits;
    stats.recall = double(hits) / double(std::max<long long>(1, ground_truth_entries));
    stats.seconds = seconds;
    stats.qps = double(timed_queries) / std::max(1e-9, seconds);
    stats.distances_per_query = double(distances) / double(std::max<size_t>(1, timed_queries));
    stats.distance_cache_hits_per_query = double(cache_hits) / double(std::max<size_t>(1, timed_queries));
    stats.expanded_per_query = double(expansions) / double(std::max<size_t>(1, timed_queries));
    stats.adjacency_slots_per_query = double(adjacency_slots) / double(std::max<size_t>(1, timed_queries));
    stats.representative_scores_per_query = double(representative_scores) / double(std::max<size_t>(1, timed_queries));
    stats.eligible_per_query = double(eligible_sum) / double(std::max<size_t>(1, timed_queries));
    stats.attributes_in_scope_per_query = double(attributes_in_scope_sum) / double(std::max<size_t>(1, timed_queries));
    stats.attributes_entered_per_query = double(attributes_entered_sum) / double(std::max<size_t>(1, timed_queries));
    stats.rerank_calls_per_query = double(rerank_calls) / double(std::max<size_t>(1, timed_queries));
    return stats;
}
