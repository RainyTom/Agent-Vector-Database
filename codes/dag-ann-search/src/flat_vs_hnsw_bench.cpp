// 单层图 vs 多层图：在**每概念 subgraph** 这一层做 A/B。
//
// A（单层）：概念内精确 kNN 图（M 近邻）+ 对称化 + 组件桥接（每组件质心连到全局质心）
// B（多层）：hnswlib 每概念 HNSW（M=16, efC=200，读缓存 idx/c*.bin）
//
// 两者共用：入口 = 概念内距质心最近的 item；beam search(ef) 返回每概念 top-N；
// 查询 = 范围内各概念各搜一次，并集取 top-k。距离为标量平方 L2（与 NodeANN/B-scan 同源）。
// 输出：recall@k、QPS、每查询概念搜索数、每概念平均 µs。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hnswlib/hnswlib.h"
#include "raw_l2_space.h"

static constexpr int DIM = 128;

using Adj = std::vector<std::vector<uint32_t>>;

struct FlatIndex {
    std::vector<uint32_t> members;  // concept -> global ids
    Adj adj;                        // local ids
    uint32_t entry = 0;             // local id
};

static bool readf(const std::string& p, std::vector<float>& v) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long b = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    v.resize(size_t(b) / sizeof(float));
    const bool ok = std::fread(v.data(), sizeof(float), v.size(), f) == v.size();
    std::fclose(f);
    return ok;
}

static inline float l2(const float* a, const float* b) {
    float s = 0.f;
    for (int d = 0; d < DIM; ++d) { const float t = a[d] - b[d]; s += t * t; }
    return s;
}

// 单层构建：精确 M 近邻 + 对称化 + 组件桥接
static void build_flat(const float* base, const std::vector<uint32_t>& ids, int M, Adj& out, uint32_t& entry) {
    const int n = int(ids.size());
    out.assign(n, {});
    if (n == 1) { entry = 0; return; }
    std::vector<std::vector<std::pair<float, uint32_t>>> knn(n);
    for (int i = 0; i < n; ++i) {
        const float* pi = base + size_t(ids[i]) * DIM;
        std::vector<std::pair<float, uint32_t>> cand;
        cand.reserve(n - 1);
        for (int j = 0; j < n; ++j) if (j != i) cand.push_back({l2(pi, base + size_t(ids[j]) * DIM), uint32_t(j)});
        const size_t keep = std::min<size_t>(M, cand.size());
        std::partial_sort(cand.begin(), cand.begin() + keep, cand.end());
        cand.resize(keep);
        knn[i] = std::move(cand);
    }
    for (int i = 0; i < n; ++i) for (auto& c : knn[i]) out[size_t(i)].push_back(c.second);
    // 对称化（去重）
    for (int i = 0; i < n; ++i)
        for (uint32_t j : out[size_t(i)]) {
            auto& back = out[j];
            if (std::find(back.begin(), back.end(), uint32_t(i)) == back.end()) back.push_back(uint32_t(i));
        }
    // 组件检查 + 桥接
    std::vector<int> comp(size_t(n), -1);
    int ncomp = 0;
    for (int s = 0; s < n; ++s) {
        if (comp[s] >= 0) continue;
        std::vector<int> st{s};
        comp[s] = ncomp;
        while (!st.empty()) {
            const int v = st.back(); st.pop_back();
            for (uint32_t w : out[size_t(v)]) if (comp[w] < 0) { comp[w] = ncomp; st.push_back(int(w)); }
        }
        ++ncomp;
    }
    // 每组件质心 -> 最近成员；1 号组件质心作为全局 hub
    std::vector<uint32_t> rep(size_t(ncomp), 0);
    for (int c = 0; c < ncomp; ++c) {
        float cx[DIM] = {0};
        int cnt = 0;
        for (int i = 0; i < n; ++i) if (comp[i] == c) { const float* p = base + size_t(ids[i]) * DIM; for (int d = 0; d < DIM; ++d) cx[d] += p[d]; ++cnt; }
        for (int d = 0; d < DIM; ++d) cx[d] /= std::max(1, cnt);
        float best = INFINITY; uint32_t bid = 0;
        for (int i = 0; i < n; ++i) if (comp[i] == c) { const float d2 = l2(cx, base + size_t(ids[i]) * DIM); if (d2 < best) { best = d2; bid = uint32_t(i); } }
        rep[size_t(c)] = bid;
    }
    for (int c = 1; c < ncomp; ++c) {
        out[rep[0]].push_back(rep[size_t(c)]);
        out[rep[size_t(c)]].push_back(rep[0]);
    }
    entry = rep[0];
    if (ncomp > 1) std::fprintf(stderr, "[flat] concept n=%d -> %d components (bridged)\n", n, ncomp);
}

// beam search：从 entry 出发，ef 预算，返回 local ids 的 top-N
static void beam(const float* q, const float* base, const std::vector<uint32_t>& ids, const Adj& g,
                 uint32_t entry, int ef, int N, std::vector<std::pair<float, uint32_t>>& out,
                 std::vector<uint32_t>& stamp, uint32_t token) {
    out.clear();
    if (ids.empty()) return;
    std::priority_queue<std::pair<float, uint32_t>> cand;   // min-heap via negative
    std::priority_queue<std::pair<float, uint32_t>> best;   // max-heap of best ef
    // 距离按需计算：先查 visited，再算距离（与 hnswlib 同口径，避免重复计算）
    auto push = [&](uint32_t v) {
        if (stamp[v] == token) return;
        stamp[v] = token;
        const float d = l2(q, base + size_t(ids[v]) * DIM);
        cand.push({-d, v});
        if (int(best.size()) < ef) best.push({d, v});
        else if (d < best.top().first) { best.pop(); best.push({d, v}); }
    };
    push(entry);
    while (!cand.empty()) {
        const auto [nd, v] = cand.top(); cand.pop();
        const float dv = -nd;
        if (int(best.size()) >= ef && dv > best.top().first) break;
        for (uint32_t w : g[v]) push(w);
    }
    out.assign(best.size(), {});
    for (int i = int(best.size()) - 1; i >= 0; --i) { out[size_t(i)] = best.top(); best.pop(); }
    if (int(out.size()) > N) out.resize(size_t(N));
}

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", idxdir = "work/dag-ann/idx", vec = "recon.bin";
    int k = 50, M = 16, ef = 50, nq_max = 200;
    std::vector<int> Ns{10, 50};
    bool run_flat = true, run_hnsw = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--idx") idxdir = nx();
        else if (a == "--vec") vec = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--ef") ef = std::stoi(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--N") { Ns.clear(); std::stringstream ss(nx()); std::string t; while (std::getline(ss, t, ',')) if (!t.empty()) Ns.push_back(std::stoi(t)); }
        else if (a == "--only") { std::string m = nx(); run_flat = (m != "hnsw"); run_hnsw = (m != "flat"); }
    }

    std::vector<float> base, queries;
    if (!readf(dir + "/" + vec, base) || !readf(dir + "/queries.bin", queries)) { std::fprintf(stderr, "missing vectors\n"); return 2; }
    const size_t n = base.size() / DIM;
    const int nq = std::min<int>(int(queries.size() / DIM), nq_max);

    std::vector<int> owner;
    { std::ifstream in(work + "/objects.txt"); std::string l; while (std::getline(in, l)) if (!l.empty()) owner.push_back(std::stoi(l)); }
    std::unordered_map<int, std::vector<int>> children;
    { std::ifstream in(work + "/edges.tsv"); std::string l;
      while (std::getline(in, l)) { if (l.empty()) continue; std::stringstream ss(l); int c, p; ss >> c >> p; children[p].push_back(c); } }
    std::vector<std::vector<int>> qc;
    { std::ifstream in(work + "/queries.tsv"); std::string l;
      while (std::getline(in, l) && int(qc.size()) < nq) { if (l.empty()) continue;
        std::stringstream ss(l); int cnt; ss >> cnt; std::vector<int> v(cnt); for (int i = 0; i < cnt; ++i) ss >> v[i]; qc.push_back(v); } }
    std::vector<int> gt(size_t(nq) * k, -1);
    { FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
      if (f) { if (std::fread(gt.data(), sizeof(int32_t), gt.size(), f) != gt.size()) {} std::fclose(f); } }

    // 概念 -> item 列表
    std::unordered_map<int, std::vector<uint32_t>> members_map;
    for (size_t i = 0; i < n; ++i) members_map[owner[i]].push_back(uint32_t(i));

    // A：单层图
    std::unordered_map<int, FlatIndex> flat;
    if (run_flat) {
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& [c, ids] : members_map) {
            FlatIndex fi;
            fi.members = ids;
            build_flat(base.data(), ids, M, fi.adj, fi.entry);
            flat.emplace(c, std::move(fi));
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[flat] built %zu concepts in %.1fs\n", flat.size(), s);
    }

    // B：hnswlib 每概念 HNSW（读缓存）
    RawL2Space space(DIM);
    std::unordered_map<int, hnswlib::HierarchicalNSW<float>*> hnsw;
    if (run_hnsw) {
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& [c, ids] : members_map) {
            const std::string p = idxdir + "/c" + std::to_string(c) + ".bin";
            hnswlib::HierarchicalNSW<float>* h = nullptr;
            FILE* f = std::fopen(p.c_str(), "rb");
            if (f) {
                std::fclose(f);
                h = new hnswlib::HierarchicalNSW<float>(&space, p);
            } else {
                h = new hnswlib::HierarchicalNSW<float>(&space, ids.size(), M, 200, 100 + c);
                for (size_t i = 0; i < ids.size(); ++i) h->addPoint(base.data() + size_t(ids[i]) * DIM, i);
            }
            h->setEf(ef);
            hnsw.emplace(c, h);
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[hnsw] loaded %zu concepts in %.1fs\n", hnsw.size(), s);
    }

    std::vector<uint32_t> stamp(size_t(n), 0);
    uint32_t token = 0;
    std::vector<std::pair<float, uint32_t>> got;
    std::vector<std::pair<float, int>> merged;

    for (int N : Ns) {
        for (int arm = 0; arm < 2; ++arm) {
            if (arm == 0 && !run_flat) continue;
            if (arm == 1 && !run_hnsw) continue;
            const char* name = arm == 0 ? "flat_single_layer" : "hnsw_multi_layer";
            long hit = 0, denom = 0, searches = 0;
            const auto t1 = std::chrono::steady_clock::now();
            for (int qi = 0; qi < nq; ++qi) {
                std::unordered_set<int> scope;
                std::vector<int> st = qc[qi];
                while (!st.empty()) { const int c = st.back(); st.pop_back(); if (!scope.insert(c).second) continue;
                    auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
                merged.clear();
                const float* q = queries.data() + size_t(qi) * DIM;
                for (int c : scope) {
                    if (arm == 0) {
                        auto it = flat.find(c);
                        if (it == flat.end()) continue;
                        ++searches;
                        ++token;
                        beam(q, base.data(), it->second.members, it->second.adj, it->second.entry, ef, N, got, stamp, token);
                        for (auto& g : got) merged.push_back({g.first, int(it->second.members[g.second])});
                    } else {
                        auto it = hnsw.find(c);
                        if (it == hnsw.end()) continue;
                        ++searches;
                        auto r = it->second->searchKnn(q, std::min<size_t>(N, it->second->getCurrentElementCount()));
                        const auto& ids = members_map[c];
                        while (!r.empty()) { merged.push_back({r.top().first, int(ids[r.top().second])}); r.pop(); }
                    }
                }
                const size_t keep = std::min<size_t>(k, merged.size());
                std::partial_sort(merged.begin(), merged.begin() + keep, merged.end());
                std::unordered_set<int> top;
                for (size_t i = 0; i < keep; ++i) top.insert(merged[i].second);
                int e = 0, h = 0;
                for (int j = 0; j < k; ++j) { const int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (top.count(g)) ++h; } }
                hit += h; denom += std::min(k, e);
            }
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            std::printf("{\"arm\":\"%s\",\"N\":%d,\"ef\":%d,\"k\":%d,\"queries\":%d,\"recall\":%.6f,\"qps\":%.1f,"
                        "\"searches_per_q\":%.2f,\"us_per_concept\":%.1f}\n",
                        name, N, ef, k, nq, double(hit) / double(std::max<long>(denom, 1)), nq / s,
                        double(searches) / nq, 1e6 * s / std::max<long>(searches, 1));
            std::fflush(stdout);
        }
    }
    for (auto& kv : hnsw) delete kv.second;
    return 0;
}
