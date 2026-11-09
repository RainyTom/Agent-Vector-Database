#include "acorn_index.h"

#include <chrono>
#include <exception>

#include "dataset.h"

namespace {

double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

bool acorn_build_index(const std::string& data_directory, const std::string& index_path,
                       const AcornBuildConfig& config, AcornBuildStats* stats, std::string* error) {
    Dataset dataset;
    if (!dataset.load(data_directory, 10, error)) return false;

    const size_t object_count = dataset.num_objects();
    if (object_count == 0) {
        *error = data_directory + ": dataset has no objects";
        return false;
    }

    auto space = std::make_unique<RawL2Space>(static_cast<size_t>(dataset.dim));
    const auto build_start = std::chrono::steady_clock::now();
    hnswlib::HierarchicalNSW<float> graph(space.get(), object_count, config.M,
                                          config.ef_construction, config.seed);
    for (size_t object_id = 0; object_id < object_count; ++object_id) {
        graph.addPoint(dataset.object_vector(object_id), static_cast<hnswlib::labeltype>(object_id));
    }
    if (stats) {
        stats->build_ms = elapsed_ms(build_start);
        stats->object_count = object_count;
        stats->dimension = dataset.dim;
    }
    graph.saveIndex(index_path);
    return true;
}

AcornIndex::AcornIndex() = default;
AcornIndex::~AcornIndex() = default;

bool AcornIndex::load(const std::string& index_path, int dimension, std::string* error) {
    dimension_ = dimension;
    space_ = std::make_unique<RawL2Space>(static_cast<size_t>(dimension));
    try {
        graph_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(space_.get(), index_path, false, 0);
    } catch (const std::exception& failure) {
        *error = index_path + ": " + failure.what();
        graph_.reset();
        space_.reset();
        return false;
    }
    if (graph_->data_size_ != space_->get_data_size()) {
        *error = index_path + ": stored vector size does not match dimension " + std::to_string(dimension);
        graph_.reset();
        space_.reset();
        return false;
    }
    object_count_ = static_cast<size_t>(graph_->cur_element_count.load());
    return true;
}