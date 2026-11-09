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

// Result heap of capacity k (the official search sizes its result heap by k).
class ResultHeap {
public:
    explicit ResultHeap(size_t capacity) : capacity_(capacity) {}
    void offer(float distance, int object_id) {
        if (size_ < capacity_) {
            entries_.emplace_back(distance, object_id);
            std::push_heap(entries_.begin(), entries_.end());
            ++size_;
        } else if (capacity_ && distance < entries_.front().first) {
            std::pop_heap(entries_.begin(), entries_.end());
            entries_.back() = {distance, object_id};
            std::push_heap(entries_.begin(), entries_.end());
        }
    }
    std::vector<int> sorted_ids() const {
        std::vector<std::pair<float, int>> copy = entries_;
        std::sort(copy.begin(), copy.end());
        std::vector<int> ids;
        ids.reserve(copy.size());
        for (const auto& entry : copy) ids.push_back(entry.second);
        return ids;
    }
private:
    std::vector<std::pair<float, int>> entries_;
    size_t capacity_ = 0;
    size_t size_ = 0;
};

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

AcornQueryResult acorn_prefilter_query(const AcornIndex& index,
                                       const AcornPredicateSpace& predicate_space,
                                       const std::vector<uint64_t>& predicate_bits,
                                       const float* query_vector, int k) {
    AcornQueryResult result;
    const auto query_start = std::chrono::steady_clock::now();
    ResultHeap results(size_t(std::max(k, 0)));
    for (size_t object_id = 0; object_id < index.object_count(); ++object_id) {
        if (!predicate_space.passes(object_id, predicate_bits)) continue;
        ++result.distance_calls;
        results.offer(index.distance(query_vector, index.vector_of(hnswlib::tableint(object_id))),
                      static_cast<int>(object_id));
    }
    result.object_ids = results.sorted_ids();
    result.visited_nodes = result.distance_calls;
    result.query_ms = elapsed_ms(query_start);
    return result;
}

AcornQueryResult acorn_search_query(const AcornIndex& index,
                                    const AcornPredicateSpace& predicate_space,
                                    const std::vector<uint64_t>& predicate_bits,
                                    const float* query_vector, size_t ef, int k,
                                    size_t M_beta, size_t M,
                                    AcornSearchScratch* scratch) {
    // Faithful port of faiss/impl/ACORN.cpp hybrid_search_from_candidates
    // (L1194-1360) onto the hnswlib graph:
    //   * the result heap holds k entries, not ef (L1237);
    //   * the candidate queue holds ef = max(efSearch, k) entries (L1445-1448);
    //   * termination is count_below(d0) >= efSearch (L1262-1268), which is the
    //     relative-distance rule ACORN inherits from faiss HNSW;
    //   * level-0 expansion: the first M_beta neighbours are taken as stored,
    //     neighbours past that index are expanded to their own lists, and the
    //     expansion stops once 2*M predicate-passing neighbours were seen
    //     (L1319-1330).
    AcornQueryResult result;
    const auto query_start = std::chrono::steady_clock::now();
    const size_t result_capacity = size_t(std::max(k, 0));
    if (ef == 0 || result_capacity == 0 || index.object_count() == 0) return result;

    std::vector<uint8_t>& visited = scratch->visited;
    if (visited.size() != index.object_count()) visited.assign(index.object_count(), 0);
    else std::memset(visited.data(), 0, visited.size());

    std::vector<BeamEntry>& candidates = scratch->beam;   // min-heap: (distance, node)
    candidates.clear();
    ResultHeap results(result_capacity);

    // greedy descent over the upper layers, distance only (no predicate)
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
    visited[entry] = 1;
    ++result.visited_nodes;
    candidates.push_back({entry_distance, entry});
    std::push_heap(candidates.begin(), candidates.end(), std::greater<BeamEntry>());

    auto consider = [&](hnswlib::tableint node, float distance) {
        const int object_id = index.object_id_of(node);
        results.offer(distance, object_id);
        // bounded candidate queue, exactly like MinimaxHeap(ef)
        if (candidates.size() < ef) {
            candidates.push_back({distance, node});
            std::push_heap(candidates.begin(), candidates.end(), std::greater<BeamEntry>());
        } else if (distance < candidates.front().first) {
            std::pop_heap(candidates.begin(), candidates.end(), std::greater<BeamEntry>());
            candidates.back() = {distance, node};
            std::push_heap(candidates.begin(), candidates.end(), std::greater<BeamEntry>());
        }
    };
    consider(entry, entry_distance);

    while (!candidates.empty()) {
        std::pop_heap(candidates.begin(), candidates.end(), std::greater<BeamEntry>());
        const BeamEntry current = candidates.back();
        candidates.pop_back();
        const float current_distance = current.first;

        // count_below(d0) >= efSearch -> stop
        size_t below = 0;
        for (const BeamEntry& other : candidates)
            if (other.first < current_distance) ++below;
        if (below >= ef) break;

        size_t degree = 0;
        const hnswlib::tableint* neighbours = index.neighbors_of(current.second, &degree);
        size_t num_found = 0;
        bool keep_expanding = true;
        for (size_t j = 0; j < degree; ++j) {
            const hnswlib::tableint neighbour = neighbours[j];
            const bool passing = predicate_space.passes(
                static_cast<size_t>(index.object_id_of(neighbour)), predicate_bits);
            if (passing) {
                ++num_found;
                if (!visited[neighbour]) {
                    visited[neighbour] = 1;
                    ++result.visited_nodes;
                    ++result.distance_calls;
                    consider(neighbour, index.distance(query_vector, index.vector_of(neighbour)));
                    if (num_found >= M * 2) { keep_expanding = false; break; }
                }
            }
            // expansion of the tail of the neighbour list, regardless of whether
            // this particular neighbour passes the predicate
            if (j >= M_beta && keep_expanding) {
                size_t second_degree = 0;
                const hnswlib::tableint* second = index.neighbors_of(neighbour, &second_degree);
                for (size_t j2 = 0; j2 < second_degree; ++j2) {
                    const hnswlib::tableint reached = second[j2];
                    if (!predicate_space.passes(
                            static_cast<size_t>(index.object_id_of(reached)), predicate_bits))
                        continue;
                    ++num_found;
                    if (visited[reached]) continue;
                    visited[reached] = 1;
                    ++result.visited_nodes;
                    ++result.distance_calls;
                    consider(reached, index.distance(query_vector, index.vector_of(reached)));
                    if (num_found >= M * 2) { keep_expanding = false; break; }
                }
                if (!keep_expanding) break;
            }
        }
    }

    result.object_ids = results.sorted_ids();
    result.query_ms = elapsed_ms(query_start);
    return result;
}
