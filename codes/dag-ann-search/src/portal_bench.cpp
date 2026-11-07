// NodeANN+ v2：三项改动同时上
//  (1) 构造：插入时对**邻居表**做 shrink（RNG 启发式，与 hnswlib 的 getNeighborsByHeuristic2 同规则），
//      不再"只追加入边、截断 2M"。目标：把单层图的可导航性拉到 hnswlib 水平。
//  (2) 预算：递归模式下，概念预算满后若候选优于**全局第 k 名**，仍允许入队与展开（有限超额）。
//  (3) 骨架：每概念在 farthest-first 代表点上建 MST，把 MST 边作为图边加入（低成本远程捷径）；
//      门户桥端点改为"距子概念入口最近的父侧代表点/入口"。
//
// 两种查询：nr（每概念独立 + 每概念预算） / r（单队列 + 每概念预算 + 门户桥 + 结构式播种）。
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
using Vec = std::vector<uint32_t>;

struct LocalGraph {
    Vec members;
    std::vector<Vec> adj;
    std::vector<Vec> portal;
    Vec reps;
    Vec mst_edges;      // 扁平：(u,v) 对
    mutable std::vector<uint32_t> vstamp;   // 紧凑 visited（按局部 id）
    float radius = 0.f;                     // 覆盖半径（到入口的最大距离）
    std::vector<uint32_t> adj_off;          // n+1 偏移
    std::vector<uint32_t> adj_dat;          // 连续邻接
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
// 距离：必须走项目的 RawL2Space（hnswlib L2Space::get_dist_func），与基线同源
static RawL2Space g_space(DIM);
static inline float l2(const float* a, const float* b) { return g_space.dist(a, b); }

// RNG 启发式：候选按到 base 的距离升序，保留 v 当且仅当对已保留的 u 都有 d(v,u) > d(v,base)
static void shrink_list(const float* base, const Vec& ids, uint32_t base_local, Vec& list, int M) {
    if (int(list.size()) <= M) return;
    std::sort(list.begin(), list.end(), [&](uint32_t a, uint32_t b) {
        return l2(base + size_t(ids[a]) * DIM, base + size_t(ids[base_local]) * DIM) <
               l2(base + size_t(ids[b]) * DIM, base + size_t(ids[base_local]) * DIM);
    });
    Vec keep;
    const float* pb = base + size_t(ids[base_local]) * DIM;
    for (uint32_t c : list) {
        if (int(keep.size()) >= M) break;
        const float* pc = base + size_t(ids[c]) * DIM;
        const float dq = l2(pc, pb);
        bool ok = true;
        for (uint32_t k : keep) if (l2(pc, base + size_t(ids[k]) * DIM) < dq) { ok = false; break; }
        if (ok) keep.push_back(c);
    }
    list.swap(keep);
}

static void build_local(const float* base, const Vec& ids, int M, int efC, int nrep, LocalGraph& g) {
    const int n = int(ids.size());
    g.members = ids;
    g.adj.assign(size_t(n), {});
    g.portal.assign(size_t(n), {});
    g.vstamp.assign(size_t(n), 0);
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
            for (uint32_t s : chosen) if (l2(p, base + size_t(ids[s]) * DIM) < c.first) { ok = false; break; }
            if (ok) chosen.push_back(c.second);
        }
        if (chosen.empty() && !cands.empty()) chosen.push_back(cands[0].second);

        g.adj[size_t(i)] = chosen;
        for (uint32_t w : chosen) {
            g.adj[w].push_back(uint32_t(i));
            // (1) 邻居表 shrink：入边不再无脑追加
            if (int(g.adj[w].size()) > M) shrink_list(base, ids, w, g.adj[w], M);
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
    // 连通性桥接
    {
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
    }
    // 代表点：farthest-first
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
    // 覆盖半径：以入口为 anchor
    {
        const float* pa = base + size_t(ids[g.entry]) * DIM;
        float r = 0.f;
        for (int i = 0; i < n; ++i) r = std::max(r, l2(pa, base + size_t(ids[size_t(i)]) * DIM));
        g.radius = std::sqrt(r);
    }
    // (3) reps 上的 MST（Prim，K 很小），MST 边作为图边加入
    {
        const int K2 = int(g.reps.size());
        if (K2 >= 2) {
            std::vector<bool> in(size_t(K2), false);
            std::vector<float> key(size_t(K2), INFINITY);
            std::vector<int> par(size_t(K2), -1);
            key[0] = 0;
            for (int it = 0; it < K2; ++it) {
                int u = -1;
                for (int k = 0; k < K2; ++k) if (!in[size_t(k)] && (u < 0 || key[size_t(k)] < key[size_t(u)])) u = k;
                in[size_t(u)] = true;
                for (int k = 0; k < K2; ++k) {
                    if (in[size_t(k)]) continue;
                    const float d = l2(base + size_t(ids[g.reps[size_t(u)]]) * DIM,
                                       base + size_t(ids[g.reps[size_t(k)]]) * DIM);
                    if (d < key[size_t(k)]) { key[size_t(k)] = d; par[size_t(k)] = u; }
                }
            }
            for (int k = 1; k < K2; ++k) {
                if (par[size_t(k)] < 0) continue;
                const uint32_t a = g.reps[size_t(k)], b = g.reps[size_t(par[size_t(k)])];
                g.adj[a].push_back(b);
                g.adj[b].push_back(a);
                g.mst_edges.push_back(a); g.mst_edges.push_back(b);
            }
        }
    }
}


// ---- 手写堆（避免 std::push_heap/pop_heap 与 pair 移动的开销） ----
struct MinHeap {                       // 按距离升序弹出
    std::vector<float> d;
    std::vector<uint64_t> v;           // (concept_idx<<32)|local_id
    void clear() { d.clear(); v.clear(); }
    size_t size() const { return d.size(); }
    bool empty() const { return d.empty(); }
    void push(float dist, uint64_t id) {
        d.push_back(dist); v.push_back(id);
        size_t i = d.size() - 1;
        while (i) {
            const size_t par = (i - 1) / 2;
            if (d[par] <= d[i]) break;
            std::swap(d[par], d[i]); std::swap(v[par], v[i]); i = par;
        }
    }
    void pop() {
        d[0] = d.back(); v[0] = v.back(); d.pop_back(); v.pop_back();
        size_t i = 0; const size_t n = d.size();
        while (true) {
            size_t l = 2 * i + 1, r = l + 1, m = i;
            if (l < n && d[l] < d[m]) m = l;
            if (r < n && d[r] < d[m]) m = r;
            if (m == i) break;
            std::swap(d[m], d[i]); std::swap(v[m], v[i]); i = m;
        }
    }
};
struct MaxHeap {                       // 结果表：容量 ef_c，保留最小的一批
    std::vector<float> d;
    std::vector<uint32_t> v;
    void reserve(size_t n) { d.reserve(n + 1); v.reserve(n + 1); }
    size_t size() const { return d.size(); }
    bool empty() const { return d.empty(); }
    float worst() const { return d.empty() ? INFINITY : d[0]; }
    void push(float dist, uint32_t id) {
        d.push_back(dist); v.push_back(id);
        size_t i = d.size() - 1;
        while (i) { const size_t par = (i - 1) / 2;
            if (d[par] >= d[i]) break;
            std::swap(d[par], d[i]); std::swap(v[par], v[i]); i = par; }
    }
    void replace_top(float dist, uint32_t id) {
        d[0] = dist; v[0] = id;
        size_t i = 0; const size_t n = d.size();
        while (true) { size_t l = 2 * i + 1, r = l + 1, m = i;
            if (l < n && d[l] > d[m]) m = l;
            if (r < n && d[r] > d[m]) m = r;
            if (m == i) break;
            std::swap(d[m], d[i]); std::swap(v[m], v[i]); i = m; }
    }
};

static void flatten_adj(LocalGraph& g) {
    const size_t n = g.adj.size();
    g.adj_off.assign(n + 1, 0);
    size_t tot = 0;
    for (size_t i = 0; i < n; ++i) { g.adj_off[i] = uint32_t(tot); tot += g.adj[i].size(); }
    g.adj_off[n] = uint32_t(tot);
    g.adj_dat.resize(tot);
    size_t k2 = 0;
    for (size_t i = 0; i < n; ++i) for (uint32_t w : g.adj[i]) g.adj_dat[k2++] = w;
    std::vector<Vec>().swap(g.adj);   // 释放原结构，避免两套并存
}

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vec = "recon.bin";
    int k = 50, M = 16, efC = 200, nq_max = 200, nrep = 8, per_concept_N = 50, repeat = 1;
    bool overflow = true;
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
        else if (a == "--repeat") repeat = std::max(1, std::stoi(nx()));
        else if (a == "--no-overflow") overflow = false;
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
        std::fprintf(stderr, "[build] %zu graphs in %.1fs (M=%d efC=%d reps=%d shrink+MST)\n", G.size(),
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), M, efC, nrep);
    }
    for (auto& kv : G) flatten_adj(kv.second);
    // 稠密索引：查询热路径不再对 unordered_map 做哈希
    std::unordered_map<int, int> cpos;
    std::vector<LocalGraph*> Gp;
    {
        Gp.reserve(G.size());
        for (auto& kv : G) { cpos[kv.first] = int(Gp.size()); Gp.push_back(&kv.second); }
    }
    // (3) 门户桥：父侧选"距子入口最近"的 rep 或入口
    for (auto& [c, kids] : children) {
        auto pc = G.find(c);
        if (pc == G.end()) continue;
        LocalGraph& pg = pc->second;
        for (int kid : kids) {
            auto kc = G.find(kid);
            if (kc == G.end()) continue;
            LocalGraph& cg = kc->second;
            const float* ke = base.data() + size_t(cg.members[cg.entry]) * DIM;
            uint32_t anchor = pg.entry;
            float bd = l2(ke, base.data() + size_t(pg.members[pg.entry]) * DIM);
            for (uint32_t r : pg.reps) {
                const float d = l2(ke, base.data() + size_t(pg.members[r]) * DIM);
                if (d < bd) { bd = d; anchor = r; }
            }
            pg.portal[anchor].push_back(uint32_t(kid));
            cg.portal[cg.entry].push_back(uint32_t(c));
        }
    }

    std::vector<uint32_t> stamp(size_t(n), 0);
    uint32_t token = 0;
    std::vector<std::pair<float, int>> merged;
    // 每查询独立的入口缓存（绝不复用上一查询的结果）
    std::vector<uint32_t> ec_stamp(Gp.size(), 0), ec_li(Gp.size(), 0);
    uint32_t ec_token = 0;

    for (const std::string& mode : modes) {
        for (int ef : efs) {
            long hit = 0, denom = 0, dists = 0, cov_sum = 0, d_sum = 0;
            double best_round = 1e30, sum_round = 0;
            const auto t1 = std::chrono::steady_clock::now();
            for (int round = 0; round < repeat; ++round)
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

                ++ec_token;
                auto best_entry = [&](int c) -> uint32_t {
                    const int ci = cpos[c];
                    if (ec_stamp[size_t(ci)] == ec_token) return ec_li[size_t(ci)];
                    const LocalGraph& g = *Gp[size_t(ci)];
                    uint32_t bi = g.entry; float bd = l2(q, base.data() + size_t(g.members[g.entry]) * DIM);
                    ++dists;
                    for (uint32_t r : g.reps) {
                        const float d = l2(q, base.data() + size_t(g.members[r]) * DIM);
                        ++dists;
                        if (d < bd) { bd = d; bi = r; }
                    }
                    ec_stamp[size_t(ci)] = ec_token;
                    ec_li[size_t(ci)] = bi;
                    return bi;
                };

                merged.clear();
                if (mode == "nr") {
                    for (int c : inD) {
                        const LocalGraph& g = G[c];
                        ++token;
                        std::priority_queue<std::pair<float, uint32_t>> best, frontier;
                        auto push = [&](uint32_t li) {
                            const uint32_t gid = g.members[li];
                            if (g.vstamp[li] == token) return;
                            g.vstamp[li] = token;
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
                            for (uint32_t kk = g.adj_off[tp.second]; kk < g.adj_off[tp.second + 1]; ++kk) push(g.adj_dat[kk]);
                        }
                        std::vector<std::pair<float, uint32_t>> local;
                        while (!best.empty()) { local.push_back(best.top()); best.pop(); }
                        std::sort(local.begin(), local.end());
                        for (size_t i = 0; i < local.size() && int(i) < per_concept_N; ++i)
                            merged.push_back({local[i].first, int(g.members[local[i].second])});
                    }
                } else {
                    const int ef_c = std::max(k, ef / std::max<int>(1, int(inD.size())));
                    // 每概念结果表：稠密数组 + 手动堆（避免 map 查找与 priority_queue 的额外封装）
                    static std::vector<MaxHeap> pc;
                    static std::vector<int> touched;
                    if (pc.size() < Gp.size()) pc.resize(Gp.size());
                    touched.clear();
                    MinHeap frontier;
                    std::priority_queue<float> gworst;   // 全局 k 名最差（用于超额放行）
                    ++token;
                    auto push = [&](int ci, uint32_t li, bool force = false) {
                        LocalGraph& g = *Gp[ci];
                        if (g.vstamp[li] == (uint32_t)token) return;
                        g.vstamp[li] = (uint32_t)token;
                        const float d = l2(q, base.data() + size_t(g.members[li]) * DIM);
                        ++dists;
                        auto& b = pc[size_t(ci)];
                        if (b.empty()) { touched.push_back(ci); b.reserve(size_t(ef_c) + 1); }
                        bool keep;
                        if (int(b.size()) < ef_c) { b.push(d, li); keep = true; }
                        else if (d < b.worst()) { b.replace_top(d, li); keep = true; }
                        else keep = false;
                        bool slack = overflow && !gworst.empty() && gworst.size() >= size_t(k) && d <= gworst.top();
                        if (!force && !keep && !slack) return;
                        if (gworst.size() < size_t(k)) gworst.push(d);
                        else if (d < gworst.top()) { gworst.pop(); gworst.push(d); }
                        frontier.push(d, (uint64_t(ci) << 32) | uint64_t(li));
                    };
                    auto best_entry_ci = [&](int ci) -> uint32_t {
                        if (ec_stamp[size_t(ci)] == ec_token) return ec_li[size_t(ci)];
                        LocalGraph& g = *Gp[ci];
                        uint32_t bi = g.entry;
                        float bd = l2(q, base.data() + size_t(g.members[g.entry]) * DIM);
                        ++dists;
                        for (uint32_t r : g.reps) {
                            const float d = l2(q, base.data() + size_t(g.members[r]) * DIM);
                            ++dists;
                            if (d < bd) { bd = d; bi = r; }
                        }
                        ec_stamp[size_t(ci)] = ec_token;
                        ec_li[size_t(ci)] = bi;
                        return bi;
                    };
                    // 概念级下界：d(q,anchor) - radius，升序准入；下界超过当前第 k 名即整概念跳过
                    struct Cand { float lb; int ci; };
                    std::vector<Cand> order; order.reserve(inD.size());
                    for (int c : inD) {
                        const int ci = cpos[c];
                        const LocalGraph& g = *Gp[size_t(ci)];
                        const float dq = std::sqrt(l2(q, base.data() + size_t(g.members[g.entry]) * DIM));
                        ++dists;
                        order.push_back({std::max(0.f, dq - g.radius), ci});
                    }
                    std::sort(order.begin(), order.end(), [](const Cand& a, const Cand& b) { return a.lb < b.lb; });
                    for (const Cand& cd : order) {
                        if (!gworst.empty() && gworst.size() >= size_t(k) && cd.lb * cd.lb > gworst.top()) continue;
                        push(cd.ci, best_entry_ci(cd.ci), /*force=*/true);
                    }
                    while (!frontier.empty()) {
                        const float dv = frontier.d[0];
                        const uint64_t packed = frontier.v[0];
                        frontier.pop();
                        const int ci = int(packed >> 32);
                        const uint32_t li = uint32_t(packed & 0xffffffffu);
                        LocalGraph& g = *Gp[size_t(ci)];
                        const auto& b = pc[size_t(ci)];
                        if (int(b.size()) >= ef_c && dv > b.worst()) {
                            if (!(overflow && !gworst.empty() && dv <= gworst.top())) continue;
                        }
                        for (uint32_t kk = g.adj_off[li]; kk < g.adj_off[li + 1]; ++kk) push(ci, g.adj_dat[kk]);
                        if (li < g.portal.size()) {
                            for (uint32_t tgt : g.portal[li]) {
                                if (!D.count(int(tgt))) continue;
                                auto it = cpos.find(int(tgt));
                                if (it == cpos.end()) continue;
                                push(it->second, best_entry_ci(it->second), /*force=*/false);
                            }
                        }
                    }
                    cov_sum += long(touched.size()); d_sum += long(inD.size());
                    for (int ci : touched) {
                        auto& b = pc[size_t(ci)];
                        const LocalGraph& g = *Gp[size_t(ci)];
                        for (size_t i = 0; i < b.size(); ++i) merged.push_back({b.d[i], int(g.members[b.v[i]])});
                        b.d.clear(); b.v.clear();
                    }
                }
                const size_t keep = std::min<size_t>(k, merged.size());
                std::partial_sort(merged.begin(), merged.begin() + keep, merged.end());
                std::unordered_set<int> topk;
                for (size_t i = 0; i < keep; ++i) topk.insert(merged[i].second);
                int e = 0, h = 0;
                for (int j = 0; j < k; ++j) { const int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (topk.count(g)) ++h; } }
                hit += h; denom += std::min(k, e);
                if (qi + 1 == nq) {   // 一轮结束
                    const auto now = std::chrono::steady_clock::now();
                    const double rs = std::chrono::duration<double>(now - t1).count();
                    const double this_round = (round == 0) ? rs : (rs - sum_round);
                    sum_round = rs;
                    best_round = std::min(best_round, this_round);
                }
            }
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            std::printf("{\"mode\":\"%s\",\"ef\":%d,\"k\":%d,\"M\":%d,\"efC\":%d,\"reps\":%d,\"overflow\":%d,\"queries\":%d,\"repeat\":%d,"
                        "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.0f,\"concepts_covered\":%.2f,\"concepts_in_scope\":%.2f}\n",
                        mode.c_str(), ef, k, M, efC, nrep, int(overflow), nq, repeat,
                        double(hit) / double(std::max<long>(denom, 1)),
                        double(nq) * repeat / s,
                        double(dists) / (double(nq) * repeat),
                        double(cov_sum) / (double(nq) * repeat), double(d_sum) / (double(nq) * repeat));
            std::fflush(stdout);
        }
    }
    return 0;
}
