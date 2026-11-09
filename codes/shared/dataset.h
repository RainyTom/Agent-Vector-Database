#pragma once
// Unified dataset reader.
//
// Every dataset lives in one directory with a fixed flat layout:
//
//   <dataset>/meta.json        optional; dim / n / k / description
//   <dataset>/base.bin         float32, n * dim, object i starts at i*dim
//   <dataset>/edges.tsv        one "child parent" attribute pair per line
//   <dataset>/objects.txt      n lines; line i is the owning attribute of object i
//   <dataset>/queries.bin      float32, m * dim
//   <dataset>/queries.tsv      m lines; "<attribute_count> <attribute>..."
//   <dataset>/gt_10.txt        int32, m * k; negative ids are padding
//
// Same format for every dataset and every scheme: no per-dataset code path.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Dataset {
    int dim = 0;                                  // vector dimension
    int k = 10;                                   // ground-truth depth
    std::vector<float> base;                      // n * dim object vectors
    std::vector<int> owner;                       // object -> directly owned attribute id
    std::unordered_map<int, std::vector<int>> children_of;  // attribute -> child attributes
    std::unordered_map<int, std::vector<int>> parents_of;   // attribute -> parent attributes
    std::vector<std::vector<int>> query_attributes;         // per query: attribute ids
    std::vector<float> query_vectors;                        // m * dim
    std::vector<int32_t> ground_truth;                       // m * k, -1 padding
    std::string path;                                        // directory this was loaded from

    size_t num_objects() const { return dim ? base.size() / size_t(dim) : 0; }
    size_t num_queries() const { return query_attributes.size(); }
    const float* object_vector(size_t object_id) const { return base.data() + object_id * size_t(dim); }
    const float* query_vector(size_t query_id) const { return query_vectors.data() + query_id * size_t(dim); }

    // Loads every file of the layout above; returns false and fills `error` on failure.
    bool load(const std::string& directory, int ground_truth_k, std::string* error);
};

// All attribute ids appearing in edges, objects or queries, in ascending order.
std::vector<int> collect_attribute_ids(const Dataset& dataset);
