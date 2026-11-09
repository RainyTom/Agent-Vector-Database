// Drive the official ACORN (guestrin-lab/ACORN, Faiss fork) on the unified
// dataset layout and the fixed-selectivity slices, at the same k, ef and query
// set as the acorn-hnswlib reproduction and the codes/ACORN baseline.
//
// Build (official tree cloned separately, e.g. /tmp/ACORN-official):
//   cmake -DFAISS_ENABLE_GPU=OFF -DFAISS_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF \
//         -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release -B build . && make -C build -j faiss
//   g++ -std=c++17 -O3 -fopenmp -I <official>/faiss -I codes/shared \
//       codes/ACORN-official/selectivity_driver.cpp codes/shared/dataset.cpp \
//       -L <official>/build/faiss -lfaiss -Wl,-rpath,<official>/build/faiss \
//       -o codes/ACORN-official/build/selectivity_driver
//
// One index is built per (M, gamma, M_beta) over the base vectors of the first
// slice, persisted with faiss::write_index, and reused by later invocations.
#include <faiss/IndexACORN.h>
#include <faiss/index_io.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dataset.h"

namespace faiss {
extern ACORNStats acorn_stats;
}

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// owner -> object ids by a dense attribute id; query attributes are closed over
// the hierarchy, and because every object has exactly one owner the closures
// give disjoint sets, so |E(U)| is exact.
struct MaskBuilder {
    std::unordered_map<int, int> dense_of;
    std::vector<std::vector<int32_t>> members;
    std::vector<int32_t> dense_owner;

    explicit MaskBuilder(const Dataset& dataset) : dense_owner(dataset.num_objects(), -1) {
        for (size_t id = 0; id < dataset.num_objects(); ++id) {
            const int attribute = dataset.owner[id];
            auto found = dense_of.find(attribute);
            if (found == dense_of.end()) {
                found = dense_of.emplace(attribute, static_cast<int>(members.size())).first;
                members.emplace_back();
            }
            dense_owner[id] = found->second;
            members[static_cast<size_t>(found->second)].push_back(static_cast<int32_t>(id));
        }
    }

    std::vector<size_t> fill(const Dataset& dataset, size_t query_count, std::vector<char>* map) const {
        std::vector<size_t> eligible(query_count, 0);
        std::vector<int> stack, closed;
        std::vector<uint8_t> seen(members.size(), 0);
        map->assign(query_count * dataset.num_objects(), 0);
        for (size_t qi = 0; qi < query_count; ++qi) {
            std::fill(seen.begin(), seen.end(), 0);
            closed.clear();
            stack = dataset.query_attributes[qi];
            while (!stack.empty()) {
                const int attribute = stack.back();
                stack.pop_back();
                const auto found = dense_of.find(attribute);
                if (found == dense_of.end() || seen[static_cast<size_t>(found->second)]) continue;
                const int dense = found->second;
                seen[static_cast<size_t>(dense)] = 1;
                closed.push_back(dense);
                const auto children = dataset.children_of.find(attribute);
                if (children != dataset.children_of.end())
                    for (int child : children->second) stack.push_back(child);
            }
            char* row = map->data() + qi * dataset.num_objects();
            size_t count = 0;
            for (int dense : closed)
                for (auto id : members[static_cast<size_t>(dense)]) {
                    row[id] = 1;
                    ++count;
                }
            eligible[qi] = count;
        }
        return eligible;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string index_path;
    std::vector<std::string> slices;
    int gamma = 12;
    int ef = 60;
    int queries = 100;
    int M = 32;
    int M_beta = -1;
    bool build_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        auto next = [&](std::string* value) {
            if (i + 1 >= argc) return false;
            *value = argv[++i];
            return true;
        };
        std::string value;
        if (flag == "--index") { if (!next(&index_path)) return 2; }
        else if (flag == "--slice") { if (!next(&value)) return 2; slices.push_back(value); }
        else if (flag == "--gamma") { if (!next(&value)) return 2; gamma = std::atoi(value.c_str()); }
        else if (flag == "--ef") { if (!next(&value)) return 2; ef = std::atoi(value.c_str()); }
        else if (flag == "--queries") { if (!next(&value)) return 2; queries = std::atoi(value.c_str()); }
        else if (flag == "--M") { if (!next(&value)) return 2; M = std::atoi(value.c_str()); }
        else if (flag == "--Mbeta") { if (!next(&value)) return 2; M_beta = std::atoi(value.c_str()); }
        else if (flag == "--build-only") { build_only = true; }
        else { std::fprintf(stderr, "unknown flag: %s\n", flag.c_str()); return 2; }
    }
    if (index_path.empty() || slices.empty()) {
        std::fprintf(stderr, "usage: selectivity_driver --index PATH --slice DIR [--slice DIR...] "
                             "[--gamma 12|1] [--ef 60] [--queries 100] [--M 32] [--Mbeta N] [--build-only]\n");
        return 2;
    }
    if (M_beta < 0) M_beta = gamma == 12 ? M : 2 * M;   // README: gamma=12 -> M_beta=32; gamma=1 -> 2M
    const int k = 20;

    std::unique_ptr<faiss::IndexACORNFlat> index;
    double build_s = 0.0;
    FILE* probe = std::fopen(index_path.c_str(), "rb");
    if (probe) {
        std::fclose(probe);
        index.reset(dynamic_cast<faiss::IndexACORNFlat*>(faiss::read_index(index_path.c_str())));
        if (!index) {
            std::fprintf(stderr, "%s: not an ACORN index\n", index_path.c_str());
            return 2;
        }
        std::printf("{\"impl\":\"acorn-official\",\"action\":\"load\",\"gamma\":%d,\"M\":%d,\"Mbeta\":%d,"
                    "\"objects\":%ld}\n", gamma, M, M_beta, static_cast<long>(index->ntotal));
    } else {
        Dataset first;
        std::string error;
        if (!first.load(slices.front(), k, &error)) {
            std::fprintf(stderr, "dataset load failed: %s\n", error.c_str());
            return 2;
        }
        std::vector<int> metadata(first.owner.begin(), first.owner.end());
        index = std::make_unique<faiss::IndexACORNFlat>(first.dim, M, gamma, metadata, M_beta);
        const auto start = Clock::now();
        index->add(first.num_objects(), first.base.data());
        build_s = ms_since(start) / 1000.0;
        faiss::write_index(index.get(), index_path.c_str());
        std::printf("{\"impl\":\"acorn-official\",\"action\":\"build\",\"gamma\":%d,\"M\":%d,\"Mbeta\":%d,"
                    "\"objects\":%ld,\"build_s\":%.1f}\n",
                    gamma, M, M_beta, static_cast<long>(index->ntotal), build_s);
        std::fflush(stdout);
    }
    if (build_only) return 0;

    index->acorn.efSearch = ef;
    std::vector<char> filter_map;
    std::vector<faiss::idx_t> ids;
    std::vector<float> distances;
    for (const auto& slice : slices) {
        Dataset dataset;
        std::string error;
        if (!dataset.load(slice, k, &error)) {
            std::fprintf(stderr, "dataset load failed: %s\n", error.c_str());
            return 2;
        }
        if (dataset.num_objects() != static_cast<size_t>(index->ntotal)) {
            std::fprintf(stderr, "%s: %zu objects but index holds %ld\n",
                         slice.c_str(), dataset.num_objects(), static_cast<long>(index->ntotal));
            return 2;
        }
        const size_t query_count = std::min<size_t>(static_cast<size_t>(queries), dataset.num_queries());
        const MaskBuilder builder(dataset);
        const std::vector<size_t> eligible = builder.fill(dataset, query_count, &filter_map);
        ids.assign(query_count * k, -1);
        distances.assign(query_count * k, 0.0f);

        // This fork accumulates search distance evaluations in stats.n3 (ndis stays 0).
        const size_t before = faiss::acorn_stats.n3;
        const auto start = Clock::now();
        index->search(static_cast<faiss::idx_t>(query_count), dataset.query_vectors.data(), k,
                      distances.data(), ids.data(), filter_map.data());
        const double pass_ms = ms_since(start);
        const size_t distance_calls = faiss::acorn_stats.n3 - before;

        const size_t gt_depth = static_cast<size_t>(k);
        unsigned long long hits = 0;
        size_t denominator = 0, returned_total = 0;
        double eligible_sum = 0.0;
        for (size_t qi = 0; qi < query_count; ++qi) {
            const char* row = filter_map.data() + qi * dataset.num_objects();
            eligible_sum += double(eligible[qi]);
            size_t returned = 0;
            for (size_t j = 0; j < gt_depth; ++j) {
                const faiss::idx_t id = ids[qi * k + j];
                if (id < 0) break;
                ++returned;
                if (!row[id]) {
                    std::fprintf(stderr, "slice %s query %zu: id %lld fails the predicate\n",
                                 slice.c_str(), qi, static_cast<long long>(id));
                    return 2;
                }
            }
            returned_total += returned;
            for (size_t j = 0; j < gt_depth; ++j) {
                const int truth = dataset.ground_truth[qi * gt_depth + j];
                if (truth < 0) break;
                ++denominator;
                for (size_t r = 0; r < returned; ++r)
                    if (ids[qi * k + r] == truth) { ++hits; break; }
            }
        }
        std::printf("{\"impl\":\"acorn-official\",\"gamma\":%d,\"M\":%d,\"Mbeta\":%d,\"slice\":\"%s\","
                    "\"ef\":%d,\"k\":%d,\"queries\":%zu,\"recall\":%.6f,\"qps\":%.1f,\"per_query_ms\":%.3f,"
                    "\"dists_per_q\":%.1f,\"eligible_per_q\":%.1f,\"avg_returned\":%.2f,\"build_s\":%.1f}\n",
                    gamma, M, M_beta, slice.c_str(), ef, k, query_count,
                    denominator ? double(hits) / double(denominator) : 0.0,
                    pass_ms > 0.0 ? double(query_count) * 1000.0 / pass_ms : 0.0,
                    pass_ms / double(query_count), double(distance_calls) / double(query_count),
                    eligible_sum / double(query_count), double(returned_total) / double(query_count),
                    build_s);
        std::fflush(stdout);
    }
    return 0;
}
