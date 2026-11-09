// Drive port::Acorn (the acorn-hnswlib reproduction) on the unified dataset
// layout and the fixed-selectivity slices, so its numbers are comparable with
// codes/ACORN at the same k, ef and query set.
//
// Build:
//   g++ -std=c++17 -O3 -fno-tree-vectorize -fno-tree-slp-vectorize -DNO_MANUAL_VECTORIZATION \
//       -I codes/acorn-hnswlib -I codes/acorn-hnswlib/upstream -I codes/shared \
//       codes/acorn-hnswlib/acorn_index.cpp codes/shared/dataset.cpp \
//       codes/acorn-hnswlib/selectivity_driver.cpp \
//       -o codes/acorn-hnswlib/build/selectivity_driver
//
// The index is built once per gamma over the base vectors of the first slice,
// persisted, and reused. Every slice then builds its own per-query masks from
// the leaf-union attribute sets, so the filtering is exactly the same as in the
// codes/ACORN runs.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <hnswlib/hnswlib.h>

#include "acorn_index.h"
#include "dataset.h"

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Masks {
    std::vector<std::vector<uint8_t>> per_query;
    std::vector<size_t> eligible;
};

// owner -> objects, indexed by a dense attribute id; each object has one owner,
// so closures give disjoint object sets and the sizes are exact.
Masks build_masks(const Dataset& dataset, size_t query_count) {
    std::unordered_map<int, int> dense_of;
    std::vector<int> dense_owner(dataset.num_objects(), -1);
    std::vector<std::vector<hnswlib::tableint>> members;
    for (size_t id = 0; id < dataset.num_objects(); ++id) {
        const int attribute = dataset.owner[id];
        auto found = dense_of.find(attribute);
        if (found == dense_of.end()) {
            found = dense_of.emplace(attribute, static_cast<int>(members.size())).first;
            members.emplace_back();
        }
        dense_owner[id] = found->second;
        members[static_cast<size_t>(found->second)].push_back(static_cast<hnswlib::tableint>(id));
    }

    Masks masks;
    masks.per_query.resize(query_count);
    masks.eligible.assign(query_count, 0);
    std::vector<int> stack, closed;
    std::vector<uint8_t> seen(members.size(), 0);
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
        std::vector<uint8_t> mask(dataset.num_objects(), 0);
        size_t count = 0;
        for (int dense : closed) {
            for (auto id : members[static_cast<size_t>(dense)]) {
                mask[id] = 1;
                ++count;
            }
        }
        masks.eligible[qi] = count;
        masks.per_query[qi] = std::move(mask);
    }
    return masks;
}

}  // namespace

int main(int argc, char** argv) {
    std::string index_path;
    std::vector<std::string> slices;
    int gamma = 12;
    int ef = 60;
    int queries = 100;
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
        else if (flag == "--build-only") { build_only = true; }
        else { std::fprintf(stderr, "unknown flag: %s\n", flag.c_str()); return 2; }
    }
    if (index_path.empty() || slices.empty()) {
        std::fprintf(stderr, "usage: selectivity_driver --index PATH --slice DIR [--slice DIR...] "
                             "[--gamma 12|1] [--ef 60] [--queries 100] [--build-only]\n");
        return 2;
    }

    const int prefix = gamma == 12 ? 64 : 32;
    const int k = port::top_k;
    hnswlib::L2Space space(128);

    std::unique_ptr<port::Acorn> index;
    double build_ms = 0.0;
    FILE* probe = std::fopen(index_path.c_str(), "rb");
    if (probe) {
        std::fclose(probe);
        Dataset first;
        std::string error;
        if (!first.load(slices.front(), k, &error)) {
            std::fprintf(stderr, "dataset load failed: %s\n", error.c_str());
            return 2;
        }
        index = std::make_unique<port::Acorn>(space, gamma, prefix);
        index->load(index_path, space);
        std::printf("{\"impl\":\"acorn-hnswlib\",\"action\":\"load\",\"gamma\":%d,\"prefix\":%d,"
                    "\"objects\":%zu,\"index_bytes\":%zu}\n",
                    gamma, prefix, index->size(), static_cast<size_t>(0));
    } else {
        Dataset first;
        std::string error;
        if (!first.load(slices.front(), k, &error)) {
            std::fprintf(stderr, "dataset load failed: %s\n", error.c_str());
            return 2;
        }
        index = std::make_unique<port::Acorn>(space, gamma, prefix);
        const auto start = Clock::now();
        index->build(first.base.data(), first.num_objects());
        build_ms = ms_since(start);
        index->validate();
        index->saveIndex(index_path);
        std::printf("{\"impl\":\"acorn-hnswlib\",\"action\":\"build\",\"gamma\":%d,\"prefix\":%d,"
                    "\"objects\":%zu,\"M\":32,\"efC\":40,\"build_s\":%.1f}\n",
                    gamma, prefix, index->size(), build_ms / 1000.0);
        std::fflush(stdout);
    }
    if (build_only) return 0;

    for (const auto& slice : slices) {
        Dataset dataset;
        std::string error;
        if (!dataset.load(slice, k, &error)) {
            std::fprintf(stderr, "dataset load failed: %s\n", error.c_str());
            return 2;
        }
        if (dataset.num_objects() != index->size()) {
            std::fprintf(stderr, "%s: %zu objects but index holds %zu\n",
                         slice.c_str(), dataset.num_objects(), index->size());
            return 2;
        }
        if (static_cast<size_t>(queries) > dataset.num_queries()) queries = static_cast<int>(dataset.num_queries());
        const Masks masks = build_masks(dataset, static_cast<size_t>(queries));
        const size_t gt_depth = static_cast<size_t>(k);

        unsigned long long hits = 0, calls_total = 0, returned_total = 0;
        double pass_ms = 0.0;
        for (int qi = 0; qi < queries; ++qi) {
            uint64_t calls = 0;
            const auto start = Clock::now();
            port::Result result = index->search(dataset.query_vector(static_cast<size_t>(qi)),
                                                masks.per_query[static_cast<size_t>(qi)].data(), ef, &calls);
            pass_ms += ms_since(start);
            calls_total += calls;
            size_t valid = 0;
            for (size_t j = 0; j < gt_depth; ++j)
                if (dataset.ground_truth[static_cast<size_t>(qi) * gt_depth + j] >= 0) ++valid;
            size_t returned = 0;
            for (size_t j = 0; j < gt_depth; ++j) {
                const port::Id id = result.ids[j];
                if (id < 0) break;
                ++returned;
                if (!masks.per_query[static_cast<size_t>(qi)][static_cast<size_t>(id)]) {
                    std::fprintf(stderr, "slice %s query %d: id %lld fails the predicate\n",
                                 slice.c_str(), qi, static_cast<long long>(id));
                    return 2;
                }
            }
            returned_total += returned;
            for (size_t j = 0; j < gt_depth; ++j) {
                const int truth = dataset.ground_truth[static_cast<size_t>(qi) * gt_depth + j];
                if (truth < 0) break;
                for (size_t r = 0; r < returned; ++r)
                    if (result.ids[r] == truth) { ++hits; break; }
            }
        }
        size_t denominator = 0;
        for (int qi = 0; qi < queries; ++qi)
            for (size_t j = 0; j < gt_depth; ++j)
                if (dataset.ground_truth[static_cast<size_t>(qi) * gt_depth + j] >= 0) ++denominator;
        double eligible = 0.0;
        for (int qi = 0; qi < queries; ++qi) eligible += double(masks.eligible[static_cast<size_t>(qi)]);
        eligible /= double(queries);
        std::printf("{\"impl\":\"acorn-hnswlib\",\"gamma\":%d,\"slice\":\"%s\",\"ef\":%d,\"k\":%d,"
                    "\"queries\":%d,\"recall\":%.6f,\"qps\":%.1f,\"per_query_ms\":%.3f,"
                    "\"dists_per_q\":%.1f,\"eligible_per_q\":%.1f,\"avg_returned\":%.2f,"
                    "\"build_s\":%.1f}\n",
                    gamma, slice.c_str(), ef, k, queries,
                    denominator ? double(hits) / double(denominator) : 0.0,
                    pass_ms > 0 ? double(queries) * 1000.0 / pass_ms : 0.0,
                    pass_ms / double(queries), double(calls_total) / double(queries), eligible,
                    double(returned_total) / double(queries), build_ms / 1000.0);
        std::fflush(stdout);
    }
    return 0;
}
