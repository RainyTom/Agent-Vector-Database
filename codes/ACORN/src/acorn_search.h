#pragma once
// Predicate construction and ACORN-1 style level-0 traversal.
//
// Predicate semantics: a query attribute set U selects every object whose
// owning attribute lies in the OR-descendant closure of U. The closure is
// materialised once per query as a bitmap over dense attribute ids.
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "acorn_index.h"
#include "dataset.h"

class AcornPredicateSpace {
public:
    void build(const Dataset& dataset);

    int attribute_count() const { return attribute_count_; }

    // Fills `bits` with the OR-descendant closure of `query_attributes` and
    // `closed_attributes` with the dense ids that were set (ascending-ish).
    void closure_bitmap(const Dataset& dataset, const std::vector<int>& query_attributes,
                        std::vector<uint64_t>* bits, std::vector<int>* closed_attributes) const;

    bool passes(size_t object_id, const std::vector<uint64_t>& bits) const {
        const int attribute = owner_attribute_dense_[object_id];
        if (attribute < 0) return false;
        return (bits[static_cast<size_t>(attribute) >> 6] >> (attribute & 63)) & 1u;
    }

    // |E(U)| from per-attribute object counts; exact because each object has one owner.
    size_t eligible_count(const std::vector<int>& closed_attributes) const;

    // |E(U)| by scanning every object; used only to cross-check the counting path.
    size_t exact_eligible_count(const Dataset& dataset, const std::vector<uint64_t>& bits) const;

private:
    std::unordered_map<int, int> dense_of_attribute_;
    std::vector<int> owner_attribute_dense_;   // per object, -1 if its owner was not mapped
    std::vector<size_t> attribute_member_count_;
    int attribute_count_ = 0;
};

// Reusable per-process buffers so queries do not reallocate.
struct AcornSearchScratch {
    std::vector<uint8_t> visited;                            // by internal id
    std::vector<std::pair<float, hnswlib::tableint>> beam;   // min-heap by distance
    std::vector<std::pair<float, int>> best;                 // max-heap by distance, passing ids only
};

struct AcornQueryResult {
    std::vector<int> object_ids;             // up to k, ascending distance
    unsigned long long visited_nodes = 0;    // distinct nodes distance-evaluated
    unsigned long long distance_calls = 0;
    double query_ms = 0.0;
};

// one_query: beam search from the entry point; non-passing nodes may be
// traversed (one hop) but never enter the result heap; two-hop expansion of
// the current best node runs while the passing-result heap is below ef.
// Faithful ACORN-gamma level-0 search (Algorithm 2 of the paper):
//   * the candidate set and the result list contain ONLY predicate-passing
//     nodes, so the search never wanders through the wrong predicate region;
//   * the neighbourhood of a visited node is recovered by scanning the first
//     M_beta stored neighbours, then expanding the remaining ones to their own
//     neighbours (the compression heuristic the construction relies on);
//   * termination follows HNSW: stop once the nearest candidate is farther than
//     the ef-th best passing result.
AcornQueryResult acorn_search_query(const AcornIndex& index,
                                    const AcornPredicateSpace& predicate_space,
                                    const std::vector<uint64_t>& predicate_bits,
                                    const float* query_vector, size_t ef, int k,
                                    size_t M_beta, size_t M,
                                    AcornSearchScratch* scratch);