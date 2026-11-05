// ACORN 风格基线（移植）：单张全局 HNSW + 谓词无关的过滤遍历（命中过滤的邻居做 2 跳扩展）。
// 参考实现：stanford-futuredata/ACORN（FAISS 之上，search 接受 filter_ids_map）。
// 这里在同口径下重写：PQ 还原向量、标量距离、ef 固定、每查询谓词 = DAG 闭包位图（包含）或精确集合（相等）。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hnswlib/hnswlib.h"

using namespace hnswlib;
static const int DIM = 128;

struct ScalarL2 : public SpaceInterface<float> {
    size_t dim_, data_size_;
    DISTFUNC<float> f_;
    ScalarL2(size_t d) : dim_(d), data_size_(d * sizeof(float)) {
        f_ = [](const void* p1, const void* p2, const void* q) -> float {
            const float* a = (const float*)p1; const float* b = (const float*)p2;
            size_t d = *(const size_t*)q; float s = 0;
            for (size_t i = 0; i < d; ++i) { float t = a[i] - b[i]; s += t * t; }
            return s;
        };
    }
    size_t get_data_size() override { return data_size_; }
    DISTFUNC<float> get_dist_func() override { return f_; }
    void* get_dist_func_param() override { return &dim_; }
    ~ScalarL2() override {}
};

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann-1m", work = "work/dag-ann-1m";
    int k = 50, ef = 50, M = 32, efC = 200, nq_max = 200, gamma = 1;
    std::string vecfile = "recon_pq.bin", mode = "containment", idxfile = "";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i]; auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--ef") ef = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--efC") efC = std::stoi(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--vec") vecfile = nx();
        else if (a == "--mode") mode = nx();
        else if (a == "--index") idxfile = nx();
    }

    std::vector<float> vec;
    {
        FILE* f = std::fopen((dir + "/" + vecfile).c_str(), "rb");
        if (!f) { std::fprintf(stderr, "missing %s\n", vecfile.c_str()); return 2; }
        std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        vec.resize(b / sizeof(float));
        if (std::fread(vec.data(), sizeof(float), vec.size(), f) != vec.size()) return 2;
        std::fclose(f);
    }
    const int n = int(vec.size() / DIM);

    std::vector<int> owner;
    { std::ifstream in(work + "/objects.txt"); std::string l; while (std::getline(in, l)) if (!l.empty()) owner.push_back(std::stoi(l)); }

    std::unordered_map<int, std::vector<int>> children;
    { std::ifstream in(work + "/edges.tsv"); std::string l;
      while (std::getline(in, l)) { if (l.empty()) continue; std::stringstream ss(l); int c, p; ss >> c >> p; children[p].push_back(c); } }

    std::vector<std::vector<int>> qc;
    { std::ifstream in(work + "/queries.tsv"); std::string l;
      while (std::getline(in, l) && int(qc.size()) < nq_max) { if (l.empty()) continue; std::stringstream ss(l); int cnt; ss >> cnt; std::vector<int> v(cnt); for (int i = 0; i < cnt; ++i) ss >> v[i]; qc.push_back(v); } }

    ScalarL2 space(DIM);
    HierarchicalNSW<float>* g = nullptr;
    double build_ms = 0;
    if (!idxfile.empty()) {
        FILE* f = std::fopen(idxfile.c_str(), "rb");
        if (f) { std::fclose(f); g = new HierarchicalNSW<float>(&space, idxfile, false, n); }
    }
    if (!g) {
        auto t0 = std::chrono::steady_clock::now();
        g = new HierarchicalNSW<float>(&space, n, M, efC, 20261105);
        for (int i = 0; i < n; ++i) g->addPoint(vec.data() + size_t(i) * DIM, i);
        build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!idxfile.empty()) g->saveIndex(idxfile);
    }

    std::vector<int> gt(size_t(nq_max) * k, -1);
    { FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
      if (f) { if (std::fread(gt.data(), sizeof(int32_t), gt.size(), f) != gt.size()) {} std::fclose(f); } }

    std::vector<float> qv;
    { FILE* f = std::fopen((dir + "/queries.bin").c_str(), "rb"); if (!f) { std::fprintf(stderr, "no queries.bin\n"); return 2; }
      std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
      qv.resize(b / sizeof(float)); if (std::fread(qv.data(), sizeof(float), qv.size(), f) != qv.size()) return 2; std::fclose(f); }
    const int nq = std::min<int>(int(qv.size() / DIM), int(qc.size()));

    long hit = 0, denom = 0, dist_calls = 0, uniq_visited = 0, pushes = 0, allowed_seen = 0, res_sizes = 0;
    auto t_all = std::chrono::steady_clock::now();
    std::vector<char> allowed(n);
    for (int qi = 0; qi < nq; ++qi) {
        // 谓词：containment = 闭包；equality = 精确集合
        std::fill(allowed.begin(), allowed.end(), 0);
        if (mode == "equality") {
            std::unordered_set<int> want(qc[qi].begin(), qc[qi].end());
            for (int i = 0; i < n; ++i) if (want.count(owner[i])) allowed[i] = 1;
        } else {
            std::unordered_set<int> scope;
            std::vector<int> st = qc[qi];
            while (!st.empty()) { int c = st.back(); st.pop_back(); if (!scope.insert(c).second) continue;
                auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
            for (int i = 0; i < n; ++i) if (scope.count(owner[i])) allowed[i] = 1;
        }
        // ACORN 风格：best-first，命中过滤的收进结果；未命中的候选做 2 跳扩展
        const float* q = qv.data() + size_t(qi) * DIM;
        std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<>> cand;
        std::unordered_set<int> visited;
        std::unordered_set<int> pushed;
        std::priority_queue<std::pair<float, int>> res;   // max-heap of accepted
        auto push_if_allowed = [&](int id, float d) {
            if (int(res.size()) < k) res.push({d, id});
            else if (d < res.top().first) { res.pop(); res.push({d, id}); }
        };
        // 入口：HNSW 的 ep_
        {
            tableint ep = g->enterpoint_node_;
            int id = int(g->getExternalLabel(ep));
            float d = space.get_dist_func()(vec.data() + size_t(id) * DIM, q, space.get_dist_func_param());
            ++dist_calls;
            cand.push({d, id}); pushed.insert(id);
        }
        size_t pushes_before = size_t(dist_calls);
        while (!cand.empty()) {
            auto [d, u] = cand.top(); cand.pop();
            if (visited.count(u)) continue;
            visited.insert(u);
            if (int(res.size()) >= k && d > res.top().first) break;
            if (allowed[u]) push_if_allowed(u, d);
            // 邻接
            int* ll = (int*)g->get_linklist0(u);
            size_t cnt = g->getListCount((linklistsizeint*)ll);
            int* ids = ll + 1;
            bool expand_two_hop = (!allowed[u]) || (int(res.size()) < k);
            for (size_t i = 0; i < cnt; ++i) {
                int v = int(g->getExternalLabel((tableint)ids[i]));
                if (visited.count(v) || pushed.count(v)) continue;
                float dv = space.get_dist_func()(vec.data() + size_t(v) * DIM, q, space.get_dist_func_param());
                ++dist_calls; ++pushes;
                cand.push({dv, v}); pushed.insert(v);
            }
            if (expand_two_hop && !allowed[u]) {
                for (size_t i = 0; i < cnt; ++i) {
                    int vv = ids[i];
                    int* ll2 = (int*)g->get_linklist0(vv);
                    size_t c2 = g->getListCount((linklistsizeint*)ll2);
                    int* d2 = ll2 + 1;
                    for (size_t j = 0; j < c2; ++j) {
                        int w = int(g->getExternalLabel((tableint)d2[j]));
                        if (visited.count(w) || pushed.count(w)) continue;
                        float dw = space.get_dist_func()(vec.data() + size_t(w) * DIM, q, space.get_dist_func_param());
                        ++dist_calls; ++pushes;
                        cand.push({dw, w}); pushed.insert(w);
                    }
                }
            }
        }
        uniq_visited += (long)visited.size();
        allowed_seen += (long)std::count(allowed.begin(), allowed.end(), (char)1);
        std::unordered_set<int> got;
        while (!res.empty()) { got.insert(res.top().second); res.pop(); }
        res_sizes += (long)got.size();
        int e2 = 0, h2 = 0;
        for (int j = 0; j < k; ++j) { int id = gt[size_t(qi) * k + j]; if (id >= 0) { ++e2; if (got.count(id)) ++h2; } }
        hit += h2; denom += std::min(k, e2);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_all).count();
    std::printf("{\n  \"method\": \"acorn_style\", \"mode\": \"%s\", \"objects\": %d, \"M\": %d, \"efC\": %d, \"ef\": %d, \"k\": %d,\n",
                mode.c_str(), n, M, efC, ef, k);
    std::printf("  \"recall\": %.6f, \"qps\": %.1f, \"dist_calls\": %ld, \"build_ms\": %.1f,\n", double(hit) / double(std::max<long>(denom,1)), nq / secs, dist_calls, build_ms);
    std::printf("  \"uniq_visited_per_q\": %.1f, \"pushes_per_q\": %.1f, \"allowed_per_q\": %.1f, \"res_per_q\": %.2f, \"dup_rate\": %.2f\n}\n",
                double(uniq_visited)/nq, double(pushes)/nq, double(allowed_seen)/nq, double(res_sizes)/nq,
                double(pushes)/std::max<double>(double(uniq_visited),1.0));
    return 0;
}
