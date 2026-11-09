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

    // hoist the predicate so the per-neighbour loop is a plain array read
    const char* mask = predicate_space.external_mask();
    const size_t mask_size = predicate_space.external_mask_size();
    auto passes_fast = [&](uint32_t id) -> bool {
        if (mask) return id < mask_size && mask[id] != 0;
        return predicate_space.passes(size_t(id), predicate_bits);
    };

    std::vector<uint8_t>& visited = scratch->visited;
    if (visited.size() != index.object_count()) visited.assign(index.object_count(), 0);
    else std::memset(visited.data(), 0, visited.size());

    // Faithful clone of faiss ACORN::MinimaxHeap (ACORN.cpp L1629-1690):
    //   * fixed capacity ef holding the ef smallest distances seen;
    //   * push rejects when the new distance is not smaller than the current max;
    //   * pop_min marks the entry dead but KEEPS its distance, and count_below
    //     therefore grows with the number of pops -- that is what stops the search
    //     after roughly ef expansions.
    struct Minimax {
        // faiss ACORN::MinimaxHeap: a max-heap over the ef smallest distances seen
        // plus lazy deletion (popped entries keep their distance so count_below
        // keeps growing). push/pop are O(log k), matching the reference cost.
        int capacity = 0;
        std::vector<float> dis;
        std::vector<int> ids;
        int k = 0;        // slots in the heap
        int nvalid = 0;   // live entries
        explicit Minimax(int n) : capacity(n), dis(size_t(n)), ids(size_t(n), 0) {}

        void sift_up(int index) {
            while (index > 0) {
                const int parent = (index - 1) / 2;
                if (dis[size_t(parent)] >= dis[size_t(index)]) break;
                std::swap(dis[size_t(parent)], dis[size_t(index)]);
                std::swap(ids[size_t(parent)], ids[size_t(index)]);
                index = parent;
            }
        }
        void sift_down(int index) {
            for (;;) {
                const int left = 2 * index + 1;
                const int right = left + 1;
                int largest = index;
                if (left < k && dis[size_t(left)] > dis[size_t(largest)]) largest = left;
                if (right < k && dis[size_t(right)] > dis[size_t(largest)]) largest = right;
                if (largest == index) break;
                std::swap(dis[size_t(index)], dis[size_t(largest)]);
                std::swap(ids[size_t(index)], ids[size_t(largest)]);
                index = largest;
            }
        }
        void push(float distance, int id) {
            if (k == capacity) {
                if (distance >= dis[0]) return;
                // replace the max (root) in place
                dis[0] = distance;
                ids[0] = id;
                sift_down(0);
                return;
            }
            dis[size_t(k)] = distance;
            ids[size_t(k)] = id;
            ++k;
            ++nvalid;
            sift_up(k - 1);
        }
        int pop_min(float* out) {
            int best = -1;
            for (int i = 0; i < k; ++i)
                if (ids[size_t(i)] >= 0 && (best < 0 || dis[size_t(i)] < dis[size_t(best)])) best = i;
            if (best < 0) return -1;
            *out = dis[size_t(best)];
            const int id = ids[size_t(best)];
            ids[size_t(best)] = -1;   // lazy delete: distance stays for count_below
            --nvalid;
            return id;
        }
        int count_below(float threshold) const {
            int below = 0;
            for (int i = 0; i < k; ++i)
                if (dis[size_t(i)] < threshold) ++below;
            return below;
        }
        int size() const { return nvalid; }
    } candidates{int(ef)};
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
    candidates.push(entry_distance, int(entry));

    auto consider = [&](hnswlib::tableint node, float distance) {
        results.offer(distance, index.object_id_of(node));
        candidates.push(distance, int(node));
    };
    consider(entry, entry_distance);

    while (candidates.size() > 0) {
        float current_distance = 0.0f;
        const int popped = candidates.pop_min(&current_distance);
        if (popped < 0) break;
        const hnswlib::tableint current = hnswlib::tableint(popped);

        // count_below(d0) >= efSearch -> stop (dead entries still count)
        if (candidates.count_below(current_distance) >= int(ef)) break;

        size_t degree = 0;
        const hnswlib::tableint* neighbours = index.neighbors_of(current, &degree);
        size_t num_found = 0;
        bool keep_expanding = true;
        for (size_t j = 0; j < degree; ++j) {
            const hnswlib::tableint neighbour = neighbours[j];
            const bool passing = passes_fast(neighbour);
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
                    if (!passes_fast(reached)) continue;
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
