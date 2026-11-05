// NodeANN on the CCS concept DAG：每个非空概念一张 HNSW，查询按范围内概念各搜一次再归并。
// 向量用 PQ 还原后的 float32（搜索在 code vector 上，GT 在原始 FP32 上）。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hnswlib/hnswlib.h"

using namespace hnswlib;

// 标量 L2 空间：纯 for 循环，不调用 hnswlib 的 SIMD 内核
struct ScalarL2Space : public SpaceInterface<float> {
    size_t dim_;
    size_t data_size_;
    DISTFUNC<float> fstdistfunc_;
    ScalarL2Space(size_t dim) : dim_(dim), data_size_(dim * sizeof(float)) {
        fstdistfunc_ = [](const void* p1, const void* p2, const void* qty_ptr) -> float {
            const float* a = (const float*)p1;
            const float* b = (const float*)p2;
            size_t d = *(const size_t*)qty_ptr;
            float s = 0.0f;
            for (size_t i = 0; i < d; ++i) { float t = a[i] - b[i]; s += t * t; }
            return s;
        };
    }
    size_t get_data_size() override { return data_size_; }
    DISTFUNC<float> get_dist_func() override { return fstdistfunc_; }
    void* get_dist_func_param() override { return &dim_; }
    ~ScalarL2Space() override {}
};


static const int DIM = 128;

struct Timer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
};

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1";
    std::string mode = "nodeann", vecfile = "recon.bin";
    bool nosimd = false;
    int repeat = 1;
    std::string idxdir = "";
    int k = 50, ef = 50, queries = 200, M = 16, efC = 200, perN = 50;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--ef") ef = std::stoi(nx());
        else if (a == "--queries") queries = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--efC") efC = std::stoi(nx());
        else if (a == "--mode") mode = nx();
        else if (a == "--vec") vecfile = nx();
        else if (a == "--nosimd") nosimd = true;
        else if (a == "--repeat") repeat = std::stoi(nx());
        else if (a == "--N") perN = std::stoi(nx());
        else if (a == "--index-dir") idxdir = nx();
    }

    // 向量（PQ 还原）
    std::vector<float> vec;
    {
        FILE* f = std::fopen((dir + "/" + vecfile).c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing recon.bin\n"); return 2; }
        std::fseek(f, 0, SEEK_END);
        long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        vec.resize(bytes / sizeof(float));
        std::fread(vec.data(), sizeof(float), vec.size(), f);
        std::fclose(f);
    }
    const int n = int(vec.size() / DIM);

    // 对象 -> 直属概念
    std::vector<int> owner;
    {
        std::ifstream in(work + "/objects.txt");
        std::string line;
        while (std::getline(in, line)) if (!line.empty()) owner.push_back(std::stoi(line));
    }

    // DAG
    std::unordered_map<int, std::vector<int>> children;
    std::unordered_set<int> nodes;
    {
        std::ifstream in(work + "/edges.tsv");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            int c, p; ss >> c >> p;
            children[p].push_back(c);
            nodes.insert(c); nodes.insert(p);
        }
    }

    // 查询：概念集合
    std::vector<std::vector<int>> qconcepts;
    {
        std::ifstream in(work + "/queries.tsv");
        std::string line;
        while (std::getline(in, line) && int(qconcepts.size()) < queries) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            int cnt; ss >> cnt;
            std::vector<int> cs(cnt);
            for (int i = 0; i < cnt; ++i) ss >> cs[i];
            qconcepts.push_back(cs);
        }
    }

    // 每概念建图
    std::unordered_map<int, std::vector<int>> members;
    for (int i = 0; i < n; ++i) members[owner[i]].push_back(i);
    L2Space simd_space(DIM);
    ScalarL2Space scalar_space(DIM);
    SpaceInterface<float>& space = nosimd ? (SpaceInterface<float>&)scalar_space : (SpaceInterface<float>&)simd_space;
    std::unordered_map<int, HierarchicalNSW<float>*> graphs;
    Timer build;
    size_t graph_bytes = 0;
    int reused = 0;
    for (auto& [c, ids] : members) {
        std::string path = idxdir.empty() ? std::string() : (idxdir + "/c" + std::to_string(c) + ".bin");
        HierarchicalNSW<float>* g = nullptr;
        if (!path.empty()) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f) {
                std::fclose(f);
                g = new HierarchicalNSW<float>(&space, path, false, ids.size());
                ++reused;
            }
        }
        if (!g) {
            g = new HierarchicalNSW<float>(&space, ids.size(), M, efC, 100 + c);
            for (size_t j = 0; j < ids.size(); ++j) g->addPoint(vec.data() + size_t(ids[j]) * DIM, j);
            if (!path.empty()) g->saveIndex(path);
        }
        graphs[c] = g;
        graph_bytes += ids.size() * (M * 2 * sizeof(int) + sizeof(float) * DIM);
    }
    double build_ms = build.ms();

    // GT
    std::vector<int> gt(size_t(queries) * k, -1);
    {
        FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
        if (f) { std::fread(gt.data(), sizeof(int32_t), gt.size(), f); std::fclose(f); }
    }

    // 查询向量
    std::vector<float> qv;
    {
        FILE* f = std::fopen((dir + "/queries.bin").c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing queries.bin (run prepare)\n"); return 2; }
        std::fread(qv.data(), sizeof(float), 0, f);
        std::fseek(f, 0, SEEK_END);
        long b = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        qv.resize(b / sizeof(float));
        std::fread(qv.data(), sizeof(float), qv.size(), f);
        std::fclose(f);
    }
    const int nq = int(qv.size() / DIM);

    std::vector<int> concept_of(n);
    for (int i = 0; i < n; ++i) concept_of[i] = owner[i];

    long hit = 0, denom = 0, graph_searches = 0;
    std::vector<double> lat;
    double best_pass_ms = 1e18, sum_pass_ms = 0;
    for (int rep = 0; rep < repeat; ++rep) {
    Timer pass_t;
    for (int qi = 0; qi < nq; ++qi) {
        std::unordered_set<int> scope;
        std::vector<int> stack = qconcepts[qi];
        while (!stack.empty()) {
            int c = stack.back(); stack.pop_back();
            if (!scope.insert(c).second) continue;
            auto it = children.find(c);
            if (it != children.end()) for (int w : it->second) stack.push_back(w);
        }
        Timer t;
        std::priority_queue<std::pair<float, int>> best;   // max-heap of (dist, id)
        if (mode == "nodeann") {
            for (int c : scope) {
                auto g = graphs.find(c);
                if (g == graphs.end()) continue;
                ++graph_searches;
                auto r = g->second->searchKnn(qv.data() + size_t(qi) * DIM, perN);
                while (!r.empty()) {
                    auto pr = r.top(); r.pop();
                    int id = members[c][pr.second];
                    if (int(best.size()) < k) best.push({pr.first, id});
                    else if (pr.first < best.top().first) { best.pop(); best.push({pr.first, id}); }
                }
            }
        } else {   // exact scan over the eligible set, on the given vector file
            for (int c : scope) {
                auto m = members.find(c);
                if (m == members.end()) continue;
                for (int id : m->second) {
                    const float* x = vec.data() + size_t(id) * DIM;
                    const float* q = qv.data() + size_t(qi) * DIM;
                    float s = 0; for (int d = 0; d < DIM; ++d) { float t = x[d] - q[d]; s += t * t; }
                    if (int(best.size()) < k) best.push({s, id});
                    else if (s < best.top().first) { best.pop(); best.push({s, id}); }
                }
            }
        }
        lat.push_back(t.ms());
        // ties-aware recall：GT 中与第 k 名距离相同的任意不同对象算命中
        std::unordered_set<int> got;
        while (!best.empty()) { got.insert(best.top().second); best.pop(); }
        int e = 0;
        for (int j = 0; j < k; ++j) if (gt[size_t(qi) * k + j] >= 0) ++e;
        int h = 0;
        for (int j = 0; j < k; ++j) if (gt[size_t(qi) * k + j] >= 0 && got.count(gt[size_t(qi) * k + j])) ++h;
        if (rep == 0) { hit += h; denom += std::min(k, e); }
    }
    double pass_ms = pass_t.ms();
    best_pass_ms = std::min(best_pass_ms, pass_ms);
    sum_pass_ms += pass_ms;
    }
    std::sort(lat.begin(), lat.end());
    double mean = 0; for (double v : lat) mean += v; mean /= double(lat.size());
    std::printf("{\n  \"method\": \"nodeann\", \"objects\": %d, \"concepts\": %zu, \"queries\": %d,\n", n, members.size(), nq);
    std::printf("  \"M\": %d, \"efC\": %d, \"ef\": %d, \"k\": %d,\n", M, efC, ef, k, perN);
    std::printf("  \"recall\": %.6f, \"graph_searches\": %ld,\n", double(hit) / double(std::max<long>(denom, 1)), graph_searches);
    double qps_best = double(nq) * 1000.0 / best_pass_ms;
    double qps_avg = double(nq) * 1000.0 / (sum_pass_ms / repeat);
    std::printf("  \"repeat\": %d, \"qps_best_pass\": %.1f, \"qps_avg_pass\": %.1f, \"per_query_us\": %.2f,\n",
                repeat, qps_best, qps_avg, sum_pass_ms / repeat / double(nq) * 1000.0);
    std::printf("  \"build_ms\": %.1f, \"graphs_reused\": %d, \"graph_dir\": \"%s\"\n}\n", build_ms, reused, idxdir.c_str());
    return 0;
}
