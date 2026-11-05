// ACORN 基线（忠实移植 guestrin-lab/ACORN 的 hybrid_search_from_candidates）：
//  - candidates 是**容量 ef 的有界堆**，停止条件 = 堆内距离小于当前 d0 的个数 ≥ efSearch
//  - **只有通过过滤的节点**才算距离、才进候选堆；未通过的邻居只用于 2 跳扩展
//  - gamma==1 时对每个邻居都做扩展；某节点邻居里命中过滤数达 2M 时停止该节点扩展
// 与原实现的差别：图用 hnswlib（同一 M/efC），向量为 PQ 还原 float32，距离为标量 for 循环。
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
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1";
    std::string vecfile = "recon.bin", mode = "containment", idxfile;
    int k = 50, ef = 50, M = 32, efC = 200, nq_max = 200, Mbeta = 64, gamma = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i]; auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--vec") vecfile = nx();
        else if (a == "--mode") mode = nx();
        else if (a == "--index") idxfile = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--ef") ef = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--Mbeta") Mbeta = std::stoi(nx());
        else if (a == "--gamma") gamma = std::stoi(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
    }

    std::vector<float> vec;
    { FILE* f = std::fopen((dir + "/" + vecfile).c_str(), "rb");
      if (!f) { std::fprintf(stderr, "missing %s\n", vecfile.c_str()); return 2; }
      std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
      vec.resize(b / sizeof(float));
      if (std::fread(vec.data(), sizeof(float), vec.size(), f) != vec.size()) return 2;
      std::fclose(f); }
    const int n = int(vec.size() / DIM);

    std::vector<int> owner;
    { std::ifstream in(work + "/objects.txt"); std::string l; while (std::getline(in, l)) if (!l.empty()) owner.push_back(std::stoi(l)); }
    std::unordered_map<int, std::vector<int>> children;
    { std::ifstream in(work + "/edges.tsv"); std::string l;
      while (std::getline(in, l)) { if (l.empty()) continue; std::stringstream ss(l); int c, p; ss >> c >> p; children[p].push_back(c); } }
    std::vector<std::vector<int>> qc;
    { std::ifstream in(work + "/queries.tsv"); std::string l;
      while (std::getline(in, l) && int(qc.size()) < nq_max) { if (l.empty()) continue; std::stringstream ss(l); int cnt; ss >> cnt;
        std::vector<int> v(cnt); for (int i = 0; i < cnt; ++i) ss >> v[i]; qc.push_back(v); } }

    ScalarL2 space(DIM);
    HierarchicalNSW<float>* g = nullptr; double build_ms = 0;
    if (!idxfile.empty()) { FILE* f = std::fopen(idxfile.c_str(), "rb"); if (f) { std::fclose(f); g = new HierarchicalNSW<float>(&space, idxfile, false, n); } }
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
    { FILE* f = std::fopen((dir + "/queries.bin").c_str(), "rb"); if (!f) return 2;
      std::fseek(f, 0, SEEK_END); long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
      qv.resize(b / sizeof(float)); if (std::fread(qv.data(), sizeof(float), qv.size(), f) != qv.size()) return 2; std::fclose(f); }
    const int nq = std::min<int>(int(qv.size() / DIM), int(qc.size()));

    const auto dist = [&](int id, const float* q) {
        return space.get_dist_func()(vec.data() + size_t(id) * DIM, q, space.get_dist_func_param());
    };

    long hit = 0, denom = 0, dist_calls = 0, visits = 0;
    auto t_all = std::chrono::steady_clock::now();
    std::vector<char> allowed(n, 0);
    std::vector<int> visited_stamp(n, -1);

    for (int qi = 0; qi < nq; ++qi) {
        std::fill(allowed.begin(), allowed.end(), 0);
        if (mode == "equality") {
            std::unordered_set<int> want(qc[qi].begin(), qc[qi].end());
            for (int i = 0; i < n; ++i) if (want.count(owner[i])) allowed[i] = 1;
        } else {
            std::unordered_set<int> scope; std::vector<int> st = qc[qi];
            while (!st.empty()) { int c = st.back(); st.pop_back(); if (!scope.insert(c).second) continue;
                auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
            for (int i = 0; i < n; ++i) if (scope.count(owner[i])) allowed[i] = 1;
        }
        const float* q = qv.data() + size_t(qi) * DIM;

        // 结果（top-k，最大堆） 与 有界候选堆（容量 ef，存最小距离）
        std::vector<std::pair<float,int>> res;      // max-heap by dist
        std::vector<std::pair<float,int>> bestef;   // max-heap, size <= ef
        std::vector<std::pair<float,int>> frontier; // min-heap
        auto cmp_max = [](const std::pair<float,int>& a, const std::pair<float,int>& b){ return a.first < b.first; };
        auto cmp_min = [](const std::pair<float,int>& a, const std::pair<float,int>& b){ return a.first > b.first; };

        auto push_res = [&](float d, int id) {
            if (int(res.size()) < k) { res.push_back({d,id}); std::push_heap(res.begin(), res.end(), cmp_max); }
            else if (d < res.front().first) { std::pop_heap(res.begin(), res.end(), cmp_max); res.back() = {d,id}; std::push_heap(res.begin(), res.end(), cmp_max); }
        };
        auto push_bestef = [&](float d, int id) {
            if (int(bestef.size()) < ef) { bestef.push_back({d,id}); std::push_heap(bestef.begin(), bestef.end(), cmp_max); }
            else if (d < bestef.front().first) { std::pop_heap(bestef.begin(), bestef.end(), cmp_max); bestef.back() = {d,id}; std::push_heap(bestef.begin(), bestef.end(), cmp_max); }
        };
        auto push_frontier = [&](float d, int id) { frontier.push_back({d,id}); std::push_heap(frontier.begin(), frontier.end(), cmp_min); };

        // 入口：与 ACORN 一致，无条件压入候选堆（并算一次距离）
        {
            int ep = int(g->getExternalLabel(g->enterpoint_node_));
            visited_stamp[ep] = qi; ++dist_calls; ++visits;
            float d = dist(ep, q);
            frontier.clear(); bestef.clear();
            push_frontier(d, ep);
            push_bestef(d, ep);
            if (allowed[ep]) push_res(d, ep);
        }

        while (!frontier.empty()) {
            std::pop_heap(frontier.begin(), frontier.end(), cmp_min);
            auto [d0, v0] = frontier.back(); frontier.pop_back();
            int below = 0;
            for (auto& b : bestef) if (b.first < d0) ++below;
            if (below >= ef) break;                      // ACORN: count_below(d0) >= efSearch

            int* ll = (int*)g->get_linklist0(v0);
            size_t cnt = g->getListCount((linklistsizeint*)ll);
            int* ids = ll + 1;
            int num_found = 0;
            bool keep_expanding = true;

            for (size_t j = 0; j < cnt; ++j) {
                int v1 = int(g->getExternalLabel((tableint)ids[j]));
                if (allowed[v1]) {
                    ++num_found;
                    if (visited_stamp[v1] != qi) {
                        visited_stamp[v1] = qi; ++dist_calls; ++visits;
                        float d1 = dist(v1, q);
                        push_res(d1, v1); push_bestef(d1, v1); push_frontier(d1, v1);
                    }
                    if (num_found >= 2 * M) { keep_expanding = false; break; }
                }
                if ((int(j) >= Mbeta && keep_expanding) || gamma == 1) {
                    int* ll2 = (int*)g->get_linklist0(v1);
                    size_t c2 = g->getListCount((linklistsizeint*)ll2);
                    int* d2 = ll2 + 1;
                    for (size_t j2 = 0; j2 < c2; ++j2) {
                        int v2 = int(g->getExternalLabel((tableint)d2[j2]));
                        if (!allowed[v2]) continue;          // 未通过过滤：不扩展、不算距离
                        ++num_found;
                        if (visited_stamp[v2] == qi) continue;
                        visited_stamp[v2] = qi; ++dist_calls; ++visits;
                        float dv = dist(v2, q);
                        push_res(dv, v2); push_bestef(dv, v2); push_frontier(dv, v2);
                        if (num_found >= 2 * M) { keep_expanding = false; break; }
                    }
                }
            }
        }

        std::unordered_set<int> got;
        for (auto& r : res) got.insert(r.second);
        int e = 0, h = 0;
        for (int j = 0; j < k; ++j) { int gid = gt[size_t(qi) * k + j]; if (gid >= 0) { ++e; if (got.count(gid)) ++h; } }
        hit += h; denom += std::min(k, e);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_all).count();
    std::printf("{\n  \"method\": \"acorn_faithful\", \"mode\": \"%s\", \"objects\": %d, \"M\": %d, \"Mbeta\": %d, \"gamma\": %d, \"ef\": %d, \"k\": %d, \"queries\": %d,\n",
                mode.c_str(), n, M, Mbeta, gamma, ef, k, nq);
    std::printf("  \"recall\": %.6f, \"qps\": %.1f, \"dist_calls_per_q\": %.0f, \"visits_per_q\": %.0f, \"build_ms\": %.1f\n}\n",
                double(hit)/double(std::max<long>(denom,1)), nq/secs, double(dist_calls)/nq, double(visits)/nq, build_ms);
    return 0;
}
