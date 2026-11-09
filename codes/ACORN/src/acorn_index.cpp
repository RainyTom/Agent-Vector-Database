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
#include <atomic>
#include <chrono>
#include <thread>
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
    // ACORN collects 2*M*gamma candidates per node before pruning, so the base
    // graph has to be built wide enough for that pool to exist.
    const size_t hnsw_degree = config.hnsw_M ? config.hnsw_M : config.M;
    hnswlib::HierarchicalNSW<float> graph(space.get(), object_count, hnsw_degree,
                                          config.ef_construction, config.seed);
    // hnswlib's addPoint is safe for concurrent insertion (per-node locks), and the
    // official ACORN builder is parallel too (omp). Same thread count as every other
    // scheme in this harness so build time stays comparable.
    const size_t build_threads = std::max<size_t>(1, config.build_threads);
    if (build_threads <= 1) {
        for (size_t object_id = 0; object_id < object_count; ++object_id)
            graph.addPoint(dataset.object_vector(object_id), static_cast<hnswlib::labeltype>(object_id));
    } else {
        std::atomic<size_t> next_object{0};
        std::vector<std::thread> pool;
        pool.reserve(build_threads);
        for (size_t thread = 0; thread < build_threads; ++thread) {
            pool.emplace_back([&]() {
                for (;;) {
                    const size_t object_id = next_object.fetch_add(1);
                    if (object_id >= object_count) return;
                    graph.addPoint(dataset.object_vector(object_id),
                                   static_cast<hnswlib::labeltype>(object_id));
                }
            });
        }
        for (auto& worker : pool) worker.join();
    }
    // ACORN-gamma construction (Section 5.2 of the paper, ported onto hnswlib).
    // The graph is still built by hnswlib so every scheme shares the same engine
    // and the same distance kernel; what ACORN adds is its own pruning rule over
    // the candidate edges, applied here to each node's level-0 list.
    //
    // Official rule (faiss/impl/ACORN.cpp shrink_neighbor_list, L530-590):
    //   * candidates are visited from FARTHEST to nearest;
    //   * the first M_beta candidates are kept unconditionally;
    //   * past that point a candidate is dropped when it already appears in
    //     neigh_of_neigh; otherwise it is kept and both the candidate itself and
    //     all of its neighbours join neigh_of_neigh;
    //   * the walk stops once |neigh_of_neigh| >= max_size or max_size edges are
    //     kept, where max_size = nb_neighbors(0) = M.
    // Level 0 capacity follows the paper: M_beta + 1.5*M (ACORN.cpp L167).
    if (config.gamma >= 1) {
        const auto prune_start = std::chrono::steady_clock::now();
        // official level-0 capacity: M_beta + 1.5*M (ACORN.cpp L167) and the prune
        // target is that same nb_neighbors(0), not M
        const size_t capacity = std::min<size_t>(graph.maxM0_,
            size_t(double(config.M_beta) + 1.5 * double(config.M)));
        const size_t max_size = capacity;
        const size_t M_beta = std::min(config.M_beta, max_size);
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
        std::unordered_set<hnswlib::tableint> neigh_of_neigh;
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
            // farthest first, matching the official priority queue
            std::sort(ranked.begin(), ranked.end(), std::greater<>());
            kept.clear();
            neigh_of_neigh.clear();
            size_t examined = 0;
            for (const auto& entry : ranked) {
                ++examined;
                const hnswlib::tableint candidate = entry.second;
                if (examined > M_beta && neigh_of_neigh.count(candidate)) continue;
                kept.push_back(candidate);
                if (kept.size() >= max_size) break;
                neigh_of_neigh.insert(candidate);
                if (examined > M_beta) {
                    for (hnswlib::tableint second : original[size_t(candidate)])
                        neigh_of_neigh.insert(second);
                }
                if (neigh_of_neigh.size() >= max_size) break;
            }
            hnswlib::linklistsizeint* list = graph.get_linklist0(id);
            *list = static_cast<hnswlib::linklistsizeint>(kept.size());
            std::memcpy(list + 1, kept.data(), kept.size() * sizeof(hnswlib::tableint));
        }
        std::fprintf(stderr, "[acorn] hnsw base %.1fs, acorn pruning %.1fs (max_size %zu, M_beta %zu)\n",
                     elapsed_ms(build_start) / 1000.0 - elapsed_ms(prune_start) / 1000.0,
                     elapsed_ms(prune_start) / 1000.0, max_size, M_beta);
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