// Directory-Aware scheme: one global graph plus a prefix-tree of subtree bitmaps.
//
//   build  : writes <index>/graph.bin (hnswlib) and <index>/trie.bin (bitmaps)
//   search : loads both, resolves the eligible set per query, runs a filtered
//            best-first walk, and reports recall / QPS over an ef sweep
//
// Scope modes:
//   --mode recursive     eligible = subtree(node)
//   --mode nonrecursive  eligible = subtree(node) minus every child's subtree
//
// The bitmap is an in-graph mask: every popped node is tested once (O(1) bit
// test) and only passing nodes enter the result heap. The walk itself may pass
// through nodes outside the mask, which is what keeps the graph connected at low
// selectivity.
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

// attribute id -> dense id, children in dense space
struct Shape {
    std::vector<int> dense;
    std::unordered_map<int, int> position;
    std::vector<std::vector<int>> children;

    void build(const Dataset& dataset) {
        std::set<int> ids(dataset.owner.begin(), dataset.owner.end());
        for (const auto& e : dataset.children_of) ids.insert(e.first);
        for (const auto& e : dataset.parents_of) ids.insert(e.first);
        dense.assign(ids.begin(), ids.end());
        for (size_t i = 0; i < dense.size(); ++i) position[dense[i]] = int(i);
        children.assign(dense.size(), {});
        for (const auto& e : dataset.children_of)
            for (int child : e.second) {
                auto p = position.find(e.first), c = position.find(child);
                if (p != position.end() && c != position.end()) children[size_t(p->second)].push_back(c->second);
            }
    }
};

template <typename T>
void write_value(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage:\n"
            "  directory_aware build  --data DIR --index DIR [--M 32] [--efC 40]\n"
            "  directory_aware search --data DIR --index DIR [--mode recursive|nonrecursive]\n"
            "                         [--efs 10,50,...] [--k 10] [--queries N]\n");
        return 2;
    }
    const std::string command = argv[1];
    std::string data, index;
    std::string mode = "recursive";
    std::string ef_list = "10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800";
    int M = 32, ef_construction = 40, k = 10, query_limit = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--data") data = next();
        else if (a == "--index") index = next();
        else if (a == "--mode") mode = next();
        else if (a == "--efs") ef_list = next();
        else if (a == "--M") M = std::atoi(next().c_str());
        else if (a == "--efC") ef_construction = std::atoi(next().c_str());
        else if (a == "--k") k = std::atoi(next().c_str());
        else if (a == "--queries") query_limit = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown flag %s\n", a.c_str()); return 2; }
    }
    if (data.empty() || index.empty()) { std::fprintf(stderr, "--data and --index required\n"); return 2; }

    Dataset dataset;
    std::string error;
    if (!dataset.load(data, k, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    const size_t n = dataset.num_objects();
    const size_t words = (n + 63) / 64;
    Shape shape;
    shape.build(dataset);

    if (command == "build") {
        if (std::system(("mkdir -p '" + index + "'").c_str()) != 0) {
            std::fprintf(stderr, "cannot create %s\n", index.c_str());
            return 2;
        }
        RawL2Space build_space{size_t(dataset.dim)};   // no counting during construction
        const auto started = now_ms();
        hnswlib::HierarchicalNSW<float> graph(&build_space, n, size_t(M), size_t(ef_construction), 100);
        for (size_t i = 0; i < n; ++i)
            graph.addPoint(dataset.object_vector(i), hnswlib::labeltype(i));
        const double graph_ms = now_ms() - started;
        graph.saveIndex(index + "/graph.bin");

        // subtree bitmaps: own items, then propagate over a post-order
        std::vector<Bitmap> subtree(shape.dense.size(), Bitmap(words, 0));
        for (size_t i = 0; i < n; ++i) {
            auto it = shape.position.find(dataset.owner[i]);
            if (it == shape.position.end()) continue;
            subtree[size_t(it->second)][i / 64] |= 1ull << (i % 64);
        }
        std::vector<char> seen(shape.dense.size(), 0);
        std::vector<std::pair<int, size_t>> walk;
        std::vector<int> postorder;
        for (size_t root = 0; root < shape.dense.size(); ++root) {
            if (seen[root]) continue;
            walk.emplace_back(int(root), 0);
            while (!walk.empty()) {
                auto& [node, cursor] = walk.back();
                seen[size_t(node)] = 1;
                if (cursor < shape.children[size_t(node)].size()) {
                    const int next_node = shape.children[size_t(node)][cursor++];
                    if (!seen[size_t(next_node)]) walk.emplace_back(next_node, 0);
                } else {
                    postorder.push_back(node);
                    walk.pop_back();
                }
            }
        }
        for (int a : postorder)
            for (int c : shape.children[size_t(a)])
                for (size_t w = 0; w < words; ++w) subtree[size_t(a)][w] |= subtree[size_t(c)][w];

        std::ofstream out(index + "/trie.bin", std::ios::binary);
        const uint64_t magic = 0x4441545249453031ull;   // "DATRIE01"
        const uint64_t attribute_count = shape.dense.size();
        write_value(out, magic);
        write_value(out, n);
        write_value(out, attribute_count);
        for (size_t a = 0; a < attribute_count; ++a) {
            const int32_t id = shape.dense[a];
            write_value(out, id);
            out.write(reinterpret_cast<const char*>(subtree[a].data()),
                      std::streamsize(words * sizeof(uint64_t)));
        }
        out.close();
        std::printf("{\"stage\":\"build\",\"n\":%zu,\"attributes\":%zu,\"graph_ms\":%.1f,"
                    "\"index\":\"%s\"}\n", n, attribute_count, graph_ms, index.c_str());
        return 0;
    }

    if (command != "search") { std::fprintf(stderr, "unknown command %s\n", command.c_str()); return 2; }

    const bool recursive = mode != "nonrecursive";

    // load trie
    std::ifstream in(index + "/trie.bin", std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s/trie.bin\n", index.c_str()); return 2; }
    uint64_t magic = 0, stored_n = 0, attribute_count = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&stored_n), sizeof(stored_n));
    in.read(reinterpret_cast<char*>(&attribute_count), sizeof(attribute_count));
    if (magic != 0x4441545249453031ull || stored_n != n || attribute_count != shape.dense.size()) {
        std::fprintf(stderr, "trie.bin header mismatch\n"); return 2;
    }
    std::vector<Bitmap> subtree(attribute_count);
    for (size_t a = 0; a < attribute_count; ++a) {
        int32_t id = 0;
        in.read(reinterpret_cast<char*>(&id), sizeof(id));
        subtree[size_t(shape.position[id])].resize(words);
        in.read(reinterpret_cast<char*>(subtree[size_t(shape.position[id])].data()),
                std::streamsize(words * sizeof(uint64_t)));
    }
    in.close();

    CountedSpace space{size_t(dataset.dim)};
    hnswlib::HierarchicalNSW<float> graph(&space, index + "/graph.bin", false, 0);

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

    std::vector<Bitmap> eligible(nq);
    const auto resolve_start = now_ms();
    for (size_t q = 0; q < nq; ++q) {
        Bitmap& mask = eligible[q];
        mask.assign(words, 0);
        for (int attribute : dataset.query_attributes[q]) {
            auto it = shape.position.find(attribute);
            if (it == shape.position.end()) continue;
            const size_t a = size_t(it->second);
            for (size_t w = 0; w < words; ++w) mask[w] |= subtree[a][w];
            if (!recursive)
                for (int c : shape.children[a])
                    for (size_t w = 0; w < words; ++w) mask[w] &= ~subtree[size_t(c)][w];
        }
    }
    const double resolve_ms = now_ms() - resolve_start;
    std::printf("{\"stage\":\"resolve\",\"mode\":\"%s\",\"ms_total\":%.2f,\"ms_per_query\":%.4f}\n",
                recursive ? "recursive" : "nonrecursive", resolve_ms, resolve_ms / double(nq));

    for (size_t ef : efs) {
        graph.setEf(std::max(ef, size_t(k)));
        const unsigned long long calls_before = space.calls.load();
        const auto started = now_ms();
        unsigned long long hits = 0, relevant = 0;
#pragma omp parallel for schedule(dynamic, 1) reduction(+ : hits, relevant)
        for (size_t q = 0; q < nq; ++q) {
            const Bitmap& mask = eligible[q];
            const float* query = dataset.query_vector(q);
            auto allowed = [&](hnswlib::tableint node) {
                return (mask[size_t(node) / 64] >> (node % 64)) & 1ull;
            };
            std::vector<uint8_t> visited(n, 0);
            std::priority_queue<std::pair<float, hnswlib::tableint>> result;
            std::priority_queue<std::pair<float, hnswlib::tableint>,
                                std::vector<std::pair<float, hnswlib::tableint>>,
                                std::greater<>> frontier;
            const hnswlib::tableint entry = graph.enterpoint_node_;
            const float entry_distance = space.dist(
                query, reinterpret_cast<const float*>(graph.getDataByInternalId(entry)));
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
                    const float d = space.dist(
                        query, reinterpret_cast<const float*>(graph.getDataByInternalId(neighbour)));
                    if (!(result.size() < ef || d < result.top().first)) continue;
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
            std::unordered_set<int> got;
            for (size_t i = 0; i < ranked.size() && got.size() < size_t(k); ++i) got.insert(ranked[i].second);
            for (int rank = 0; rank < k; ++rank) {
                const int truth = dataset.ground_truth[q * size_t(k) + size_t(rank)];
                if (truth < 0) continue;
                ++relevant;
                if (got.count(truth)) ++hits;
            }
        }
        const double seconds = (now_ms() - started) / 1000.0;
        std::printf("{\"stage\":\"search\",\"mode\":\"%s\",\"ef\":%zu,\"recall\":%.6f,\"qps\":%.1f,"
                    "\"dists_per_q\":%.1f,\"queries\":%zu}\n",
                    recursive ? "recursive" : "nonrecursive", ef,
                    relevant ? double(hits) / double(relevant) : 0.0, double(nq) / seconds,
                    double(space.calls.load() - calls_before) / double(nq), nq);
        std::fflush(stdout);
    }
    return 0;
}
