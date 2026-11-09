#pragma once
// ACORN construction and storage implemented on hnswlib's primitives.
//
// The graph is built by ACORN's own insertion procedure (candidate collection of
// 2*M*gamma edges, then ACORN's predicate-agnostic pruning), but every distance
// goes through hnswlib's pinned L2Space, and the vectors live in the same
// contiguous layout hnswlib uses. This keeps the engine and the distance kernel
// identical to the other schemes while reproducing ACORN's mechanism exactly.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dataset.h"
#include "raw_l2_space.h"

struct AcornBuildConfig {
    size_t M = 16;
    size_t ef_construction = 200;
    size_t M_beta = 8;
    size_t gamma = 2;
    unsigned seed = 100;
    size_t build_threads = 1;   // accepted for API symmetry; construction is sequential
};

struct AcornBuildStats {
    double build_s = 0.0;
    size_t object_count = 0;
    int dimension = 0;
};

class AcornIndex {
public:
    AcornIndex() = default;

    bool build(const Dataset& dataset, const AcornBuildConfig& config,
               AcornBuildStats* stats, std::string* error);
    bool save(const std::string& path, std::string* error) const;
    bool load(const std::string& path, int dimension, std::string* error);
    // vectors are not stored in the index file; re-attach them after load()
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

    const uint32_t* neighbors_at_level(uint32_t id, int level, size_t* count) const {
        const std::vector<uint32_t>& list = adjacency_[size_t(id)][size_t(level)];
        *count = list.size();
        return list.data();
    }
    const uint32_t* neighbors_of(uint32_t id, size_t* count) const {
        return neighbors_at_level(id, 0, count);
    }
    int object_id_of(uint32_t id) const { return int(id); }

    // exposed for the build statistics
    size_t level0_capacity() const { return level0_capacity_; }
    size_t upper_capacity() const { return upper_capacity_; }

private:
    std::vector<std::vector<std::vector<uint32_t>>> adjacency_;  // node -> level -> neighbours
    std::vector<int> level_;
    const float* base_ = nullptr;      // borrowed from the dataset during build/search
    std::vector<float> owned_vectors_;  // used after load(), when the dataset is gone
    std::unique_ptr<RawL2Space> space_;
    uint32_t entry_point_ = 0;
    int max_level_ = 0;
    int dimension_ = 0;
    size_t object_count_ = 0;
    size_t level0_capacity_ = 0;
    size_t upper_capacity_ = 0;
};
