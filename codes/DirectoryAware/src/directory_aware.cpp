// Directory-Aware baseline: one global HNSW plus a prefix-tree (trie) over the
// attribute DAG whose nodes carry Roaring-style bitmaps of their subtree items.
//
//   recursive     : eligible = the query node's subtree bitmap
//   non-recursive : eligible = subtree(node) \ union(subtree(child)), i.e. the
//                   items owned directly by the node
//
// Both then run a filtered best-first search over the single global graph: only
// eligible nodes enter the candidate and result heaps.
#include <omp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dataset.h"
#include "hnswlib/hnswlib.h"
#include "raw_l2_space.h"

namespace {

struct CountedSpace : public RawL2Space {
    explicit CountedSpace(size_t dim) : RawL2Space(dim) {}
    float dist(const float* a, const float* b) const {
        calls.fetch_add(1, std::memory_order_relaxed);
        return RawL2Space::dist(a, b);
    }
    hnswlib::DISTFUNC<float> get_dist_func() override { return counted; }
    void* get_dist_func_param() override { return this; }
    mutable std::atomic<unsigned long long> calls{0};
    static float counted(const void* a, const void* b, const void* param) {
        auto* self = const_cast<CountedSpace*>(static_cast<const CountedSpace*>(param));
        self->calls.fetch_add(1, std::memory_order_relaxed);
        return self->RawL2Space::dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
};

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

using Bitmap = std::vector<uint64_t>;

}  // namespace

int main(int argc, char** argv) {
    std::string data = "datasets/sift1m-ccs-concepts-simulate";
    std::string mode = "recursive";
    std::string ef_list = "10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800";
    int M = 32, ef_construction = 40, k = 10, query_limit = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--data") data = next();
        else if (a == "--mode") mode = next();
        else if (a == "--efs") ef_list = next();
        else if (a == "--M") M = std::atoi(next().c_str());
        else if (a == "--efC") ef_construction = std::atoi(next().c_str());
        else if (a == "--k") k = std::atoi(next().c_str());
        else if (a == "--queries") query_limit = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown flag %s\n", a.c_str()); return 2; }
    }
    const bool recursive = mode != "nonrecursive";

    Dataset dataset;
    std::string error;
    if (!dataset.load(data, k, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    const size_t n = dataset.num_objects();

    // dense attribute ids
    std::set<int> ids(dataset.owner.begin(), dataset.owner.end());
    for (const auto& e : dataset.children_of) ids.insert(e.first);
    for (const auto& e : dataset.parents_of) ids.insert(e.first);
    std::vector<int> dense(ids.begin(), ids.end());
    std::unordered_map<int, int> position;
    for (size_t i = 0; i < dense.size(); ++i) position[dense[i]] = int(i);
    const size_t attributes = dense.size();

    std::vector<std::vector<int>> children(attributes);
    for (const auto& e : dataset.children_of)
        for (int child : e.second) {
            auto p = position.find(e.first), c = position.find(child);
            if (p != position.end() && c != position.end()) children[size_t(p->second)].push_back(c->second);
        }

    // prefix-tree bitmaps: subtree(a) = own(a) union subtree(children)
    const size_t words = (n + 63) / 64;
    const auto build_start = now_ms();
    std::vector<Bitmap> subtree(attributes);
    {
        std::vector<int> order;
        {
            std::vector<int> state(attributes, 0);
            std::vector<std::pair<int, int>> stack;
            for (size_t a = 0; a < attributes; ++a) stack.emplace_back(int(a), 0);
            // post-order over the DAG, iterative
            std::vector<int> seq;
            std::vector<char> seen(attributes, 0);
            std::vector<std::pair<int, size_t>> walk;
            for (size_t root = 0; root < attributes; ++root) {
                if (seen[root]) continue;
                walk.emplace_back(int(root), 0);
                while (!walk.empty()) {
                    auto& [node, cursor] = walk.back();
                    seen[size_t(node)] = 1;
                    if (cursor < children[size_t(node)].size()) {
                        const int next_node = children[size_t(node)][cursor++];
                        if (!seen[size_t(next_node)]) walk.emplace_back(next_node, 0);
                    } else {
                        seq.push_back(node);
                        walk.pop_back();
                    }
                }
            }
            order = seq;
        }
        for (size_t a = 0; a < attributes; ++a) subtree[a].assign(words, 0);
        for (size_t i = 0; i < n; ++i) {
            auto it = position.find(dataset.owner[i]);
            if (it == position.end()) continue;
            const size_t a = size_t(it->second);
            subtree[a][i / 64] |= 1ull << (i % 64);
        }
        for (int a : order) {
            for (int c : children[size_t(a)]) {
                const Bitmap& child = subtree[size_t(c)];
                Bitmap& parent = subtree[size_t(a)];
                for (size_t w = 0; w < words; ++w) parent[w] |= child[w];
            }
        }
    }
    const double bitmap_ms = now_ms() - build_start;

    CountedSpace space{size_t(dataset.dim)};
    hnswlib::HierarchicalNSW<float> graph(&space, n, size_t(M), size_t(ef_construction), 100);
    for (size_t i = 0; i < n; ++i)
        graph.addPoint(dataset.object_vector(i), hnswlib::labeltype(i));

    std::printf("{\"stage\":\"build\",\"n\":%zu,\"attributes\":%zu,\"bitmap_ms\":%.1f,"
                "\"graph_ms\":%.1f}\n", n, attributes, bitmap_ms, now_ms() - build_start - bitmap_ms);

    std::vector<size_t> efs;
    {
        size_t start = 0;
        while (start <= ef_list.size()) {
            const size_t comma = ef_list.find(',', start);
            const std::string token = ef_list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!token.empty()) efs.push_back(size_t(std::atoi(token.c_str())));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }

    const size_t nq = query_limit > 0 ? std::min<size_t>(size_t(query_limit), dataset.num_queries())
                                      : dataset.num_queries();

    // eligible bitmaps, one per query, resolved from the trie
    std::vector<Bitmap> eligible(nq);
    const auto resolve_start = now_ms();
    for (size_t q = 0; q < nq; ++q) {
        Bitmap& mask = eligible[q];
        mask.assign(words, 0);
        for (int attribute : dataset.query_attributes[q]) {
            auto it = position.find(attribute);
            if (it == position.end()) continue;
            const size_t a = size_t(it->second);
            const Bitmap& node = subtree[a];
            for (size_t w = 0; w < words; ++w) mask[w] |= node[w];
            if (!recursive) {
                // remove every direct child's subtree, leaving only direct items
                for (int c : children[a]) {
                    const Bitmap& child = subtree[size_t(c)];
                    for (size_t w = 0; w < words; ++w) mask[w] &= ~child[w];
                }
            }
        }
    }
    std::printf("{\"stage\":\"resolve\",\"ms_total\":%.2f,\"ms_per_query\":%.4f}\n",
                now_ms() - resolve_start, (now_ms() - resolve_start) / double(nq));

    for (size_t ef : efs) {
        graph.setEf(std::max(ef, size_t(k)));
        const unsigned long long calls_before = space.calls.load();
        const auto start = now_ms();
        unsigned long long hits = 0, relevant = 0;
#pragma omp parallel for schedule(dynamic, 1) reduction(+ : hits, relevant)
        for (size_t q = 0; q < nq; ++q) {
            const Bitmap& mask = eligible[q];
            const float* query = dataset.query_vector(q);
            auto allowed = [&](hnswlib::tableint node) {
                return (mask[size_t(node) / 64] >> (node % 64)) & 1ull;
            };
            std::vector<uint8_t> visited(n, 0);
            std::priority_queue<std::pair<float, hnswlib::tableint>> result;   // max-heap
            std::priority_queue<std::pair<float, hnswlib::tableint>,
                                std::vector<std::pair<float, hnswlib::tableint>>,
                                std::greater<>> frontier;                      // min-heap
            hnswlib::tableint entry = graph.enterpoint_node_;
            const float entry_distance = space.dist(query, reinterpret_cast<const float*>(graph.getDataByInternalId(entry)));
            // the entry point is always traversed; the bitmap only decides what may
            // be returned, never which nodes the graph walk may pass through
            frontier.push({entry_distance, entry});
            if (allowed(entry)) result.push({entry_distance, entry});
            visited[entry] = 1;
            while (!frontier.empty()) {
                const auto current = frontier.top();
                frontier.pop();
                if (result.size() >= ef && current.first > result.top().first) break;
                const hnswlib::linklistsizeint* list = graph.get_linklist0(current.second);
                const size_t count = graph.getListCount(const_cast<hnswlib::linklistsizeint*>(list));
                const auto* neighbours = reinterpret_cast<const hnswlib::tableint*>(list + 1);
                for (size_t j = 0; j < count; ++j) {
                    const hnswlib::tableint neighbour = neighbours[j];
                    if (visited[neighbour]) continue;
                    visited[neighbour] = 1;
                    const float d = space.dist(query, reinterpret_cast<const float*>(graph.getDataByInternalId(neighbour)));
                    const bool keep = result.size() < ef || d < result.top().first;
                    if (!keep) continue;
                    frontier.push({d, neighbour});
                    if (allowed(neighbour)) {
                        if (result.size() < ef) result.push({d, neighbour});
                        else { result.pop(); result.push({d, neighbour}); }
                    }
                }
            }
            std::vector<std::pair<float, int>> ranked;
            while (!result.empty()) {
                ranked.emplace_back(result.top().first, int(graph.getExternalLabel(result.top().second)));
                result.pop();
            }
            std::sort(ranked.begin(), ranked.end());
            std::vector<int> ids;
            for (size_t i = 0; i < ranked.size() && ids.size() < size_t(k); ++i)
                ids.push_back(ranked[i].second);
            const std::unordered_set<int> got(ids.begin(), ids.end());
            for (int rank = 0; rank < k; ++rank) {
                const int truth = dataset.ground_truth[q * size_t(k) + size_t(rank)];
                if (truth < 0) continue;
                ++relevant;
                if (got.count(truth)) ++hits;
            }
        }
        const double seconds = (now_ms() - start) / 1000.0;
        std::printf("{\"stage\":\"search\",\"mode\":\"%s\",\"ef\":%zu,\"recall\":%.6f,\"qps\":%.1f,"
                    "\"dists_per_q\":%.1f,\"queries\":%zu}\n",
                    recursive ? "recursive" : "nonrecursive", ef,
                    relevant ? double(hits) / double(relevant) : 0.0, double(nq) / seconds,
                    double(space.calls.load() - calls_before) / double(nq), nq);
        std::fflush(stdout);
    }
    return 0;
}
