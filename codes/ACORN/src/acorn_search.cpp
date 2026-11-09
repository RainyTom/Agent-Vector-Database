#include "acorn_search.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>

namespace {

using BeamEntry = std::pair<float, hnswlib::tableint>;
using BestEntry = std::pair<float, int>;

void beam_push(std::vector<BeamEntry>& beam, float distance, hnswlib::tableint node) {
    beam.emplace_back(distance, node);
    std::push_heap(beam.begin(), beam.end(), std::greater<BeamEntry>());
}

BeamEntry beam_pop(std::vector<BeamEntry>& beam) {
    std::pop_heap(beam.begin(), beam.end(), std::greater<BeamEntry>());
    const BeamEntry entry = beam.back();
    beam.pop_back();
    return entry;
}

// Result heap: max-heap by distance, at most `ef` predicate-passing objects.
void best_offer(std::vector<BestEntry>& best, size_t ef, float distance, int object_id) {
    if (best.size() < ef) {
        best.emplace_back(distance, object_id);
        std::push_heap(best.begin(), best.end());
        return;
    }
    if (distance < best.front().first) {
        std::pop_heap(best.begin(), best.end());
        best.back() = BestEntry(distance, object_id);
        std::push_heap(best.begin(), best.end());
    }
}

double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

void AcornPredicateSpace::build(const Dataset& dataset) {
    const std::vector<int> attribute_ids = collect_attribute_ids(dataset);
    attribute_count_ = static_cast<int>(attribute_ids.size());
    dense_of_attribute_.clear();
    dense_of_attribute_.reserve(attribute_ids.size());
    for (size_t i = 0; i < attribute_ids.size(); ++i)
        dense_of_attribute_.emplace(attribute_ids[i], static_cast<int>(i));

    attribute_member_count_.assign(attribute_ids.size(), 0);
    owner_attribute_dense_.assign(dataset.num_objects(), -1);
    for (size_t object_id = 0; object_id < dataset.num_objects(); ++object_id) {
        const auto found = dense_of_attribute_.find(dataset.owner[object_id]);
        if (found == dense_of_attribute_.end()) continue;
        owner_attribute_dense_[object_id] = found->second;
        ++attribute_member_count_[static_cast<size_t>(found->second)];
    }
}

void AcornPredicateSpace::closure_bitmap(const Dataset& dataset,
                                         const std::vector<int>& query_attributes,
                                         std::vector<uint64_t>* bits,
                                         std::vector<int>* closed_attributes) const {
    bits->assign((static_cast<size_t>(attribute_count_) + 63) / 64, 0);
    closed_attributes->clear();
    std::vector<int> pending = query_attributes;   // attribute ids, not dense ids
    while (!pending.empty()) {
        const int attribute = pending.back();
        pending.pop_back();
        const auto found = dense_of_attribute_.find(attribute);
        if (found == dense_of_attribute_.end()) continue;
        const int dense = found->second;
        const uint64_t mask = uint64_t(1) << (dense & 63);
        if ((*bits)[static_cast<size_t>(dense) >> 6] & mask) continue;   // already closed over
        (*bits)[static_cast<size_t>(dense) >> 6] |= mask;
        closed_attributes->push_back(dense);
        const auto children = dataset.children_of.find(attribute);
        if (children != dataset.children_of.end())
            for (int child : children->second) pending.push_back(child);
    }
}

size_t AcornPredicateSpace::eligible_count(const std::vector<int>& closed_attributes) const {
    size_t total = 0;
    for (int attribute : closed_attributes)
        total += attribute_member_count_[static_cast<size_t>(attribute)];
    return total;
}

size_t AcornPredicateSpace::exact_eligible_count(const Dataset& dataset,
                                                 const std::vector<uint64_t>& bits) const {
    size_t total = 0;
    for (size_t object_id = 0; object_id < dataset.num_objects(); ++object_id)
        if (passes(object_id, bits)) ++total;
    return total;
}

AcornQueryResult acorn_search_query(const AcornIndex& index,
                                    const AcornPredicateSpace& predicate_space,
                                    const std::vector<uint64_t>& predicate_bits,
                                    const float* query_vector, size_t ef, int k, bool two_hop,
                                    AcornSearchScratch* scratch) {
    const auto query_start = std::chrono::steady_clock::now();
    AcornQueryResult result;
    const size_t object_count = index.object_count();
    if (object_count == 0 || ef == 0) {
        result.query_ms = elapsed_ms(query_start);
        return result;
    }

    if (scratch->visited.size() < object_count) scratch->visited.assign(object_count, 0);
    std::memset(scratch->visited.data(), 0, object_count);
    std::vector<BeamEntry>& beam = scratch->beam;
    std::vector<BestEntry>& best = scratch->best;
    beam.clear();
    best.clear();

    // HNSW entry: greedy descent over the upper layers, distance only (the
    // predicate is applied exclusively at layer 0), then ACORN traversal.
    hnswlib::tableint entry = index.entry_point();
    float entry_distance = index.distance(query_vector, index.vector_of(entry));
    ++result.distance_calls;
    for (int level = index.max_level(); level >= 1; --level) {
        bool improved = true;
        while (improved) {
            improved = false;
            size_t upper_count = 0;
            const hnswlib::tableint* upper = index.neighbors_at_level(entry, level, &upper_count);
            for (size_t i = 0; i < upper_count; ++i) {
                const hnswlib::tableint candidate = upper[i];
                if (candidate == entry) continue;
                ++result.distance_calls;
                const float distance = index.distance(query_vector, index.vector_of(candidate));
                if (distance < entry_distance) {
                    entry_distance = distance;
                    entry = candidate;
                    improved = true;
                }
            }
        }
    }
    ++result.visited_nodes;
    scratch->visited[entry] = 1;
    beam_push(beam, entry_distance, entry);
    if (predicate_space.passes(static_cast<size_t>(index.object_id_of(entry)), predicate_bits))
        best_offer(best, ef, entry_distance, index.object_id_of(entry));

    while (!beam.empty()) {
        const BeamEntry current = beam_pop(beam);
        // HNSW stopping rule; non-passing candidates also drive the frontier.
        if (best.size() >= ef && current.first > best.front().first) break;

        size_t neighbour_count = 0;
        const hnswlib::tableint* neighbours = index.neighbors_of(current.second, &neighbour_count);
        for (size_t i = 0; i < neighbour_count; ++i) {
            const hnswlib::tableint neighbour = neighbours[i];
            if (scratch->visited[neighbour]) continue;
            scratch->visited[neighbour] = 1;
            ++result.visited_nodes;
            ++result.distance_calls;
            const float distance = index.distance(query_vector, index.vector_of(neighbour));
            if (best.size() < ef || distance <= best.front().first) {
                beam_push(beam, distance, neighbour);
                const int object_id = index.object_id_of(neighbour);
                if (predicate_space.passes(static_cast<size_t>(object_id), predicate_bits))
                    best_offer(best, ef, distance, object_id);
            }
        }

        // ACORN-1 two-hop expansion: still short of ef passing results, so look
        // at neighbours of neighbours of the current node.
        if (two_hop && best.size() < ef) {
            for (size_t i = 0; i < neighbour_count; ++i) {
                size_t second_count = 0;
                const hnswlib::tableint* second = index.neighbors_of(neighbours[i], &second_count);
                for (size_t j = 0; j < second_count; ++j) {
                    const hnswlib::tableint reached = second[j];
                    if (scratch->visited[reached]) continue;
                    scratch->visited[reached] = 1;
                    ++result.visited_nodes;
                    ++result.distance_calls;
                    const float distance = index.distance(query_vector, index.vector_of(reached));
                    if (best.size() < ef || distance <= best.front().first) {
                        beam_push(beam, distance, reached);
                        const int object_id = index.object_id_of(reached);
                        if (predicate_space.passes(static_cast<size_t>(object_id), predicate_bits))
                            best_offer(best, ef, distance, object_id);
                    }
                }
            }
        }
    }

    // `best` is a max-heap; sort ascending and keep the first k.
    std::sort(best.begin(), best.end());
    const size_t returned = std::min(best.size(), static_cast<size_t>(std::max(k, 0)));
    result.object_ids.reserve(returned);
    for (size_t i = 0; i < returned; ++i) result.object_ids.push_back(best[i].second);
    result.query_ms = elapsed_ms(query_start);
    return result;
}