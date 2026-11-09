// NOTE (audit 2026-11-09): this file is NOT the official ACORN algorithm and must
// not be used as an ACORN baseline. It differs from guestrin-lab/ACORN in four
// places: the result heap is sized by ef instead of k, the stopping rule is
// "candidate worse than the ef-th result" instead of faiss's count_below rule,
// there is no num_found >= 2M cap on two-hop expansion, and the build prunes
// hnswlib's stored list instead of collecting 2*M*gamma candidates and pruning
// farthest-first with early termination. The baseline has to come from the
// official faiss fork.
#include "acorn_index.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <unordered_set>
#include <vector>

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
    // ACORN-gamma construction (Section 5.2 of the paper). Candidate edges are
    // already collected during insertion: hnswlib keeps up to maxM0 = 2M edges at
    // level 0, which is M*gamma for gamma = 2. What ACORN adds on top is its
    // predicate-agnostic pruning, applied here to that candidate list:
    //   * keep the nearest M_beta candidates verbatim,
    //   * walk the rest in distance order and drop any candidate already covered
    //     as a two-hop neighbour of an already kept candidate,
    //   * stop once |kept| + |covered| reaches M*gamma.
    // The pruned neighbours stay recoverable at search time because the search
    // expands the neighbours past index M_beta to their own lists.
    if (config.gamma > 1) {
        const auto prune_start = std::chrono::steady_clock::now();
        const size_t capacity = graph.maxM0_;
        const size_t target = std::min(capacity, config.M * config.gamma);
        const size_t M_beta = std::min(config.M_beta, target);
        std::vector<std::vector<hnswlib::tableint>> original(object_count);
        for (size_t node = 0; node < object_count; ++node) {
            const auto id = static_cast<hnswlib::tableint>(node);
            const hnswlib::linklistsizeint* list = graph.get_linklist0(id);
            const size_t count = graph.getListCount(const_cast<hnswlib::linklistsizeint*>(list));
            const hnswlib::tableint* neighbours = reinterpret_cast<const hnswlib::tableint*>(list + 1);
            original[node].assign(neighbours, neighbours + count);
        }
        std::vector<std::pair<float, hnswlib::tableint>> ranked;
        std::vector<hnswlib::tableint> kept;
        std::unordered_set<hnswlib::tableint> covered;
        for (size_t node = 0; node < object_count; ++node) {
            const auto id = static_cast<hnswlib::tableint>(node);
            const std::vector<hnswlib::tableint>& candidates = original[node];
            if (candidates.empty()) continue;
            const float* base = dataset.object_vector(node);
            ranked.clear();
            ranked.reserve(candidates.size());
            for (hnswlib::tableint candidate : candidates)
                ranked.emplace_back(space->dist(base, dataset.object_vector(size_t(candidate))),
                                    candidate);
            std::sort(ranked.begin(), ranked.end());
            kept.clear();
            kept.reserve(target);
            for (size_t i = 0; i < ranked.size() && kept.size() < M_beta; ++i)
                kept.push_back(ranked[i].second);
            covered.clear();
            for (size_t i = M_beta; i < ranked.size(); ++i) {
                const hnswlib::tableint candidate = ranked[i].second;
                if (covered.count(candidate)) continue;
                kept.push_back(candidate);
                for (hnswlib::tableint second : original[size_t(candidate)])
                    covered.insert(second);
                if (kept.size() + covered.size() >= target) break;
            }
            hnswlib::linklistsizeint* list = graph.get_linklist0(id);
            *list = static_cast<hnswlib::linklistsizeint>(kept.size());
            std::memcpy(list + 1, kept.data(), kept.size() * sizeof(hnswlib::tableint));
        }
        std::fprintf(stderr, "[acorn] hnsw base %.1fs, acorn pruning %.1fs\n",
                     elapsed_ms(build_start) / 1000.0 - elapsed_ms(prune_start) / 1000.0,
                     elapsed_ms(prune_start) / 1000.0);
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