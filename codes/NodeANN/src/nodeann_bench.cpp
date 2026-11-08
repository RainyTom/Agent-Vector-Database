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
#include "raw_l2_space.h"
#include "native_graph_audit.h"

using namespace hnswlib;

// Count calls while delegating every distance to the project RawL2Space.
// The --nosimd spelling remains accepted for compatibility; compile with
// -DNO_MANUAL_VECTORIZATION, as for ASI, to select hnswlib's scalar path.
struct CountedL2Space : public RawL2Space {
    explicit CountedL2Space(size_t dim) : RawL2Space(dim) {}
    mutable unsigned long long calls = 0;

    float dist(const float* a, const float* b) const {
        ++calls;
        return RawL2Space::dist(a, b);
    }
    DISTFUNC<float> get_dist_func() override { return counted_dist; }
    void* get_dist_func_param() override { return this; }

private:
    static float counted_dist(const void* a, const void* b, const void* param) {
        const auto* self = static_cast<const CountedL2Space*>(param);
        ++self->calls;
        return self->RawL2Space::dist(static_cast<const float*>(a), static_cast<const float*>(b));
    }
};


static const int DIM = 128;

struct Timer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
};

int main(int argc, char** argv) {
    std::string dir = "datasets/ccs-sift-dag", work = "datasets/ccs-sift-dag";
    std::string mode = "nodeann", vecfile = "recon.bin";
    bool nosimd = false;
    int repeat = 1;
    std::string idxdir = "";
    std::string origfile = "orig.bin";
    std::string trace_file, graph_audit_file;
    bool rerank_fp32 = false;
    int pool = 0;
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
        else if (a == "--orig-file") origfile = nx();
        else if (a == "--pool") pool = std::stoi(nx());
        else if (a == "--trace-file") trace_file = nx();
        else if (a == "--graph-audit-file") graph_audit_file = nx();
        else if (a == "--rerank-fp32") rerank_fp32 = true;
    }
    FILE* trace = trace_file.empty() ? nullptr : std::fopen(trace_file.c_str(), "w");
    if (!trace_file.empty() && !trace) { std::perror(trace_file.c_str()); return 2; }
    FILE* audit = graph_audit_file.empty() ? nullptr : std::fopen(graph_audit_file.c_str(), "w");
    if (!graph_audit_file.empty() && !audit) { std::perror(graph_audit_file.c_str()); return 2; }

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
    std::vector<float> orig;
    bool need_orig = (mode == "rerank") || rerank_fp32;
    if (need_orig) {
        FILE* f = std::fopen((dir + "/" + origfile).c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing %s\n", origfile.c_str()); return 2; }
        orig.resize(size_t(n) * DIM);
        size_t got = std::fread(orig.data(), sizeof(float), orig.size(), f);
        std::fclose(f);
        if (got != orig.size()) { std::fprintf(stderr, "orig size mismatch\n"); return 2; }
    }

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
    CountedL2Space space(DIM);
    (void)nosimd;  // use -DNO_MANUAL_VECTORIZATION for the ASI-compatible scalar build
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
        if (audit) write_native_graph_audit(audit, c, *g);
        g->setEf(ef);
        graphs[c] = g;
        graph_bytes += ids.size() * (M * 2 * sizeof(int) + sizeof(float) * DIM);
    }
    if (audit) std::fclose(audit);
    double build_ms = build.ms();
    space.calls = 0;  // exclude graph construction and index loading

    // 查询向量
    std::vector<float> qv;
    {
        FILE* f = std::fopen((dir + "/queries.bin").c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing queries.bin (run prepare)\n"); return 2; }
        std::fseek(f, 0, SEEK_END);
        long b = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        qv.resize(b / sizeof(float));
        if (std::fread(qv.data(), sizeof(float), qv.size(), f) != qv.size()) {
            std::fprintf(stderr, "queries.bin read error\n"); std::fclose(f); return 2;
        }
        std::fclose(f);
    }
    const int nq = std::min({queries, int(qv.size() / DIM), int(qconcepts.size())});
    if (nq <= 0) { std::fprintf(stderr, "no usable queries\n"); return 2; }

    // GT
    std::vector<int> gt(size_t(nq) * k, -1);
    {
        FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing ground truth\n"); return 2; }
        const size_t got = std::fread(gt.data(), sizeof(int32_t), gt.size(), f);
        std::fclose(f);
        if (got != gt.size()) { std::fprintf(stderr, "ground truth read error\n"); return 2; }
    }


    std::vector<int> concept_of(n);
    for (int i = 0; i < n; ++i) concept_of[i] = owner[i];

    unsigned long long hit = 0, denom = 0, graph_searches = 0, rerank_calls = 0;
    std::vector<double> lat;
    double best_pass_ms = 1e18, sum_pass_ms = 0;
    for (int rep = 0; rep < repeat; ++rep) {
    double pass_ms = 0;
    for (int qi = 0; qi < nq; ++qi) {
        const auto distance_start = space.calls;
        Timer t;  // includes scope expansion, search, and result merge; excludes recall
        std::unordered_set<int> scope;
        std::vector<int> stack = qconcepts[qi];
        while (!stack.empty()) {
            int c = stack.back(); stack.pop_back();
            if (!scope.insert(c).second) continue;
            auto it = children.find(c);
            if (it != children.end()) for (int w : it->second) stack.push_back(w);
        }
        std::priority_queue<std::pair<float, int>> best;   // max-heap of (dist, id)
        if (mode == "rerank") {
            int want = pool > 0 ? pool : perN;
            std::vector<std::pair<float, int>> cand;
            for (int c : scope) {
                auto g = graphs.find(c);
                if (g == graphs.end()) continue;
                ++graph_searches;
                auto r = g->second->searchKnn(qv.data() + size_t(qi) * DIM, want);
                while (!r.empty()) { auto pr = r.top(); r.pop(); cand.push_back({pr.first, members[c][pr.second]}); }
            }
            // 用原始 FP32 重排前 k
            std::priority_queue<std::pair<float, int>> ex;   // max-heap
            const float* q = qv.data() + size_t(qi) * DIM;
            for (auto& pr : cand) {
                const float* x = orig.data() + size_t(pr.second) * DIM;
                float s = space.dist(x, q);
                if (int(ex.size()) < k) ex.push({s, pr.second});
                else if (s < ex.top().first) { ex.pop(); ex.push({s, pr.second}); }
            }
            best = ex;
        } else if (mode == "nodeann") {
            std::vector<std::pair<float, int>> cand;
            for (int c : scope) {
                auto g = graphs.find(c);
                if (g == graphs.end()) continue;
                ++graph_searches;
                auto r = g->second->searchKnn(qv.data() + size_t(qi) * DIM, perN);
                while (!r.empty()) {
                    auto pr = r.top(); r.pop();
                    int id = members[c][pr.second];
                    if (rerank_fp32) { cand.push_back({pr.first, id}); continue; }
                    if (int(best.size()) < k) best.push({pr.first, id});
                    else if (pr.first < best.top().first) { best.pop(); best.push({pr.first, id}); }
                }
            }
            // 同一候选流先按重构距离保留 pool 条，再用原始 FP32 重排；距离计入查询成本。
            if (rerank_fp32) {
                const size_t pool_size = size_t(pool > 0 ? pool : perN);
                std::priority_queue<std::pair<float, int>> keep_pool;
                for (auto& pr : cand) {
                    if (keep_pool.size() < pool_size) keep_pool.push(pr);
                    else if (pr.first < keep_pool.top().first) { keep_pool.pop(); keep_pool.push(pr); }
                }
                const float* q = qv.data() + size_t(qi) * DIM;
                while (!keep_pool.empty()) {
                    auto pr = keep_pool.top(); keep_pool.pop();
                    const float s = space.dist(orig.data() + size_t(pr.second) * DIM, q);
                    if (int(best.size()) < k) best.push({s, pr.second});
                    else if (s < best.top().first) { best.pop(); best.push({s, pr.second}); }
                }
                rerank_calls += pool_size < cand.size() ? pool_size : cand.size();
            }
        } else {   // exact scan over the eligible set, on the given vector file
            for (int c : scope) {
                auto m = members.find(c);
                if (m == members.end()) continue;
                for (int id : m->second) {
                    const float* x = vec.data() + size_t(id) * DIM;
                    const float* q = qv.data() + size_t(qi) * DIM;
                    float s = space.dist(x, q);
                    if (int(best.size()) < k) best.push({s, id});
                    else if (s < best.top().first) { best.pop(); best.push({s, id}); }
                }
            }
        }
        // Recall uses strict GT ID-set intersection; no tie-aware distance handling.
        std::unordered_set<int> got;
        while (!best.empty()) { got.insert(best.top().second); best.pop(); }
        const double query_ms = t.ms();
        lat.push_back(query_ms);
        pass_ms += query_ms;
        int e = 0;
        for (int j = 0; j < k; ++j) if (gt[size_t(qi) * k + j] >= 0) ++e;
        int h = 0;
        for (int j = 0; j < k; ++j) if (gt[size_t(qi) * k + j] >= 0 && got.count(gt[size_t(qi) * k + j])) ++h;
        if (rep == 0) { hit += h; denom += std::min(k, e); }
        if (trace && rep == 0) {
            std::fprintf(trace, "{\"qi\":%d,\"ef\":%d,\"perN\":%d,\"distance_calls\":%llu,\"returned\":[",
                         qi, ef, perN, space.calls - distance_start);
            bool first = true;
            for (int id : got) { std::fprintf(trace, "%s%d", first ? "" : ",", id); first = false; }
            std::fprintf(trace, "]}\n");
        }
    }
    best_pass_ms = std::min(best_pass_ms, pass_ms);
    sum_pass_ms += pass_ms;
    }
    std::sort(lat.begin(), lat.end());
    double mean = 0; for (double v : lat) mean += v; mean /= double(lat.size());
    std::printf("{\n  \"method\": \"nodeann\", \"objects\": %d, \"concepts\": %zu, \"queries\": %d,\n", n, members.size(), nq);
    std::printf("  \"M\": %d, \"efC\": %d, \"ef\": %d, \"k\": %d, \"perN\": %d,\n", M, efC, ef, k, perN);
    std::printf("  \"recall\": %.6f, \"graph_searches\": %llu, \"dist_calls_per_q\": %.1f,\n",
                double(hit) / double(std::max<unsigned long long>(denom, 1)), graph_searches,
                double(space.calls) / double(std::max(1, nq * repeat)));
    double qps_best = double(nq) * 1000.0 / best_pass_ms;
    double qps_avg = double(nq) * 1000.0 / (sum_pass_ms / repeat);
    std::printf("  \"repeat\": %d, \"qps_best_pass\": %.1f, \"qps_avg_pass\": %.1f, \"per_query_us\": %.2f,\n",
                repeat, qps_best, qps_avg, sum_pass_ms / repeat / double(nq) * 1000.0);
    std::printf("  \"rerank_fp32\": %d, \"rerank_calls_per_q\": %.1f,\n", int(rerank_fp32),
                double(rerank_calls) / double(std::max(1, nq * repeat)));
    std::printf("  \"build_ms\": %.1f, \"graphs_reused\": %d, \"graph_dir\": \"%s\"\n}\n", build_ms, reused, idxdir.c_str());
    if (trace) std::fclose(trace);
    return 0;
}
