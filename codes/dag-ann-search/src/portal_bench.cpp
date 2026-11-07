// NodeANN+ 两种查询模式 + 适配我们结构的 HNSW 式搜索。
//
// 索引：每概念一张单层图（增量插入 + 启发式剪边 + 孤立点补边 + 组件桥接）；
//       每概念额外维护 rep[]（farthest-first 代表点），作为"上层的替代物"——
//       HNSW 用上层做入口下降，我们用"距查询最近的代表点"选入口。
//       跨概念：门户桥（父{入口,最远代表} ↔ 子入口，双向，独立邻接表）。
//
// 两种查询：
//   NR（非递归）：对范围内**每个概念**各自从上式入口起做一次 beam，预算 ef，取 top-N，最后合并。
//                 ——等价于 NodeANN 的结构，但内层换成单层图 + 代表点入口。
//   R （递归）  ：只从**顶部概念**入口进**一个**队列；每概念各自持有 ef_c = max(8, ef/|D|) 的
//                 结果表（这就是 HNSW 的 ef 在多图结构上的推广）；门上桥可跳进子概念，
//                 跳进去消耗子概念自己的预算。
//
// 输出：每个 (mode, option, ef) 一条 JSON：recall/QPS/dists_per_q/entry_cost。
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

static constexpr int DIM = 128;
using Vec = std::vector<uint32_t>;

struct LocalGraph {
    Vec members;
    std::vector<Vec> adj;
    std::vector<Vec> portal;
    Vec reps;                 // 代表点（局部 id）
    uint32_t entry = 0;
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

static void build_local(const float* base, const Vec& ids, int M, int efC, int nrep, LocalGraph& g) {
    const int n = int(ids.size());
    g.members = ids;
    g.adj.assign(size_t(n), {});
    g.portal.assign(size_t(n), {});
    if (n == 1) { g.entry = 0; g.reps = {0}; return; }
    g.adj[0].push_back(1);
    g.adj[1].push_back(0);
    g.entry = 0;

    std::vector<uint32_t> visited(size_t(n), 0);
    uint32_t token = 0;
    std::vector<std::pair<float, uint32_t>> cands;

    for (int i = 2; i < n; ++i) {
        ++token;
        const float* q = base + size_t(ids[size_t(i)]) * DIM;
        std::priority_queue<std::pair<float, uint32_t>> frontier;
        frontier.push({-l2(q, base + size_t(ids[g.entry]) * DIM), g.entry});
        visited[g.entry] = token;
        cands.clear();
        while (!frontier.empty()) {
            const auto [nd, v] = frontier.top(); frontier.pop();
            const float dv = -nd;
            if (cands.size() >= size_t(efC)) {
                float worst = 0;
                for (auto& c : cands) worst = std::max(worst, c.first);
                if (dv > worst) break;
            }
            cands.push_back({dv, v});
            for (uint32_t w : g.adj[v]) {
                if (visited[w] == token) continue;
                visited[w] = token;
                frontier.push({-l2(q, base + size_t(ids[w]) * DIM), w});
            }
        }
        std::sort(cands.begin(), cands.end());
        if (cands.size() > size_t(efC)) cands.resize(size_t(efC));
        Vec chosen;
        for (auto& c : cands) {
            if (int(chosen.size()) >= M) break;
            const float* p = base + size_t(ids[c.second]) * DIM;
            bool ok = true;
            for (uint32_t s : chosen) {
                const float* sp = base + size_t(ids[s]) * DIM;
                if (l2(p, sp) < c.first) { ok = false; break; }
            }
            if (ok) chosen.push_back(c.second);
        }
        if (chosen.empty() && !cands.empty()) chosen.push_back(cands[0].second);
        for (uint32_t w : chosen) {
            g.adj[size_t(i)].push_back(w);
            auto& back = g.adj[w];
            if (back.size() < size_t(2 * M)) back.push_back(uint32_t(i));
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!g.adj[size_t(i)].empty()) continue;
        const float* pi = base + size_t(ids[size_t(i)]) * DIM;
        float best = INFINITY; int bid = -1;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const float d = l2(pi, base + size_t(ids[size_t(j)]) * DIM);
            if (d < best) { best = d; bid = j; }
        }
        if (bid >= 0) { g.adj[size_t(i)].push_back(uint32_t(bid)); g.adj[size_t(bid)].push_back(uint32_t(i)); }
    }
    std::vector<int> comp(size_t(n), -1);
    int ncomp = 0;
    for (int s0 = 0; s0 < n; ++s0) {
        if (comp[size_t(s0)] >= 0) continue;
        std::vector<int> st{s0}; comp[size_t(s0)] = ncomp;
        while (!st.empty()) { const int v = st.back(); st.pop_back();
            for (uint32_t w : g.adj[size_t(v)]) if (comp[w] < 0) { comp[w] = ncomp; st.push_back(int(w)); } }
        ++ncomp;
    }
    if (ncomp > 1) {
        std::vector<uint32_t> rep(size_t(ncomp), 0);
        for (int c = 0; c < ncomp; ++c) for (int i = 0; i < n; ++i) if (comp[size_t(i)] == c) { rep[size_t(c)] = uint32_t(i); break; }
        for (int c = 1; c < ncomp; ++c) {
            g.adj[rep[0]].push_back(rep[size_t(c)]);
            g.adj[rep[size_t(c)]].push_back(rep[0]);
        }
    }
    // farthest-first 代表点
    const int K = std::min(nrep, n);
    g.reps.clear();
    g.reps.push_back(0);
    std::vector<float> mind(size_t(n), INFINITY);
    for (int r = 1; r < K; ++r) {
        const uint32_t last = g.reps.back();
        const float* pl = base + size_t(ids[last]) * DIM;
        float far = -1; uint32_t bid = 0;
        for (int i = 0; i < n; ++i) {
            const float d = l2(pl, base + size_t(ids[size_t(i)]) * DIM);
            if (d < mind[size_t(i)]) mind[size_t(i)] = d;
            if (mind[size_t(i)] > far) { far = mind[size_t(i)]; bid = uint32_t(i); }
        }
        g.reps.push_back(bid);
    }
}

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vec = "recon.bin";
    int k = 50, M = 16, efC = 100, nq_max = 200, nrep = 8, per_concept_N = 50;
    std::vector<int> efs{50};
    std::vector<std::string> modes{"nr", "r"};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--vec") vec = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--efC") efC = std::stoi(nx());
        else if (a == "--reps") nrep = std::stoi(nx());
        else if (a == "--capN") per_concept_N = std::stoi(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--modes") { modes.clear(); std::stringstream ss(nx()); std::string t; while (std::getline(ss, t, ',')) if (!t.empty()) modes.push_back(t); }
        else if (a == "--efs") { efs.clear(); std::stringstream ss(nx()); std::string t; while (std::getline(ss, t, ',')) if (!t.empty()) efs.push_back(std::stoi(t)); }
    }

    std::vector<float> base, queries;
    if (!readf(dir + "/" + vec, base) || !readf(dir + "/queries.bin", queries)) return 2;
    const size_t n = base.size() / DIM;
    const int nq = std::min<int>(int(queries.size() / DIM), nq_max);

    std::vector<int> owner;
    { std::ifstream in(work + "/objects.txt"); std::string l; while (std::getline(in, l)) if (!l.empty()) owner.push_back(std::stoi(l)); }
    std::unordered_map<int, std::vector<int>> children, parents;
    { std::ifstream in(work + "/edges.tsv"); std::string l;
      while (std::getline(in, l)) { if (l.empty()) continue; std::stringstream ss(l); int c, p; ss >> c >> p;
        children[p].push_back(c); parents[c].push_back(p); } }
    std::vector<std::vector<int>> qc;
    { std::ifstream in(work + "/queries.tsv"); std::string l;
      while (std::getline(in, l) && int(qc.size()) < nq) { if (l.empty()) continue;
        std::stringstream ss(l); int cnt; ss >> cnt; std::vector<int> v(cnt); for (int i = 0; i < cnt; ++i) ss >> v[i]; qc.push_back(v); } }
    std::vector<int> gt(size_t(nq) * k, -1);
    { FILE* f = std::fopen((dir + "/gt_" + std::to_string(k) + ".txt").c_str(), "rb");
      if (f) { if (std::fread(gt.data(), sizeof(int32_t), gt.size(), f) != gt.size()) {} std::fclose(f); } }

    std::unordered_map<int, LocalGraph> G;
    {
        std::unordered_map<int, Vec> members;
        for (size_t i = 0; i < n; ++i) members[owner[i]].push_back(uint32_t(i));
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& [c, ids] : members) { LocalGraph g; build_local(base.data(), ids, M, efC, nrep, g); G.emplace(c, std::move(g)); }
        std::fprintf(stderr, "[build] %zu graphs in %.1fs (M=%d efC=%d reps=%d)\n", G.size(),
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), M, efC, nrep);
    }
    for (auto& [c, kids] : children) {
        auto pc = G.find(c);
        if (pc == G.end()) continue;
        LocalGraph& pg = pc->second;
        Vec anchors{pg.entry};
        for (uint32_t r : pg.reps) anchors.push_back(r);
        for (int kid : kids) {
            auto kc = G.find(kid);
            if (kc == G.end()) continue;
            LocalGraph& cg = kc->second;
            for (uint32_t a : anchors) {
                pg.portal[a].push_back(uint32_t(kid));
                cg.portal[cg.entry].push_back(uint32_t(c));
            }
        }
    }

    std::vector<uint32_t> stamp(size_t(n), 0);
    uint32_t token = 0;
    std::vector<std::pair<float, int>> merged;

    for (const std::string& mode : modes) {
        for (int ef : efs) {
            long hit = 0, denom = 0, dists = 0, cov_sum = 0, d_sum = 0;
            const auto t1 = std::chrono::steady_clock::now();
            for (int qi = 0; qi < nq; ++qi) {
                std::unordered_set<int> D;
                {
                    std::vector<int> st = qc[qi];
                    while (!st.empty()) { const int c = st.back(); st.pop_back(); if (!D.insert(c).second) continue;
                        auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
                }
                std::vector<int> inD;
                for (int c : D) if (G.count(c)) inD.push_back(c);
                if (inD.empty()) continue;
                const float* q = queries.data() + size_t(qi) * DIM;

                auto best_entry = [&](int c) -> uint32_t {
                    const LocalGraph& g = G[c];
                    uint32_t bi = g.entry; float bd = INFINITY;
                    for (uint32_t r : g.reps) {
                        const float d = l2(q, base.data() + size_t(g.members[r]) * DIM);
                        ++dists;
                        if (d < bd) { bd = d; bi = r; }
                    }
                    return bi;
                };

                merged.clear();
                if (mode == "nr") {
                    for (int c : inD) {
                        const LocalGraph& g = G[c];
                        ++token;
                        std::priority_queue<std::pair<float, uint32_t>> best;
                        std::priority_queue<std::pair<float, uint32_t>> frontier;
                        auto push = [&](uint32_t li) {
                            const uint32_t gid = g.members[li];
                            if (stamp[gid] == token) return;
                            stamp[gid] = token;
                            const float d = l2(q, base.data() + size_t(gid) * DIM);
                            ++dists;
                            frontier.push({-d, li});
                            if (int(best.size()) < ef) best.push({d, li});
                            else if (d < best.top().first) { best.pop(); best.push({d, li}); }
                        };
                        push(best_entry(c));
                        while (!frontier.empty()) {
                            const auto tp = frontier.top(); frontier.pop();
                            const float dv = -tp.first;
                            if (int(best.size()) >= ef && dv > best.top().first) break;
                            for (uint32_t w : g.adj[tp.second]) push(w);
                        }
                        // 每概念取 top-N 并入结果
                        std::vector<std::pair<float, uint32_t>> local;
                        while (!best.empty()) { local.push_back(best.top()); best.pop(); }
                        std::sort(local.begin(), local.end());
                        for (size_t i = 0; i < local.size() && int(i) < per_concept_N; ++i)
                            merged.push_back({local[i].first, int(g.members[local[i].second])});
                    }
                } else {   // 递归：单队列 + 每概念预算 + 门户桥
                    const int dfull = int(inD.size());
                    const int ef_c = std::max(8, ef / std::max(1, dfull));
                    std::unordered_map<int, std::priority_queue<std::pair<float, uint32_t>>> pc;
                    std::priority_queue<std::pair<float, std::pair<int, uint32_t>>> frontier;
                    std::priority_queue<float> gworst;   // 全局 ef 内最差，用作"是否还值得展开"的地板
                    ++token;
                    auto push = [&](int c, uint32_t li, bool force = false) {
                        const uint32_t gid = G[c].members[li];
                        if (stamp[gid] == token) return;
                        stamp[gid] = token;
                        const float d = l2(q, base.data() + size_t(gid) * DIM);
                        ++dists;
                        auto& b = pc[c];
                        bool keep;
                        if (int(b.size()) < ef_c) { b.push({d, li}); keep = true; }
                        else if (d < b.top().first) { b.pop(); b.push({d, li}); keep = true; }
                        else keep = false;
                        // 即使本概念预算已满，只要还在全局 ef 的地板之上，仍允许展开
                        if (!force && !keep && (!gworst.empty() && gworst.size() >= size_t(ef) && d > gworst.top())) return;
                        if (gworst.size() < size_t(ef)) gworst.push(d);
                        else if (d < gworst.top()) { gworst.pop(); gworst.push(d); }
                        frontier.push({-d, {c, li}});
                    };
                    // 递归下降：范围 D 内的概念逐个播种入口（预算仍按概念切分、队列仍共用）。
                    // 之所以不沿 children 逐跳下降：真实 DAG 存在"无 item 的中间概念"，逐跳会漏掉其子树。
                    for (int c : inD) push(c, best_entry(c), /*force=*/true);
                    while (!frontier.empty()) {
                        const auto tp = frontier.top(); frontier.pop();
                        const float dv = -tp.first;
                        const int c = tp.second.first;
                        const uint32_t li = tp.second.second;
                        auto itc = pc.find(c);
                        if (itc != pc.end() && int(itc->second.size()) >= ef_c && dv > itc->second.top().first) continue;
                        for (uint32_t w : G[c].adj[li]) push(c, w);
                        if (li < G[c].portal.size()) {
                            for (uint32_t tgt : G[c].portal[li]) {
                                if (!D.count(int(tgt))) continue;
                                auto it = G.find(int(tgt));
                                if (it == G.end()) continue;
                                push(int(tgt), best_entry(int(tgt)));
                            }
                        }
                    }
                    cov_sum += long(pc.size()); d_sum += long(inD.size());
                    for (auto& kv : pc) {
                        const LocalGraph& g = G[kv.first];
                        while (!kv.second.empty()) {
                            auto pr = kv.second.top(); kv.second.pop();
                            merged.push_back({pr.first, int(g.members[pr.second])});
                        }
                    }
                }
                const size_t keep = std::min<size_t>(k, merged.size());
                std::partial_sort(merged.begin(), merged.begin() + keep, merged.end());
                std::unordered_set<int> topk;
                for (size_t i = 0; i < keep; ++i) topk.insert(merged[i].second);
                int e = 0, h = 0;
                for (int j = 0; j < k; ++j) { const int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (topk.count(g)) ++h; } }
                hit += h; denom += std::min(k, e);
            }
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            std::printf("{\"mode\":\"%s\",\"ef\":%d,\"k\":%d,\"M\":%d,\"efC\":%d,\"reps\":%d,\"capN\":%d,\"queries\":%d,"
                        "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.0f,"
                        "\"concepts_covered\":%.2f,\"concepts_in_scope\":%.2f}\n",
                        mode.c_str(), ef, k, M, efC, nrep, per_concept_N, nq,
                        double(hit) / double(std::max<long>(denom, 1)), nq / s, double(dists) / nq,
                        double(cov_sum) / nq, double(d_sum) / nq);
            std::fflush(stdout);
        }
    }
    return 0;
}
