#pragma once
// NodeANN index: one HNSW graph per non-empty attribute, built over that
// attribute's object ids in ascending object-id order.
//
// Graph label j of attribute a is the j-th object of `attribute_members[a]`,
// so a label is translated back to a dataset object id by indexing that list.
// The member lists are derived from dataset.owner in both build and load, so
// the label mapping survives a save/load round trip without extra metadata.

#include <string>
#include <unordered_map>
#include <vector>

#include "dataset.h"
#include "hnswlib/hnswlib.h"
#include "raw_l2_space.h"

// Squared-L2 space that counts every distance evaluation. Every attribute graph
// shares one space object, so search cost per query is one counter delta.
class NodeANNSpace : public RawL2Space {
public:
    explicit NodeANNSpace(size_t dimension) : RawL2Space(dimension) {}

    float dist(const float* left, const float* right) const {
        ++distance_calls;
        return RawL2Space::dist(left, right);
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted_distance; }
    void* get_dist_func_param() override { return this; }

    mutable unsigned long long distance_calls = 0;

private:
    static float counted_distance(const void* left, const void* right, const void* param);
};

class NodeANNIndex {
public:
    static constexpr int default_M = 16;
    static constexpr int default_ef_construction = 200;

    NodeANNIndex(int dimension, int M, int ef_construction);
    ~NodeANNIndex();

    NodeANNIndex(const NodeANNIndex&) = delete;
    NodeANNIndex& operator=(const NodeANNIndex&) = delete;

    // Builds a graph for every non-empty attribute of `dataset`.
    // Returns elapsed build time in milliseconds.
    double build(const Dataset& dataset, std::string* error);

    // Writes one index file per attribute: <index_directory>/attribute_<id>.bin
    bool save(const std::string& index_directory, std::string* error) const;

    // Restores every attribute graph from `index_directory`; member lists are
    // rebuilt from the dataset, which must be the one the index was built on.
    bool load(const std::string& index_directory, const Dataset& dataset, std::string* error);

    void set_ef(int ef);
    size_t total_nodes() const;
    size_t attribute_count() const { return attribute_graphs.size(); }

    int dimension() const { return dimension_; }

    NodeANNSpace space;
    std::unordered_map<int, std::vector<int>> attribute_members;  // ascending object ids
    std::unordered_map<int, hnswlib::HierarchicalNSW<float>*> attribute_graphs;

private:
    void clear_graphs();
    void rebuild_member_lists(const Dataset& dataset);

    int dimension_;
    int M_;
    int ef_construction_;
};