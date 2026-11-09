#pragma once
// Attribute-skeleton index: data structure, build, save and load.
//
// Layout summary
//   * every object belongs to exactly one attribute; global node id = the
//     position of the object inside its attribute's block (see node_begin)
//   * local_edges   : within-attribute HNSW layer-0 adjacency + representative MST
//   * bridge_edges  : frozen cross-attribute edges (gateway nodes <-> parent rep)
//   * upper_layers  : per-attribute HNSW layers above 0, used only to pick an entry
//   * closure_bits  : descendant bitmap per attribute, over attribute indices
//
// Build and query are separate: build_index() + save_index() run once, then
// load_index() + run_queries() serve many query grids.

#include <cstdint>
#include <string>
#include <vector>

#include "dataset.h"

struct BuildOptions {
    int max_degree = 16;               // hnswlib M
    int construction_width = 200;      // hnswlib efConstruction
    int representative_cap = 8;        // upper bound on representatives per attribute
    int representative_rule = 0;       // 0 = min(Kcap, n); 1 = round(sqrt(n)); 2 = clustered merge
    std::string merge_structure_path;  // precomputed merge skeleton (Python + faiss k-means)
    int entries_per_attribute = 8;     // stored entry points per attribute
    int bridge_receivers = 1;          // how many scattered child members each bridge attaches to
    bool merge_attributes = true;      // add cross-attribute gateway edges
};

struct AsiIndex {
    int attribute_count = 0;
    int closure_words = 0;
    size_t object_count = 0;
    size_t upper_layer_bytes = 0;

    std::vector<int> attribute_ids;          // index -> external attribute id
    std::vector<uint32_t> node_begin;        // attribute -> first global node
    std::vector<uint32_t> node_attribute;    // global node -> attribute index
    std::vector<uint32_t> node_object;       // global node -> object id
    std::vector<uint32_t> object_node;       // object id -> global node

    std::vector<uint32_t> local_edge_begin;  // CSR offset into local_edge_targets (size n+1)
    std::vector<uint32_t> local_edge_targets;
    std::vector<uint32_t> bridge_edge_begin;
    std::vector<uint32_t> bridge_edge_targets;

    std::vector<uint64_t> closure_bits;      // attribute_count * closure_words
    std::vector<std::vector<uint32_t>> representative_nodes;  // per attribute, local ids
    std::vector<uint32_t> entry_node;        // per attribute, local id
    std::vector<uint32_t> hnsw_entry_node;   // per attribute, local id of the top layer entry
    std::vector<int> top_level;              // per attribute
    std::vector<std::vector<uint32_t>> upper_row_begin;   // per attribute, local node -> row
    std::vector<std::vector<uint32_t>> upper_row_offset;  // per attribute, row -> target start
    std::vector<std::vector<uint32_t>> upper_targets;     // per attribute

    std::vector<uint32_t> skeleton_top;      // global nodes of the outermost merge level
    std::vector<std::vector<uint32_t>> attribute_entries;  // inverted index: attribute -> entry nodes

    std::vector<char> is_representative;
    std::vector<char> is_gateway;

    size_t num_objects() const { return object_count; }
};

// Builds the index from a loaded dataset. Prints progress to stderr.
AsiIndex build_index(const Dataset& dataset, const BuildOptions& options);

bool save_index(const AsiIndex& index, const std::string& path, std::string* error);
bool load_index(AsiIndex& index, const std::string& path, std::string* error);
