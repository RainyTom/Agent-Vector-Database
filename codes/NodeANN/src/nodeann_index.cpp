#include "nodeann_index.h"

#include <chrono>
#include <exception>
#include <filesystem>

namespace {

constexpr int random_seed_base = 100;  // per-attribute seed is 100 + attribute id

std::string index_file_path(const std::string& index_directory, int attribute) {
    return index_directory + "/attribute_" + std::to_string(attribute) + ".bin";
}

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

float NodeANNSpace::counted_distance(const void* left, const void* right, const void* param) {
    const auto* space = static_cast<const NodeANNSpace*>(param);
    ++space->distance_calls;
    return space->RawL2Space::dist(static_cast<const float*>(left), static_cast<const float*>(right));
}

NodeANNIndex::NodeANNIndex(int dimension, int M, int ef_construction)
    : space(static_cast<size_t>(dimension)),
      dimension_(dimension),
      M_(M),
      ef_construction_(ef_construction) {}

NodeANNIndex::~NodeANNIndex() { clear_graphs(); }

void NodeANNIndex::clear_graphs() {
    for (auto& entry : attribute_graphs) delete entry.second;
    attribute_graphs.clear();
}

void NodeANNIndex::rebuild_member_lists(const Dataset& dataset) {
    attribute_members.clear();
    // Objects are visited in ascending id order, so every member list is ascending.
    for (size_t object = 0; object < dataset.owner.size(); ++object)
        attribute_members[dataset.owner[object]].push_back(static_cast<int>(object));
}

double NodeANNIndex::build(const Dataset& dataset, std::string* error) {
    if (dataset.dim != dimension_) {
        if (error) *error = "dataset dimension does not match the index dimension";
        return -1.0;
    }
    clear_graphs();
    rebuild_member_lists(dataset);

    const auto start = std::chrono::steady_clock::now();
    for (int attribute : collect_attribute_ids(dataset)) {
        const auto members = attribute_members.find(attribute);
        if (members == attribute_members.end() || members->second.empty()) continue;
        const std::vector<int>& attribute_objects = members->second;

        auto* graph = new hnswlib::HierarchicalNSW<float>(&space, attribute_objects.size(), size_t(M_),
                                                          size_t(ef_construction_), size_t(random_seed_base + attribute));
        for (size_t label = 0; label < attribute_objects.size(); ++label)
            graph->addPoint(dataset.object_vector(size_t(attribute_objects[label])), label);
        attribute_graphs[attribute] = graph;
    }
    return milliseconds_since(start);
}

bool NodeANNIndex::save(const std::string& index_directory, std::string* error) const {
    std::error_code directory_error;
    std::filesystem::create_directories(index_directory, directory_error);
    if (directory_error) {
        if (error) *error = "cannot create " + index_directory + ": " + directory_error.message();
        return false;
    }
    for (const auto& entry : attribute_graphs) {
        const std::string path = index_file_path(index_directory, entry.first);
        try {
            entry.second->saveIndex(path);
        } catch (const std::exception& failure) {
            if (error) *error = "cannot save " + path + ": " + failure.what();
            return false;
        }
    }
    return true;
}

bool NodeANNIndex::load(const std::string& index_directory, const Dataset& dataset, std::string* error) {
    if (dataset.dim != dimension_) {
        if (error) *error = "dataset dimension does not match the index dimension";
        return false;
    }
    clear_graphs();
    rebuild_member_lists(dataset);

    for (int attribute : collect_attribute_ids(dataset)) {
        const auto members = attribute_members.find(attribute);
        if (members == attribute_members.end() || members->second.empty()) continue;
        const std::string path = index_file_path(index_directory, attribute);
        try {
            attribute_graphs[attribute] = new hnswlib::HierarchicalNSW<float>(
                &space, path, false, members->second.size());
        } catch (const std::exception& failure) {
            if (error) *error = "cannot load " + path + ": " + failure.what();
            clear_graphs();
            return false;
        }
    }
    return true;
}

void NodeANNIndex::set_ef(int ef) {
    for (auto& entry : attribute_graphs) entry.second->setEf(size_t(ef));
}

size_t NodeANNIndex::total_nodes() const {
    size_t nodes = 0;
    for (const auto& entry : attribute_members) nodes += entry.second.size();
    return nodes;
}