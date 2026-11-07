// NodeANN+ 执行臂：单层局部图 + 唯一入口 + 门户桥，单队列。
//
// 三个臂（同一份数据、同一标量距离、同一 ef 预算、同一 GT、单线程）：
//   P0  local_only : 每概念一张单层图；查询把 **范围内所有概念的入口** 放进同一个队列，
//                    图内边只在本概念内（结构跳过），队列共享 ef 预算。→ 与 NodeANN 同构，
//                    但把内层索引换成单层图，用来隔离"内层索引"这一因子。
//   P2  portal     : 只把 **覆盖概念的顶部入口**（其祖先不在范围内的那批）放进队列；
//                    跨概念走 **门户桥**（父入口 ↔ 子入口，双向，独立邻接表），
//                    因此靠桥"跳"进子树，而不是把所有入口都塞进队列。
//
// 构建：单层图 = 增量插入 + 启发式剪边（RNG 规则），不是精确 kNN 图；
//       efC 为构建预算，M 为度上界。
// 口径：PQ 还原 float32、k=50、ef 为查询预算、recall 以 gt_50.txt 为 GT。
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
    Vec members;                 // local -> global id
    std::vector<Vec> adj;        // 局部邻接（保边：只增不减，剪枝在建图时做）
    std::vector<Vec> portal;     // 门户桥（独立表，不计入局部度）
    uint32_t entry = 0;          // 局部入口
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

// 增量插入 + 启发式剪边（RNG：保留的候选与已选点之间必须比到查询点更远）
static void build_local(const float* base, const Vec& ids, int M, int efC, LocalGraph& g) {
    const int n = int(ids.size());
    g.members = ids;
    g.adj.assign(size_t(n), {});
    g.portal.assign(size_t(n), {});   // 门户槽与局部节点一一对应，避免越界写
    if (n == 1) { g.entry = 0; return; }
    g.adj[0].push_back(1);
    g.adj[1].push_back(0);
    g.entry = 0;

    std::vector<uint32_t> visited(size_t(n), 0);
    uint32_t token = 0;
    std::vector<std::pair<float, uint32_t>> cands;

    for (int i = 2; i < n; ++i) {
        ++token;
        const float* q = base + size_t(ids[size_t(i)]) * DIM;
        // 贪心 beam（构建期）
        std::priority_queue<std::pair<float, uint32_t>> frontier;  // min-heap via -d
        frontier.push({-l2(q, base + size_t(ids[g.entry]) * DIM), g.entry});
        visited[g.entry] = token;
        cands.clear();
        while (!frontier.empty()) {
            const auto [nd, v] = frontier.top(); frontier.pop();
            const float dv = -nd;
            if (cands.size() >= size_t(efC) && dv > 0) {
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

        // 启发式选择
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
    // 连通性修补：孤立节点补一条最近边
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
    // 组件桥接（每组件代表 -> 0 号组件代表）
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

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vec = "recon.bin";
    int k = 50, M = 16, efC = 100, nq_max = 200;
    std::vector<int> efs{50};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--vec") vec = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--efC") efC = std::stoi(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--efs") { efs.clear(); std::stringstream ss(nx()); std::string t;
            while (std::getline(ss, t, ',')) if (!t.empty()) efs.push_back(std::stoi(t)); }
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

    // 每概念单层图
    std::unordered_map<int, LocalGraph> G;
    std::unordered_map<int, size_t> gid2concept;   // 无（用 owner 反查即可）
    {
        std::unordered_map<int, Vec> members;
        for (size_t i = 0; i < n; ++i) members[owner[i]].push_back(uint32_t(i));
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& [c, ids] : members) { LocalGraph g; build_local(base.data(), ids, M, efC, g); G.emplace(c, std::move(g)); }
        std::fprintf(stderr, "[build] %zu local graphs in %.1fs (M=%d efC=%d)\n", G.size(),
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), M, efC);
    }

    // 门户桥：父入口 <-> 子入口（双向，独立邻接）
    // 为让父子之间有多条通道，桥接父的入口 + 父的最远代表，到子的入口。
    for (auto& [c, kids] : children) {
        auto pc = G.find(c);
        if (pc == G.end()) continue;
        LocalGraph& pg = pc->second;   // 注意：不得再用 G[c] 触发 rehash，否则引用悬空
        // 父侧端点：入口 + 距入口最远的 1 个成员（粗略代表）
        std::vector<uint32_t> anchors{pg.entry};
        {
            float far = -1; uint32_t bid = pg.entry;
            const float* e = base.data() + size_t(pg.members[pg.entry]) * DIM;
            for (uint32_t li = 0; li < pg.members.size(); ++li) {
                const float d = l2(e, base.data() + size_t(pg.members[li]) * DIM);
                if (d > far) { far = d; bid = li; }
            }
            anchors.push_back(bid);
        }
        for (int kid : kids) {
            auto kc = G.find(kid);
            if (kc == G.end()) continue;
            LocalGraph& cg = kc->second;
            for (uint32_t a : anchors) {
                pg.portal[a].push_back(uint32_t(kid));            // 用 concept id 作为"跨图指针"
                cg.portal[cg.entry].push_back(uint32_t(c));       // 反向
            }
        }
    }

    std::vector<uint32_t> stamp(size_t(n), 0);
    uint32_t token = 0;
    std::vector<std::pair<float, int>> merged;

    for (int ef : efs) {
        for (int arm = 0; arm < 2; ++arm) {   // 0 = local_only, 1 = portal
            const char* name = arm == 0 ? "P0_local_only" : "P2_portal";
            long hit = 0, denom = 0, dists = 0;
            const auto t1 = std::chrono::steady_clock::now();
            for (int qi = 0; qi < nq; ++qi) {
                // 范围 D（闭包）与"顶部概念"（祖先不在 D 内）
                std::unordered_set<int> D;
                {
                    std::vector<int> st = qc[qi];
                    while (!st.empty()) { const int c = st.back(); st.pop_back(); if (!D.insert(c).second) continue;
                        auto it = children.find(c); if (it != children.end()) for (int w : it->second) st.push_back(w); }
                }
                std::vector<int> seeds;
                for (int c : D) {
                    if (!G.count(c)) continue;                    // 该概念没有 item，跳过
                    bool has_anc_in_D = false;
                    for (int p : parents[c]) if (D.count(p)) { has_anc_in_D = true; break; }
                    if (arm == 0 || !has_anc_in_D) seeds.push_back(c);
                }
                if (seeds.empty()) { for (int c : D) if (G.count(c)) seeds.push_back(c); }
                if (seeds.empty()) continue;

                const float* q = queries.data() + size_t(qi) * DIM;
                ++token;
                std::priority_queue<std::pair<float, int>> best;                  // max-heap of best ef (global id)
                std::priority_queue<std::pair<float, std::pair<int, uint32_t>>> frontier;  // -d, (concept, local)
                auto push = [&](int c, uint32_t li) {
                    const uint32_t gid = G[c].members[li];
                    if (stamp[gid] == token) return;
                    stamp[gid] = token;
                    const float d = l2(q, base.data() + size_t(gid) * DIM);
                    ++dists;
                    frontier.push({-d, {c, li}});
                    if (int(best.size()) < ef) best.push({d, int(gid)});
                    else if (d < best.top().first) { best.pop(); best.push({d, int(gid)}); }
                };
                for (int c : seeds) push(c, G[c].entry);

                while (!frontier.empty()) {
                    const auto top = frontier.top(); frontier.pop();
                    const float dv = -top.first;
                    if (int(best.size()) >= ef && dv > best.top().first) break;
                    const int c = top.second.first;
                    const uint32_t li = top.second.second;
                    for (uint32_t w : G[c].adj[li]) push(c, w);
                    if (arm == 1 && li < G[c].portal.size()) {
                        for (uint32_t tgt : G[c].portal[li]) {
                            auto it = G.find(int(tgt));
                            if (it == G.end()) continue;
                            if (!D.count(int(tgt))) continue;      // 只跳进范围内的概念
                            push(int(tgt), it->second.entry);
                        }
                    }
                }
                merged.clear();
                while (!best.empty()) { merged.push_back({best.top().first, best.top().second}); best.pop(); }
                const size_t keep = std::min<size_t>(k, merged.size());
                std::partial_sort(merged.begin(), merged.begin() + keep, merged.end());
                std::unordered_set<int> topk;
                for (size_t i = 0; i < keep; ++i) topk.insert(merged[i].second);
                int e = 0, h = 0;
                for (int j = 0; j < k; ++j) { const int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (topk.count(g)) ++h; } }
                hit += h; denom += std::min(k, e);
            }
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            std::printf("{\"arm\":\"%s\",\"ef\":%d,\"k\":%d,\"M\":%d,\"efC\":%d,\"queries\":%d,"
                        "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.0f}\n",
                        name, ef, k, M, efC, nq, double(hit) / double(std::max<long>(denom, 1)), nq / s,
                        double(dists) / nq);
            std::fflush(stdout);
        }
    }
    return 0;
}
