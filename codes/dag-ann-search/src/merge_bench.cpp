// 前缀树 + 块合并 + 块内单层图：实现 approaches.md §4/§6 的完整形态。
//
// 流程：
//  1) 概念 DAG → 规范前缀树（每概念选一个主父：深度最大者，平局取 id 小者；其余父记作跨链）
//  2) 按子树对象数做块划分：子树对象数 ≥ τ 或为根 → 自成一块；否则并入父块
//  3) 块内图 = 各概念局部图**保边**拼接（局部 id 平移）+ 边界边（子入口 ↔ 块锚点）
//  4) 块级代表点（farthest-first，K=8）与 MST 边
//  5) 门户桥：跨块 DAG 边（父块代表点 → 子块入口）
//  6) 查询：概念位图求闭包 → 枚举块 → 每块预算 ef_c → 单队列 → 合并
//
// 对照：--tau 0 表示不合并（等价旧实现，每概念一块）。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
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

static RawL2Space g_space(DIM);
static inline float dist2(const float* a, const float* b) { return g_space.dist(a, b); }

// ---------- 单概念局部图（建图同 portal_bench） ----------
struct ConceptGraph {
    Vec members;
    std::vector<Vec> adj;
    Vec reps;
    uint32_t entry = 0;
};

static void shrink_list(const float* base, const Vec& ids, uint32_t base_local, Vec& list, int M) {
    if (int(list.size()) <= M) return;
    std::sort(list.begin(), list.end(), [&](uint32_t a, uint32_t b) {
        return dist2(base + size_t(ids[a]) * DIM, base + size_t(ids[base_local]) * DIM) <
               dist2(base + size_t(ids[b]) * DIM, base + size_t(ids[base_local]) * DIM);
    });
    Vec keep;
    const float* pb = base + size_t(ids[base_local]) * DIM;
    for (uint32_t c : list) {
        if (int(keep.size()) >= M) break;
        const float* pc = base + size_t(ids[c]) * DIM;
        const float dq = dist2(pc, pb);
        bool ok = true;
        for (uint32_t k : keep) if (dist2(pc, base + size_t(ids[k]) * DIM) < dq) { ok = false; break; }
        if (ok) keep.push_back(c);
    }
    list.swap(keep);
}

static void build_concept_graph(const float* base, const Vec& ids, int M, int efC, int K, ConceptGraph& g) {
    const int n = int(ids.size());
    g.members = ids;
    g.adj.assign(size_t(n), {});
    if (n == 1) { g.entry = 0; g.reps = {0}; return; }
    g.adj[0].push_back(1); g.adj[1].push_back(0);
    g.entry = 0;
    std::vector<uint32_t> visited(size_t(n), 0);
    uint32_t token = 0;
    std::vector<std::pair<float, uint32_t>> cands;
    for (int i = 2; i < n; ++i) {
        ++token;
        const float* q = base + size_t(ids[size_t(i)]) * DIM;
        std::priority_queue<std::pair<float, uint32_t>> frontier;
        frontier.push({-dist2(q, base + size_t(ids[g.entry]) * DIM), g.entry});
        visited[g.entry] = token;
        cands.clear();
        while (!frontier.empty()) {
            const auto [nd, v] = frontier.top(); frontier.pop();
            const float dv = -nd;
            if (cands.size() >= size_t(efC)) {
                float worst = 0; for (auto& c : cands) worst = std::max(worst, c.first);
                if (dv > worst) break;
            }
            cands.push_back({dv, v});
            for (uint32_t w : g.adj[v]) {
                if (visited[w] == token) continue;
                visited[w] = token;
                frontier.push({-dist2(q, base + size_t(ids[w]) * DIM), w});
            }
        }
        std::sort(cands.begin(), cands.end());
        if (cands.size() > size_t(efC)) cands.resize(size_t(efC));
        Vec chosen;
        for (auto& c : cands) {
            if (int(chosen.size()) >= M) break;
            const float* p = base + size_t(ids[c.second]) * DIM;
            bool ok = true;
            for (uint32_t s : chosen) if (dist2(p, base + size_t(ids[s]) * DIM) < c.first) { ok = false; break; }
            if (ok) chosen.push_back(c.second);
        }
        if (chosen.empty() && !cands.empty()) chosen.push_back(cands[0].second);
        g.adj[size_t(i)] = chosen;
        for (uint32_t w : chosen) {
            g.adj[w].push_back(uint32_t(i));
            if (int(g.adj[w].size()) > M) shrink_list(base, ids, w, g.adj[w], M);
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!g.adj[size_t(i)].empty()) continue;
        const float* pi = base + size_t(ids[size_t(i)]) * DIM;
        float best = INFINITY; int bid = -1;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const float d = dist2(pi, base + size_t(ids[size_t(j)]) * DIM);
            if (d < best) { best = d; bid = j; }
        }
        if (bid >= 0) { g.adj[size_t(i)].push_back(uint32_t(bid)); g.adj[size_t(bid)].push_back(uint32_t(i)); }
    }
    const int K2 = std::min(K, n);
    g.reps.clear(); g.reps.push_back(0);
    std::vector<float> mind(size_t(n), INFINITY);
    for (int r = 1; r < K2; ++r) {
        const uint32_t last = g.reps.back();
        const float* pl = base + size_t(ids[last]) * DIM;
        float far = -1; uint32_t bid = 0;
        for (int i = 0; i < n; ++i) {
            const float d = dist2(pl, base + size_t(ids[size_t(i)]) * DIM);
            if (d < mind[size_t(i)]) mind[size_t(i)] = d;
            if (mind[size_t(i)] > far) { far = mind[size_t(i)]; bid = uint32_t(i); }
        }
        g.reps.push_back(bid);
    }
}

// ---------- 块 ----------
struct Block {
    Vec members;                  // 全局 id
    std::vector<uint16_t> concept; // 每个成员的归属概念（位图判定用）
    std::vector<uint32_t> adj_off, adj_dat;
    std::vector<uint32_t> portal;  // 每节点可跳转的目标块（简化：块级）
    Vec reps;
    uint32_t entry = 0;
    float radius = 0.f;
    mutable std::vector<uint32_t> vstamp;
};

static bool readf(const std::string& p, std::vector<float>& v) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); const long b = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    v.resize(size_t(b) / sizeof(float));
    const bool ok = std::fread(v.data(), sizeof(float), v.size(), f) == v.size();
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", work = "work/dag-scope/workload-run1", vec = "recon.bin";
    int k = 50, M = 16, efC = 200, nq_max = 200, K = 8, tau = 64, repeat = 1;
    std::vector<int> efs{50}, taus{64};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--work") work = nx();
        else if (a == "--vec") vec = nx();
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--efC") efC = std::stoi(nx());
        else if (a == "--K") K = std::stoi(nx());
        else if (a == "--tau") taus = {std::stoi(nx())};
        else if (a == "--taus") { taus.clear(); std::stringstream ss(nx()); std::string t;
            while (std::getline(ss, t, ',')) if (!t.empty()) taus.push_back(std::stoi(t)); }
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--repeat") repeat = std::max(1, std::stoi(nx()));
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

    // 概念全集
    std::vector<int> cids;
    std::unordered_map<int,int> cpos;
    auto addc = [&](int c) { if (cpos.emplace(c, int(cids.size())).second) cids.push_back(c); };
    for (auto& kv : children) { addc(kv.first); for (int ch : kv.second) addc(ch); }
    for (int o : owner) addc(o);
    const int C = int(cids.size());

    // 直属成员
    std::vector<Vec> mem(size_t(C), Vec{});
    for (size_t i = 0; i < n; ++i) mem[size_t(cpos[owner[i]])].push_back(uint32_t(i));

    // 1) 规范前缀树：主父 = 深度最大者（平局 id 小）
    std::vector<int> depth(size_t(C), 0), mainp(size_t(C), -1);
    {
        std::vector<int> indeg(size_t(C), 0);
        for (auto& kv : children) for (int ch : kv.second) ++indeg[size_t(cpos[ch])];
        std::vector<int> q;
        for (int i = 0; i < C; ++i) if (indeg[size_t(i)] == 0) q.push_back(i);
        for (size_t qi = 0; qi < q.size(); ++qi) {
            const int u = q[qi];
            auto it = children.find(cids[size_t(u)]);
            if (it == children.end()) continue;
            for (int ch : it->second) {
                const int ci = cpos[ch];
                if (depth[size_t(u)] + 1 > depth[size_t(ci)] ||
                    (depth[size_t(u)] + 1 == depth[size_t(ci)] && cids[size_t(u)] < (mainp[size_t(ci)] < 0 ? INT32_MAX : cids[size_t(mainp[size_t(ci)])]))) {
                    depth[size_t(ci)] = depth[size_t(u)] + 1;
                    mainp[size_t(ci)] = u;
                }
                if (--indeg[size_t(ci)] == 0) q.push_back(ci);
            }
        }
    }
    std::vector<Vec> tchild(size_t(C), Vec{});
    for (int i = 0; i < C; ++i) if (mainp[size_t(i)] >= 0) tchild[size_t(mainp[size_t(i)])].push_back(i);

    // 子树对象数（自底向上）
    std::vector<long> sub(size_t(C), 0);
    {
        std::vector<int> order; order.reserve(size_t(C));
        std::vector<int> st;
        for (int i = 0; i < C; ++i) if (mainp[size_t(i)] < 0) st.push_back(i);
        while (!st.empty()) { const int u = st.back(); st.pop_back(); order.push_back(u);
            for (int ch : tchild[size_t(u)]) st.push_back(ch); }
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            sub[size_t(*it)] = long(mem[size_t(*it)].size());
            for (int ch : tchild[size_t(*it)]) sub[size_t(*it)] += sub[size_t(ch)];
        }
        // 复用 order 作为前序
        std::swap(order, order);
    }

    for (int tau : taus) {
    // 2) 块划分（前序：子树 ≥ τ 或根 → 新块；否则并入父块）
    std::vector<int> blk_of(size_t(C), -1);
    std::vector<Vec> blk_concepts;
    {
        std::vector<int> st;
        for (int i = 0; i < C; ++i) if (mainp[size_t(i)] < 0) st.push_back(i);
        while (!st.empty()) {
            const int u = st.back(); st.pop_back();
            if (mainp[size_t(u)] < 0 || sub[size_t(u)] >= long(tau)) {
                blk_of[size_t(u)] = int(blk_concepts.size());
                blk_concepts.push_back({u});
            } else {
                blk_of[size_t(u)] = blk_of[size_t(mainp[size_t(u)])];
                blk_concepts[size_t(blk_of[size_t(u)])].push_back(u);
            }
            for (int ch : tchild[size_t(u)]) st.push_back(ch);
        }
    }
    const int B = int(blk_concepts.size());

    // 3) 建块：保边拼接 + 边界边
    std::vector<Block> blocks(size_t(B), Block{});
    {
        std::unordered_map<int, ConceptGraph> cg;
        for (int i = 0; i < C; ++i) {
            if (mem[size_t(i)].empty()) continue;
            ConceptGraph g; build_concept_graph(base.data(), mem[size_t(i)], M, efC, K, g);
            cg.emplace(cids[size_t(i)], std::move(g));
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < B; ++b) {
            Block& blk = blocks[size_t(b)];
            std::vector<uint32_t> sub_entry;   // 每个子概念在块内的入口（块内局部 id）
            // 保边拼接：按 blk_concepts 顺序追加成员与邻接（局部 id 平移）
            for (int ci : blk_concepts[size_t(b)]) {
                const int c = cids[size_t(ci)];
                auto it = cg.find(c);
                if (it == cg.end()) continue;
                const ConceptGraph& g = it->second;
                const uint32_t base_off = uint32_t(blk.members.size());
                blk.adj_off.resize(base_off + 1);
                blk.adj_off[base_off] = uint32_t(blk.adj_dat.size());
                for (size_t i = 0; i < g.members.size(); ++i) {
                    blk.members.push_back(g.members[i]);
                    blk.concept.push_back(uint16_t(c));
                    for (uint32_t w : g.adj[i]) blk.adj_dat.push_back(base_off + w);
                    blk.adj_off.push_back(uint32_t(blk.adj_dat.size()));
                }
                sub_entry.push_back(base_off + g.entry);
            }
            // 边界边：每个子概念入口 <-> 块锚点（首个入口），双向，独立于块内边追加
            if (sub_entry.size() > 1) {
                const uint32_t anchor = sub_entry[0];
                std::vector<Vec> tmp(blk.members.size());
                for (size_t i = 0; i + 1 < blk.adj_off.size(); ++i)
                    for (uint32_t j = blk.adj_off[i]; j < blk.adj_off[i + 1]; ++j) tmp[i].push_back(blk.adj_dat[j]);
                for (size_t i = 1; i < sub_entry.size(); ++i) {
                    tmp[sub_entry[i]].push_back(anchor);
                    tmp[anchor].push_back(sub_entry[i]);
                }
                blk.adj_dat.clear();
                blk.adj_off.assign(blk.members.size() + 1, 0);
                for (size_t i = 0; i < tmp.size(); ++i) {
                    blk.adj_off[i] = uint32_t(blk.adj_dat.size());
                    for (uint32_t w : tmp[i]) blk.adj_dat.push_back(w);
                }
                blk.adj_off[blk.members.size()] = uint32_t(blk.adj_dat.size());
            }
            // 块级代表点（farthest-first）与入口
            const int nB = int(blk.members.size());
            blk.vstamp.assign(size_t(nB), 0);
            if (nB > 0) {
                const int KK = std::min(K, nB);
                blk.reps.push_back(0);
                std::vector<float> mind(size_t(nB), INFINITY);
                for (int r = 1; r < KK; ++r) {
                    const uint32_t last = blk.reps.back();
                    const float* pl = base.data() + size_t(blk.members[last]) * DIM;
                    float far = -1; uint32_t bid = 0;
                    for (int i = 0; i < nB; ++i) {
                        const float d = dist2(pl, base.data() + size_t(blk.members[size_t(i)]) * DIM);
                        if (d < mind[size_t(i)]) mind[size_t(i)] = d;
                        if (mind[size_t(i)] > far) { far = mind[size_t(i)]; bid = uint32_t(i); }
                    }
                    blk.reps.push_back(bid);
                }
                blk.entry = blk.reps[0];
                float r2 = 0.f;
                const float* pe = base.data() + size_t(blk.members[blk.entry]) * DIM;
                for (int i = 0; i < nB; ++i) r2 = std::max(r2, dist2(pe, base.data() + size_t(blk.members[size_t(i)]) * DIM));
                blk.radius = std::sqrt(r2);
            }
        }
        std::fprintf(stderr, "[blocks] tau=%d -> %d blocks (concepts=%d), build %.1fs\n", tau, B, C,
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }

    // 4) 概念位图（全部概念，含无对象者）
    struct Bits { int words; std::vector<std::vector<uint64_t>> desc; };
    Bits bits;
    {
        bits.words = (C + 63) / 64;
        bits.desc.assign(size_t(C), std::vector<uint64_t>(size_t(bits.words), 0));
        std::vector<int> state(size_t(C), 0);
        std::function<void(int)> dfs = [&](int u) {
            if (state[size_t(u)] == 2) return;
            state[size_t(u)] = 1;
            auto it = children.find(cids[size_t(u)]);
            if (it != children.end()) for (int ch : it->second) {
                const int ci = cpos[ch];
                dfs(ci);
                for (int w = 0; w < bits.words; ++w) bits.desc[size_t(u)][size_t(w)] |= bits.desc[size_t(ci)][size_t(w)];
            }
            bits.desc[size_t(u)][size_t(u) / 64] |= (uint64_t(1) << (u % 64));
            state[size_t(u)] = 2;
        };
        for (int i = 0; i < C; ++i) dfs(i);
    }

    // 5) 查询
    std::vector<std::pair<float,int>> merged;
    std::vector<uint64_t> bm(size_t(bits.words), 0);
    for (int ef : efs) {
        long hit = 0, denom = 0, dists = 0, blk_visited = 0;
        const auto t1 = std::chrono::steady_clock::now();
        for (int round = 0; round < repeat; ++round)
        for (int qi = 0; qi < nq; ++qi) {
            std::fill(bm.begin(), bm.end(), 0ull);
            for (int c : qc[qi]) {
                auto it = cpos.find(c);
                if (it == cpos.end()) continue;
                const auto& d = bits.desc[size_t(it->second)];
                for (int w = 0; w < bits.words; ++w) bm[size_t(w)] |= d[size_t(w)];
            }
            // 参与块
            std::vector<int> inB;
            {
                std::vector<char> seen(size_t(B), 0);
                for (int w = 0; w < bits.words; ++w) {
                    uint64_t x = bm[size_t(w)];
                    while (x) {
                        const int b = __builtin_ctzll(x); x &= x - 1;
                        const int ci = w * 64 + b;
                        if (ci >= C) continue;
                        const int bl = blk_of[size_t(ci)];
                        if (bl >= 0 && !seen[size_t(bl)] && !blocks[size_t(bl)].members.empty()) { seen[size_t(bl)] = 1; inB.push_back(bl); }
                    }
                }
            }
            if (inB.empty()) continue;
            blk_visited += long(inB.size());
            const float* q = queries.data() + size_t(qi) * DIM;
            const int ef_c = std::max(k, ef / std::max<int>(1, int(inB.size())));

            std::vector<std::vector<std::pair<float,uint32_t>>> res(size_t(B), std::vector<std::pair<float,uint32_t>>{});
            std::priority_queue<std::pair<float,std::pair<int,uint32_t>>> frontier;
            static uint32_t token = 0; ++token;
            auto eligible = [&](uint16_t c) {
                auto it = cpos.find(int(c));
                if (it == cpos.end()) return false;
                const int ci = it->second;
                return ((bm[size_t(ci) / 64] >> (ci % 64)) & 1ull) != 0;
            };
            auto push = [&](int b, uint32_t li) {
                Block& Bk = blocks[size_t(b)];
                if (Bk.vstamp[li] == token) return;
                Bk.vstamp[li] = token;
                const float d = dist2(q, base.data() + size_t(Bk.members[li]) * DIM);
                ++dists;
                if (eligible(Bk.concept[li])) {
                    auto& r = res[size_t(b)];
                    if (int(r.size()) < ef_c) { r.push_back({d, li}); std::push_heap(r.begin(), r.end()); }
                    else if (d < r.front().first) { std::pop_heap(r.begin(), r.end()); r.back() = {d, li}; std::push_heap(r.begin(), r.end()); }
                }
                frontier.push({-d, {b, li}});
            };
            for (int b : inB) {
                Block& Bk = blocks[size_t(b)];
                uint32_t bi = Bk.entry; float bd = INFINITY;
                for (uint32_t r : Bk.reps) {
                    const float d = dist2(q, base.data() + size_t(Bk.members[r]) * DIM);
                    ++dists;
                    if (d < bd) { bd = d; bi = r; }
                }
                push(b, bi);
            }
            while (!frontier.empty()) {
                const auto tp = frontier.top(); frontier.pop();
                const float dv = -tp.first;
                const int b = tp.second.first;
                const uint32_t li = tp.second.second;
                Block& Bk = blocks[size_t(b)];
                const auto& r = res[size_t(b)];
                if (int(r.size()) >= ef_c && dv > r.front().first) continue;
                for (uint32_t j = Bk.adj_off[li]; j < Bk.adj_off[li + 1]; ++j) push(b, Bk.adj_dat[j]);
            }
            merged.clear();
            for (int b : inB) for (auto& pr : res[size_t(b)]) merged.push_back({pr.first, int(blocks[size_t(b)].members[pr.second])});
            const size_t keep = std::min<size_t>(k, merged.size());
            std::partial_sort(merged.begin(), merged.begin() + keep, merged.end());
            std::unordered_set<int> topk;
            for (size_t i = 0; i < keep; ++i) topk.insert(merged[i].second);
            int e = 0, h = 0;
            for (int j = 0; j < k; ++j) { const int g = gt[size_t(qi) * k + j]; if (g >= 0) { ++e; if (topk.count(g)) ++h; } }
            hit += h; denom += std::min(k, e);
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::printf("{\"tau\":%d,\"blocks\":%d,\"ef\":%d,\"k\":%d,\"M\":%d,\"efC\":%d,\"queries\":%d,\"repeat\":%d,"
                    "\"recall\":%.6f,\"qps\":%.1f,\"dists_per_q\":%.0f,\"blocks_per_q\":%.2f}\n",
                    tau, B, ef, k, M, efC, nq, repeat,
                    double(hit) / double(std::max<long>(denom, 1)),
                    double(nq) * repeat / s,
                    double(dists) / (double(nq) * repeat),
                    double(blk_visited) / (double(nq) * repeat));
        std::fflush(stdout);
    }
    }   // tau
    return 0;
}
