// "Oracle Partition (subtree)" baseline: one HNSW per attribute built over the
// attribute's whole subtree, i.e. every object is materialised once per ancestor.
//
// A recursive query then becomes a single graph search on the graph of the query
// attribute — no per-directory fan-out and no merge — at the cost of storing
// sum_a |Desc+(a)| vector instances instead of N.
#include <omp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage:\n"
            "  oracle_subtree build  --data DIR --index DIR [--M 32] [--efC 40]\n"
            "  oracle_subtree search --data DIR --index DIR [--efs 10,50,...] [--k 10]\n");
        return 2;
    }
    const std::string command = argv[1];
    std::string index;
    std::string data = "datasets/sift1m-ccs-concepts-simulate";
    std::string ef_list = "10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800";
    int M = 32, ef_construction = 40, k = 10, query_limit = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--data") data = next();
        else if (a == "--index") index = next();
        else if (a == "--efs") ef_list = next();
        else if (a == "--M") M = std::atoi(next().c_str());
        else if (a == "--efC") ef_construction = std::atoi(next().c_str());
        else if (a == "--k") k = std::atoi(next().c_str());
        else if (a == "--queries") query_limit = std::atoi(next().c_str());
        else { std::fprintf(stderr, "unknown flag %s\n", a.c_str()); return 2; }
    }

    Dataset dataset;
    std::string error;
    if (!dataset.load(data, k, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
    const size_t n = dataset.num_objects();
    const int dim = dataset.dim;

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

    std::vector<std::vector<int>> own(attributes);
    for (size_t i = 0; i < n; ++i) {
        auto it = position.find(dataset.owner[i]);
        if (it != position.end()) own[size_t(it->second)].push_back(int(i));
    }

    // subtree member lists (objects duplicated across ancestors)
    std::vector<std::vector<int>> members(attributes);
    size_t instances = 0;
    for (size_t a = 0; a < attributes; ++a) {
        std::vector<int> stack{int(a)};
        std::vector<char> seen(attributes, 0);
        while (!stack.empty()) {
            const int v = stack.back();
            stack.pop_back();
            if (seen[size_t(v)]) continue;
            seen[size_t(v)] = 1;
            members[a].insert(members[a].end(), own[size_t(v)].begin(), own[size_t(v)].end());
            for (int c : children[size_t(v)]) stack.push_back(c);
        }
        std::sort(members[a].begin(), members[a].end());
        instances += members[a].size();
    }

    if (command == "build") {
        if (index.empty()) { std::fprintf(stderr, "--index required for build\n"); return 2; }
        if (std::system(("mkdir -p '" + index + "'").c_str()) != 0) return 2;
        RawL2Space build_space{size_t(dim)};
        const auto build_start = now_ms();
        size_t built = 0;
        for (size_t a = 0; a < attributes; ++a) {
            if (members[a].empty()) continue;
            hnswlib::HierarchicalNSW<float> graph(&build_space, members[a].size(), size_t(M),
                                                 size_t(ef_construction), 100 + int(a));
            for (size_t j = 0; j < members[a].size(); ++j)
                graph.addPoint(dataset.object_vector(size_t(members[a][j])), hnswlib::labeltype(j));
            graph.saveIndex(index + "/attribute_" + std::to_string(dense[a]) + ".bin");
            ++built;
        }
        std::ofstream table(index + "/members.bin", std::ios::binary);
        const uint64_t magic = 0x4F52435542545231ull;   // "ORCUBTR1"
        const uint64_t count = attributes;
        table.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        table.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (size_t a = 0; a < attributes; ++a) {
            const int32_t id = dense[a];
            const uint32_t size = uint32_t(members[a].size());
            table.write(reinterpret_cast<const char*>(&id), sizeof(id));
            table.write(reinterpret_cast<const char*>(&size), sizeof(size));
            table.write(reinterpret_cast<const char*>(members[a].data()),
                        std::streamsize(size * sizeof(int)));
        }
        table.close();
        std::printf("{\"stage\":\"build\",\"graphs\":%zu,\"instances\":%zu,\"seconds\":%.1f,"
                    "\"index\":\"%s\"}\n", built, instances, (now_ms() - build_start) / 1000.0, index.c_str());
        return 0;
    }
    if (command != "search") { std::fprintf(stderr, "unknown command %s\n", command.c_str()); return 2; }
    if (index.empty()) { std::fprintf(stderr, "--index required for search\n"); return 2; }

    std::ifstream table(index + "/members.bin", std::ios::binary);
    if (!table) { std::fprintf(stderr, "cannot open %s/members.bin\n", index.c_str()); return 2; }
    uint64_t magic = 0, count = 0;
    table.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    table.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (magic != 0x4F52435542545231ull || count != attributes) {
        std::fprintf(stderr, "members.bin header mismatch\n"); return 2;
    }
    for (size_t a = 0; a < attributes; ++a) {
        int32_t id = 0; uint32_t size = 0;
        table.read(reinterpret_cast<char*>(&id), sizeof(id));
        table.read(reinterpret_cast<char*>(&size), sizeof(size));
        members[size_t(position[id])].resize(size);
        table.read(reinterpret_cast<char*>(members[size_t(position[id])].data()),
                   std::streamsize(size * sizeof(int)));
    }
    table.close();

    CountedSpace space{size_t(dim)};
    std::vector<std::unique_ptr<hnswlib::HierarchicalNSW<float>>> graphs(attributes);
    std::vector<size_t> nonempty;
    for (size_t a = 0; a < attributes; ++a) {
        if (members[a].empty()) continue;
        graphs[a] = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            &space, index + "/attribute_" + std::to_string(dense[a]) + ".bin", false, 0);
        nonempty.push_back(a);
    }

    // ef grid
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

    for (size_t ef : efs) {
        for (size_t a : nonempty) graphs[a]->setEf(std::max(ef, size_t(k)));
        const unsigned long long calls_before = space.calls.load();
        const auto start_ms = now_ms();
        unsigned long long hits = 0, relevant = 0;
#pragma omp parallel for schedule(dynamic, 1) reduction(+ : hits, relevant)
        for (size_t q = 0; q < nq; ++q) {
            std::vector<int> candidates;
            for (int attribute : dataset.query_attributes[q]) {
                auto it = position.find(attribute);
                if (it == position.end() || !graphs[size_t(it->second)]) continue;
                const size_t a = size_t(it->second);
                auto found = graphs[a]->searchKnn(dataset.query_vector(q), size_t(k));
                while (!found.empty()) {
                    const auto entry = found.top();
                    found.pop();
                    candidates.push_back(members[a][size_t(entry.second)]);
                }
            }
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            if (candidates.size() > size_t(k)) candidates.resize(size_t(k));
            const std::unordered_set<int> got(candidates.begin(), candidates.end());
            for (int rank = 0; rank < k; ++rank) {
                const int truth = dataset.ground_truth[q * size_t(k) + size_t(rank)];
                if (truth < 0) continue;
                ++relevant;
                if (got.count(truth)) ++hits;
            }
        }
        const double seconds = (now_ms() - start_ms) / 1000.0;
        std::printf("{\"stage\":\"search\",\"ef\":%zu,\"recall\":%.6f,\"qps\":%.1f,"
                    "\"dists_per_q\":%.1f,\"queries\":%zu}\n",
                    ef, relevant ? double(hits) / double(relevant) : 0.0, double(nq) / seconds,
                    double(space.calls.load() - calls_before) / double(nq), nq);
        std::fflush(stdout);
    }
    return 0;
}

