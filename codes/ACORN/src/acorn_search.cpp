// NOTE (audit 2026-11-09): this file is NOT the official ACORN algorithm and must
// not be used as an ACORN baseline. It differs from guestrin-lab/ACORN in four
// places: the result heap is sized by ef instead of k, the stopping rule is
// "candidate worse than the ef-th result" instead of faiss's count_below rule,
// there is no num_found >= 2M cap on two-hop expansion, and the build prunes
// hnswlib's stored list instead of collecting 2*M*gamma candidates and pruning
// farthest-first with early termination. The baseline has to come from the
// official faiss fork.
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
                                    const float* query_vector, size_t ef, int k,
                                    size_t M_beta, size_t M,
                                    AcornSearchScratch* scratch) {
    AcornQueryResult result;
    const auto query_start = std::chrono::steady_clock::now();
    if (ef == 0 || index.object_count() == 0) return result;

    std::vector<BeamEntry>& beam = scratch->beam;   // min-heap: candidate set C
    std::vector<BestEntry>& best = scratch->best;   // max-heap: result list W
    std::vector<uint8_t>& visited = scratch->visited;
    beam.clear();
    best.clear();
    // a 1 MB clear per query is cheaper than tracking which stamps were touched
    if (visited.size() != index.object_count()) visited.assign(index.object_count(), 0);
    else std::memset(visited.data(), 0, visited.size());
    auto mark_visited = [&](hnswlib::tableint node) {
        if (visited[node]) return false;
        visited[node] = 1;
        return true;
    };

    // HNSW entry: greedy descent over the upper layers, distance only.
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
    mark_visited(entry);
    ++result.visited_nodes;

    // GET-NEIGHBORS(c, p_q): first M_beta stored neighbours filtered by the
    // predicate, then the remaining ones expanded to their neighbours.
    std::vector<hnswlib::tableint> neighbourhood;
    std::unordered_set<hnswlib::tableint> in_neighbourhood;
    auto expand_and_filter = [&](hnswlib::tableint candidate) {
        if (in_neighbourhood.size() >= M) return;
        if (!mark_visited(candidate)) return;
        ++result.visited_nodes;
        ++result.distance_calls;
        const float distance = index.distance(query_vector, index.vector_of(candidate));
        const int object_id = index.object_id_of(candidate);
        if (!predicate_space.passes(static_cast<size_t>(object_id), predicate_bits)) return;
        if (!in_neighbourhood.insert(candidate).second) return;
        neighbourhood.push_back(candidate);
        // result list W: at most ef passing objects
        if (best.size() < ef) {
            best.push_back({distance, object_id});
            std::push_heap(best.begin(), best.end());
        } else if (distance < best.front().first) {
            std::pop_heap(best.begin(), best.end());
            best.back() = {distance, object_id};
            std::push_heap(best.begin(), best.end());
        }
        // candidate set C holds passing nodes only
        beam.push_back({-distance, candidate});
        std::push_heap(beam.begin(), beam.end());
        (void)object_id;
    };

    auto neighborhood_of = [&](hnswlib::tableint node) {
        neighbourhood.clear();
        in_neighbourhood.clear();
        size_t count = 0;
        const hnswlib::tableint* neighbours = index.neighbors_of(node, &count);
        const size_t head = std::min(M_beta, count);
        for (size_t i = 0; i < head; ++i) expand_and_filter(neighbours[i]);
        for (size_t i = head; i < count; ++i) {
            size_t second_count = 0;
            const hnswlib::tableint* second = index.neighbors_of(neighbours[i], &second_count);
            for (size_t j = 0; j < second_count; ++j) expand_and_filter(second[j]);
        }
    };

    // seed the search from the entry point's own neighbourhood
    if (predicate_space.passes(static_cast<size_t>(index.object_id_of(entry)), predicate_bits)) {
        best.push_back({entry_distance, index.object_id_of(entry)});
        std::push_heap(best.begin(), best.end());
        beam.push_back({-entry_distance, entry});
        std::push_heap(beam.begin(), beam.end());
    }

    while (!beam.empty()) {
        std::pop_heap(beam.begin(), beam.end());
        const BeamEntry current = beam.back();
        beam.pop_back();
        const float current_distance = -current.first;
        // HNSW stopping rule (line 7 of Algorithm 2)
        if (best.size() >= ef && current_distance > best.front().first) break;
        neighborhood_of(current.second);
    }

    std::sort(best.begin(), best.end());
    const size_t returned = std::min(best.size(), static_cast<size_t>(std::max(k, 0)));
    result.object_ids.reserve(returned);
    for (size_t i = 0; i < returned; ++i) result.object_ids.push_back(best[i].second);
    result.query_ms = elapsed_ms(query_start);
    return result;
}
