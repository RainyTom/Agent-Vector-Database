#pragma once
// One global HNSW over every object of the unified dataset (no per-attribute
// graphs). This translation unit owns index construction and index loading;
// predicate-aware traversal lives in acorn_search.cpp.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "hnswlib/hnswlib.h"
#include "raw_l2_space.h"

struct AcornBuildConfig {
    size_t M = 16;
    size_t ef_construction = 200;
    size_t M_beta = 8;      // neighbours kept verbatim before pruning (ACORN's M_beta)
    size_t gamma = 2;       // neighbour-list expansion factor
    unsigned seed = 100;
    size_t build_threads = 1;
};

struct AcornBuildStats {
    double build_ms = 0.0;
    size_t object_count = 0;
    int dimension = 0;
};

// Builds the graph in ascending object-id order and writes it to `index_path`.
bool acorn_build_index(const std::string& data_directory, const std::string& index_path,
                       const AcornBuildConfig& config, AcornBuildStats* stats, std::string* error);

// A saved HNSW index reloaded for search, exposing the level-0 traversal
// surface ACORN traversal needs: vectors, neighbours, entry point, labels.
class AcornIndex {
public:
    AcornIndex();
    ~AcornIndex();
    AcornIndex(const AcornIndex&) = delete;
    AcornIndex& operator=(const AcornIndex&) = delete;

    bool load(const std::string& index_path, int dimension, std::string* error);

    size_t object_count() const { return object_count_; }
    int dimension() const { return dimension_; }
    float distance(const float* first, const float* second) const {
        return space_->dist(first, second);
    }
    const float* vector_of(hnswlib::tableint internal_id) const {
        return reinterpret_cast<const float*>(graph_->getDataByInternalId(internal_id));
    }
    hnswlib::tableint entry_point() const { return graph_->enterpoint_node_; }
    int max_level() const { return graph_->maxlevel_; }
    int level_of(hnswlib::tableint internal_id) const { return graph_->element_levels_[internal_id]; }

    // Neighbours of a node at one layer; `count` receives the edge count.
    const hnswlib::tableint* neighbors_at_level(hnswlib::tableint internal_id, int level,
                                                size_t* count) const {
        const hnswlib::linklistsizeint* list = graph_->get_linklist_at_level(internal_id, level);
        *count = graph_->getListCount(const_cast<hnswlib::linklistsizeint*>(list));
        return reinterpret_cast<const hnswlib::tableint*>(list + 1);
    }

    // Level-0 neighbours of a node.
    const hnswlib::tableint* neighbors_of(hnswlib::tableint internal_id, size_t* count) const {
        return neighbors_at_level(internal_id, 0, count);
    }
    size_t level0_capacity() const { return graph_->maxM0_; }
    hnswlib::linklistsizeint* level0_list(hnswlib::tableint internal_id) const {
        return graph_->get_linklist0(internal_id);
    }

    // Object id recorded as the node's external label at build time.
    int object_id_of(hnswlib::tableint internal_id) const {
        return static_cast<int>(graph_->getExternalLabel(internal_id));
    }

private:
    std::unique_ptr<RawL2Space> space_;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> graph_;
    int dimension_ = 0;
    size_t object_count_ = 0;
};