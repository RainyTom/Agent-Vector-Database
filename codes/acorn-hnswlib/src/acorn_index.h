#pragma once
// ACORN construction and storage implemented on hnswlib's primitives.
//
// The graph is built by ACORN's own insertion procedure (candidate collection of
// 2*M*gamma edges, then ACORN's predicate-agnostic pruning), but every distance
// goes through hnswlib's pinned L2Space.
//
// Storage: level 0 is a flat CSR (offset array + concatenated targets), which is
// what the search touches on every expansion. Levels >= 1 cover only ~6% of the
// nodes, so they keep a per-node vector and are traversed once per query during
// the entry descent.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dataset.h"
#include "raw_l2_space.h"

struct AcornBuildConfig {
    size_t M = 32;
    size_t ef_construction = 40;
    size_t M_beta = 64;
    size_t gamma = 12;
    unsigned seed = 100;
    size_t build_threads = 1;   // accepted for API symmetry; construction is sequential
};

struct AcornBuildStats {
    double build_s = 0.0;
    size_t object_count = 0;
    int dimension = 0;
    double level0_degree = 0.0;
};

class AcornIndex {
public:
    AcornIndex() = default;

    bool build(const Dataset& dataset, const AcornBuildConfig& config,
               AcornBuildStats* stats, std::string* error);
    bool save(const std::string& path, std::string* error) const;
    bool load(const std::string& path, int dimension, std::string* error);
    void attach_vectors(const Dataset& dataset) {
        base_ = dataset.base.data();
        dimension_ = dataset.dim;
        space_ = std::make_unique<RawL2Space>(size_t(dimension_));
    }

    size_t object_count() const { return object_count_; }
    int dimension() const { return dimension_; }

    float distance(const float* a, const float* b) const { return space_->dist(a, b); }
    const float* vector_of(uint32_t id) const {
        return base_ + size_t(id) * size_t(dimension_);
    }
    uint32_t entry_point() const { return entry_point_; }
    int max_level() const { return max_level_; }
    int level_of(uint32_t id) const { return level_[size_t(id)]; }

    // Level 0 is CSR: neighbours of `id` live in one contiguous run.
    const uint32_t* neighbors_of(uint32_t id, size_t* count) const {
        *count = size_t(level0_offset_[size_t(id) + 1] - level0_offset_[size_t(id)]);
        return level0_target_.data() + level0_offset_[size_t(id)];
    }
    const uint32_t* neighbors_at_level(uint32_t id, int level, size_t* count) const {
        if (level == 0) return neighbors_of(id, count);
        const std::vector<std::vector<uint32_t>>& per_node = upper_adjacency_[size_t(id)];
        const size_t slot = size_t(level - 1);
        if (slot >= per_node.size()) { *count = 0; return nullptr; }
        *count = per_node[slot].size();
        return per_node[slot].data();
    }
    int object_id_of(uint32_t id) const { return int(id); }

    size_t level0_capacity() const { return level0_capacity_; }
    size_t upper_capacity() const { return upper_capacity_; }
    size_t level0_edge_count() const { return level0_target_.size(); }

private:
    // level 0, flattened
    std::vector<uint32_t> level0_offset_;   // size n + 1
    std::vector<uint32_t> level0_target_;
    // levels >= 1, sparse
    std::vector<std::vector<std::vector<uint32_t>>> upper_adjacency_;   // node -> (level-1) -> neighbours

    std::vector<int> level_;
    const float* base_ = nullptr;
    std::unique_ptr<RawL2Space> space_;
    uint32_t entry_point_ = 0;
    int max_level_ = 0;
    int dimension_ = 0;
    size_t object_count_ = 0;
    size_t level0_capacity_ = 0;
    size_t upper_capacity_ = 0;
};
