// Prefilter and postfilter baselines over the unified hierarchical dataset.
//
//   prefilter  : resolve the eligible set, then score every eligible vector (exact).
//   postfilter : one global HNSW search with 1/s over-sampling, then drop vectors
//                that fail the predicate, doubling the request when short.
//
// Both use hnswlib's SIMD distance kernels and OpenMP over queries.
#include <omp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dataset.h"
#include "hnswlib/hnswlib.h"
#include "raw_l2_space.h"

namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

// Predicate space: attribute -> dense id, descendant closure bitmaps.
struct PredicateSpace {
    std::vector<int> dense_of_attribute;   // attribute id -> dense index (sorted keys)
    std::unordered_map<int, int> position;
    std::vector<std::vector<int>> children;
    std::vector<std::vector<uint64_t>> closure;   // per attribute, over dense ids
    std::vector<int> owner_dense;                 // per object

    void build(const Dataset& dataset, const std::vector<int>& owner, size_t object_count) {
        std::set<int> ids(owner.begin(), owner.end());
        for (const auto& entry : dataset.children_of) ids.insert(entry.first);
        for (const auto& entry : dataset.parents_of) ids.insert(entry.first);
        dense_of_attribute.assign(ids.begin(), ids.end());
        for (size_t i = 0; i < dense_of_attribute.size(); ++i) position[dense_of_attribute[i]] = int(i);
        const size_t n = dense_of_attribute.size();
        children.assign(n, {});
        for (const auto& entry : dataset.children_of)
            for (int child : entry.second) {
                auto parent_it = position.find(entry.first);
                auto child_it = position.find(child);
                if (parent_it != position.end() && child_it != position.end())
                    children[size_t(parent_it->second)].push_back(child_it->second);
            }
        const size_t words = (n + 63) / 64;
        closure.assign(n, std::vector<uint64_t>(words, 0));
        for (size_t a = 0; a < n; ++a) {
            std::vector<int> stack{int(a)};
            closure[a][a / 64] |= 1ull << (a % 64);
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                for (int c : children[size_t(v)]) {
                    if ((closure[a][size_t(c) / 64] >> (c % 64)) & 1ull) continue;
                    closure[a][size_t(c) / 64] |= 1ull << (c % 64);
                    stack.push_back(c);
                }
            }
        }
        owner_dense.assign(object_count, -1);
        for (size_t i = 0; i < object_count; ++i) {
            auto it = position.find(owner[i]);
            if (it != position.end()) owner_dense[i] = it->second;
        }
    }

    std::vector<uint64_t> resolve(const std::vector<int>& attributes) const {
        std::vector<uint64_t> bits(closure.empty() ? 0 : closure[0].size(), 0);
        for (int attribute : attributes) {
            auto it = position.find(attribute);
            if (it == position.end()) continue;
            const auto& one = closure[size_t(it->second)];
            for (size_t w = 0; w < bits.size(); ++w) bits[w] |= one[w];
        }
        return bits;
    }

    bool passes(int dense_attribute, const std::vector<uint64_t>& bits) const {
        if (dense_attribute < 0) return false;
        return (bits[size_t(dense_attribute) / 64] >> (dense_attribute % 64)) & 1ull;
    }
};

std::vector<size_t> parse_list(const std::string& text) {
    std::vector<size_t> values;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!token.empty()) values.push_back(size_t(std::strtoul(token.c_str(), nullptr, 10)));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return values;
}

struct Row {
    size_t ef = 0;
    double recall = 0.0, qps = 0.0;
    double extra = 0.0;
};

void emit(const std::string& method, const Row& row, const char* extra_name) {
    std::printf("{\"method\":\"%s\",\"ef\":%zu,\"recall\":%.6f,\"qps\":%.1f,\"%s\":%.2f}\n",
                method.c_str(), row.ef, row.recall, row.qps, extra_name, row.extra);
    std::fflush(stdout);
}

double hit_rate(const Dataset& dataset, size_t query_id, const std::vector<int>& returned) {
    const size_t k = size_t(dataset.k);
    const std::unordered_set<int> got(returned.begin(), returned.end());
    size_t relevant = 0, hits = 0;
    for (size_t rank = 0; rank < k; ++rank) {
        const int32_t truth = dataset.ground_truth[query_id * k + rank];
        if (truth < 0) continue;
        ++relevant;
        if (got.count(int(truth))) ++hits;
    }
    return relevant ? double(hits) / double(relevant) : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, data;
    size_t k = 10, M = 32, ef_construction = 40, query_limit = 0;
    std::string ef_list = "10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--mode") mode = next();
        else if (a == "--data") data = next();
        else if (a == "--efs") ef_list = next();
        else if (a == "--k") k = size_t(std::atoi(next().c_str()));
        else if (a == "--M") M = size_t(std::atoi(next().c_str()));
        else if (a == "--efC") ef_construction = size_t(std::atoi(next().c_str()));
        else if (a == "--queries") query_limit = size_t(std::atoi(next().c_str()));
        else { std::fprintf(stderr, "unknown flag %s\n", a.c_str()); return 2; }
    }
    if (data.empty() || (mode != "prefilter" && mode != "postfilter")) {
        std::fprintf(stderr, "usage: filters --mode prefilter|postfilter --data DIR [--efs ...]\n");
        return 2;
    }

    Dataset dataset;
    std::string error;
    if (!dataset.load(data, int(k), &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    const size_t n = dataset.num_objects();
    const size_t nq = query_limit ? std::min(query_limit, dataset.num_queries()) : dataset.num_queries();
    std::vector<int> owner(n);
    for (size_t i = 0; i < n; ++i) owner[i] = dataset.owner[i];

    PredicateSpace predicate;
    predicate.build(dataset, owner, n);

    std::vector<std::vector<uint64_t>> bits(nq);
    std::vector<size_t> eligible(nq, 0);
    for (size_t q = 0; q < nq; ++q) {
        bits[q] = predicate.resolve(dataset.query_attributes[q]);
        size_t count = 0;
        for (size_t i = 0; i < n; ++i)
            if (predicate.passes(predicate.owner_dense[i], bits[q])) ++count;
        eligible[q] = count;
    }
    double eligible_mean = 0.0;
    for (size_t q = 0; q < nq; ++q) eligible_mean += double(eligible[q]);
    eligible_mean /= double(std::max<size_t>(1, nq));

    RawL2Space space(size_t(dataset.dim));
    const std::vector<size_t> efs = parse_list(ef_list);

    if (mode == "prefilter") {
        // Exact scan over the eligible set: recall is 1 by construction.
        const auto start = now_ms();
        double recall = 0.0;
#pragma omp parallel for schedule(dynamic, 1) reduction(+ : recall)
        for (size_t q = 0; q < nq; ++q) {
            std::vector<std::pair<float, int>> best;
            best.reserve(size_t(k) + 1);
            const float* query = dataset.query_vector(q);
            for (size_t i = 0; i < n; ++i) {
                if (!predicate.passes(predicate.owner_dense[i], bits[q])) continue;
                const float d = space.dist(query, dataset.object_vector(i));
                if (best.size() < k) {
                    best.emplace_back(d, int(i));
                    std::push_heap(best.begin(), best.end());
                } else if (d < best.front().first) {
                    std::pop_heap(best.begin(), best.end());
                    best.back() = {d, int(i)};
                    std::push_heap(best.begin(), best.end());
                }
            }
            std::sort(best.begin(), best.end());
            std::vector<int> ids;
            ids.reserve(best.size());
            for (const auto& item : best) ids.push_back(item.second);
            recall += hit_rate(dataset, q, ids);
        }
        Row row;
        row.ef = 0;
        row.recall = recall / double(nq);
        row.qps = double(nq) / ((now_ms() - start) / 1000.0);
        row.extra = eligible_mean;
        emit("prefilter", row, "eligible_per_q");
        return 0;
    }

    // postfilter: one global graph, 1/s over-sampling with doubling on shortfall.
    hnswlib::HierarchicalNSW<float> graph(&space, n, M, ef_construction, 100);
    for (size_t i = 0; i < n; ++i)
        graph.addPoint(dataset.object_vector(i), hnswlib::labeltype(i));

    for (size_t ef : efs) {
        graph.setEf(std::max<size_t>(ef, k));
        const auto start = now_ms();
        double recall = 0.0;
        double returned_mean = 0.0;
        std::atomic<size_t> short_queries{0};
#pragma omp parallel for schedule(dynamic, 1) reduction(+ : recall, returned_mean)
        for (size_t q = 0; q < nq; ++q) {
            const float* query = dataset.query_vector(q);
            const size_t pass = std::max<size_t>(1, eligible[q]);
            size_t request = std::max(k, size_t(std::ceil(double(k) * double(n) / double(pass))));
            request = std::min(request, n);
            std::vector<int> ids;
            for (int attempt = 0; attempt < 8; ++attempt) {
                ids.clear();
                auto found = graph.searchKnn(query, request);
                while (!found.empty()) {
                    const auto entry = found.top();
                    found.pop();
                    if (predicate.passes(predicate.owner_dense[size_t(entry.second)], bits[q]))
                        ids.push_back(int(entry.second));
                }
                if (ids.size() >= k) break;
                if (request >= n) break;
                request = std::min(request * 2, n);
            }
            if (ids.size() < k) ++short_queries;
            std::sort(ids.begin(), ids.end());
            ids.resize(std::min(ids.size(), k));
            returned_mean += double(ids.size());
            recall += hit_rate(dataset, q, ids);
        }
        Row row;
        row.ef = ef;
        row.recall = recall / double(nq);
        row.qps = double(nq) / ((now_ms() - start) / 1000.0);
        row.extra = returned_mean / double(nq);
        emit("postfilter", row, "returned_per_q");
    }
    return 0;
}
